#include "infrastructure/anonymization_verifier.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <sstream>

#include "application/use_cases/scan_library.hpp"
#include "infrastructure/anonymization_placeholder.hpp"
#include "infrastructure/engine/libdjinterop_engine_reader.hpp"
#include "infrastructure/rekordbox/anlz_file.hpp"
#include "infrastructure/rekordbox/generated/rekordbox_anlz.h"
#include "infrastructure/rekordbox/kaitai_rekordbox_reader.hpp"

namespace seabass::infrastructure
{

namespace fs = std::filesystem;
using Anlz = rekordbox_anlz_t;

namespace
{

bool isHexDigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

// How many leading hex digits, capped: the hash is masked to 0xFFFFFF, so
// a genuine placeholder never starts with more than six.
size_t hexPrefixLength(const std::string &value)
{
    size_t i = 0;
    while (i < value.size() && isHexDigit(value[i])) {
        ++i;
    }
    return i;
}

bool isPrefixOf(const std::string &candidate, const std::string &whole)
{
    return candidate.size() <= whole.size() && whole.compare(0, candidate.size(), candidate) == 0;
}

std::string readAnlzPath(const std::string &sectionBytes)
{
    // Same framing the scrubber uses: length of this section's header at
    // +4, length of the path at +12, UTF-16BE text at +lenHeader.
    constexpr size_t LenHeaderOffset = 4;
    constexpr size_t LenPathOffset = 12;
    if (sectionBytes.size() < LenPathOffset + 4) {
        return {};
    }
    auto readU32BE = [&sectionBytes](size_t at) {
        return (static_cast<uint32_t>(static_cast<unsigned char>(sectionBytes[at])) << 24)
            | (static_cast<uint32_t>(static_cast<unsigned char>(sectionBytes[at + 1])) << 16)
            | (static_cast<uint32_t>(static_cast<unsigned char>(sectionBytes[at + 2])) << 8)
            | static_cast<uint32_t>(static_cast<unsigned char>(sectionBytes[at + 3]));
    };
    const uint32_t lenHeader = readU32BE(LenHeaderOffset);
    const uint32_t lenPath = readU32BE(LenPathOffset);
    if (lenPath < 4 || lenHeader + lenPath > sectionBytes.size()) {
        return {};
    }
    const size_t units = lenPath / 2 - 1;
    std::string path;
    for (size_t u = 0; u < units; ++u) {
        path.push_back(sectionBytes[lenHeader + u * 2 + 1]);
    }
    while (!path.empty() && (path.back() == ' ' || path.back() == '\0')) {
        path.pop_back();
    }
    return path;
}

bool isAnalysisFile(const fs::path &path)
{
    const std::string ext = path.extension().string();
    return ext == ".DAT" || ext == ".EXT" || ext == ".2EX";
}

// Files this project's own test harness writes beside a set. They are not
// part of an export and their presence is not a leak.
bool isHarnessFile(const std::string &name)
{
    return name == "SET-EXPECTATIONS.txt" || name == "REFUSAL-BASELINE.txt";
}

}  // namespace

bool looksLikeHashPlaceholder(const std::string &value, const std::string &kind)
{
    if (value.empty()) {
        return true;  // an empty field carries nothing
    }
    // The "no real value to hash" form, possibly truncated.
    if (isPrefixOf(value, kind + " (none)")) {
        return true;
    }
    const size_t hex = hexPrefixLength(value);
    if (hex == 0 || hex > 6) {
        return false;
    }
    const std::string rest = value.substr(hex);
    if (rest.empty()) {
        return true;  // truncated down to the hash alone
    }
    if (rest[0] != ' ') {
        return false;
    }
    // The label is written after the hash precisely so truncation eats the
    // label and not the part that makes tracks distinguishable.
    return isPrefixOf(rest.substr(1), kind);
}

bool looksLikeFilenamePlaceholder(const std::string &value)
{
    if (value.empty()) {
        return true;
    }
    if (isPrefixOf(value, std::string("unknown.mp3"))) {
        return true;
    }
    const size_t hex = hexPrefixLength(value);
    if (hex == 0 || hex > 6) {
        return false;
    }
    const std::string rest = value.substr(hex);
    if (rest.empty()) {
        return true;
    }
    if (rest[0] != '.') {
        return false;
    }
    // Whatever the real file's extension was, letters and digits only.
    return std::all_of(rest.begin() + 1, rest.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)); });
}

bool looksLikeIndexedPlaceholder(const std::string &value, const std::string &kind)
{
    if (value.empty()) {
        return true;
    }
    if (!isPrefixOf(kind, value)) {
        // Could still be a truncation of the label itself.
        return isPrefixOf(value, kind);
    }
    const std::string rest = value.substr(kind.size());
    if (rest.empty()) {
        return true;
    }
    if (rest[0] != ' ') {
        return false;
    }
    return std::all_of(rest.begin() + 1, rest.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

std::string AnonymizationVerification::describe() const
{
    std::ostringstream out;
    out << (ok ? "Anonymization verified." : "ANONYMIZATION CHECK FAILED -- do not share this export.") << "\n";
    out << "Analysis files checked: " << analysisFilesChecked << "\n";
    out << "rekordbox tracks sampled: " << rekordboxTracksSampled << "\n";
    out << "Engine tracks sampled: " << engineTracksSampled << "\n";
    if (!problems.empty()) {
        out << "\nProblems (" << problems.size() << "):\n";
        for (const auto &problem : problems) {
            out << "  - " << problem << "\n";
        }
    }
    if (!warnings.empty()) {
        out << "\nCould not be checked (" << warnings.size() << "):\n";
        for (const auto &warning : warnings) {
            out << "  - " << warning << "\n";
        }
    }
    return out.str();
}

AnonymizationVerification verifyAnonymizedExport(const std::string &exportRoot, int trackSampleSize)
{
    AnonymizationVerification result;
    auto fail = [&result](const std::string &what) { result.problems.push_back(what); };
    auto warn = [&result](const std::string &what) { result.warnings.push_back(what); };

    std::error_code ec;
    const fs::path root(exportRoot);
    if (!fs::is_directory(root, ec)) {
        fail(exportRoot + " is not a directory");
        return result;
    }

    // --- Layout: only what the manifest says is in here, is in here. ---
    const fs::path rekordboxRoot = root / "rekordbox";
    const fs::path engineRoot = root / "engine";
    for (const auto &entry : fs::directory_iterator(root, ec)) {
        const std::string name = entry.path().filename().string();
        if (name == "MANIFEST.txt" || name == "rekordbox" || name == "engine" || isHarnessFile(name)) {
            continue;
        }
        fail("unexpected file at the top level of the export: " + name);
    }

    if (fs::is_directory(rekordboxRoot, ec)) {
        for (const auto &entry : fs::directory_iterator(rekordboxRoot, ec)) {
            const std::string name = entry.path().filename().string();
            // The catalog itself, the analysis files, and the player
            // preference files the Device Profile feature needs.
            if (name == "rekordbox" || name == "USBANLZ" || name == "MYSETTING.DAT" || name == "MYSETTING2.DAT"
                || name == "DEVSETTING.DAT" || name == "DJMMYSETTING.DAT") {
                continue;
            }
            fail("unexpected entry in the rekordbox tree: " + name);
        }
        const fs::path catalog = rekordboxRoot / "rekordbox";
        if (fs::is_directory(catalog, ec)) {
            for (const auto &entry : fs::directory_iterator(catalog, ec)) {
                const std::string name = entry.path().filename().string();
                if (name == "export.pdb") {
                    continue;
                }
                // exportLibrary.db is the Device Library Plus database and
                // exportExt.pdb the My Tag vocabulary. Both used to be
                // swept in whole by a blanket directory copy, and the
                // first holds the entire real library behind a key this
                // project's own source derives.
                fail("database that has no anonymizer is present: rekordbox/rekordbox/" + name);
            }
        }
    }

    if (fs::is_directory(engineRoot, ec)) {
        for (const auto &entry : fs::directory_iterator(engineRoot, ec)) {
            const std::string name = entry.path().filename().string();
            if (name == "Database2") {
                continue;
            }
            fail("unexpected entry in the Engine tree: " + name);
        }
    }

    // --- Analysis files: every embedded path, every file, no sampling. ---
    // This is where the leak was, and it was in all 2744 of them.
    if (fs::is_directory(rekordboxRoot / "USBANLZ", ec)) {
        for (const auto &entry : fs::recursive_directory_iterator(rekordboxRoot / "USBANLZ", ec)) {
            if (!entry.is_regular_file() || !isAnalysisFile(entry.path())) {
                continue;
            }
            ++result.analysisFilesChecked;
            try {
                auto file = rekordbox::AnlzFile::readRaw(entry.path().string());
                for (const auto &section : file.sections) {
                    if (section.fourcc != static_cast<uint32_t>(Anlz::SECTION_TAGS_PATH)) {
                        continue;
                    }
                    const std::string path = readAnlzPath(section.rawBytes);
                    if (path.empty()) {
                        continue;
                    }
                    const size_t slash = path.find_last_of('/');
                    const std::string basename = slash == std::string::npos ? path : path.substr(slash + 1);
                    const std::string directory = slash == std::string::npos ? std::string() : path.substr(0, slash);
                    if (directory != "/Contents" && !directory.empty()) {
                        fail("analysis file still names a real directory: "
                             + fs::relative(entry.path(), root, ec).string() + " -> " + path);
                    } else if (!looksLikeFilenamePlaceholder(basename)) {
                        fail("analysis file still holds a real filename: "
                             + fs::relative(entry.path(), root, ec).string() + " -> " + path);
                    }
                }
            } catch (const std::exception &e) {
                fail("could not read " + fs::relative(entry.path(), root, ec).string() + ": " + e.what());
            }
        }
    }

    // --- Track fields, sampled through the app's own readers. ---
    auto checkTracks = [&](const std::vector<domain::Track> &tracks, const std::string &label, int &sampled) {
        int checked = 0;
        for (const auto &track : tracks) {
            if (checked >= trackSampleSize) {
                break;
            }
            ++checked;
            const std::string where = label + " track id=" + track.sourceId;
            if (!looksLikeHashPlaceholder(track.title, "Track")) {
                fail(where + " still has a real title: \"" + track.title + "\"");
            }
            if (!looksLikeIndexedPlaceholder(track.artist, "Artist")) {
                fail(where + " still has a real artist: \"" + track.artist + "\"");
            }
            if (!looksLikeFilenamePlaceholder(track.filename)) {
                fail(where + " still has a real filename: \"" + track.filename + "\"");
            }
            if (!track.filePath.empty()) {
                const size_t slash = track.filePath.find_last_of('/');
                const std::string basename =
                    slash == std::string::npos ? track.filePath : track.filePath.substr(slash + 1);
                if (!looksLikeFilenamePlaceholder(basename)) {
                    fail(where + " still has a real file path: \"" + track.filePath + "\"");
                }
            }
        }
        sampled = checked;
    };

    if (fs::is_directory(rekordboxRoot, ec)) {
        try {
            rekordbox::KaitaiRekordboxReader reader(rekordboxRoot.string());
            auto tracks = application::ScanLibrary(reader).execute();
            checkTracks(tracks, "rekordbox", result.rekordboxTracksSampled);
        } catch (const std::exception &e) {
            warn(std::string("could not read the rekordbox catalog back, so its fields were not sampled: ")
                 + e.what());
        }
    }
    if (fs::is_directory(engineRoot, ec)) {
        try {
            engine::LibdjinteropEngineReader reader(engineRoot.string());
            auto tracks = application::ScanLibrary(reader).execute();
            checkTracks(tracks, "Engine", result.engineTracksSampled);
        } catch (const std::exception &e) {
            warn(std::string("could not read the Engine catalog back, so its fields were not sampled: ")
                 + e.what());
        }
    }

    result.ok = result.problems.empty();
    return result;
}

}  // namespace seabass::infrastructure
