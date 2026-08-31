#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <djinterop/djinterop.hpp>

#include "application/use_cases/anonymize_library.hpp"
#include "infrastructure/rekordbox/generated/rekordbox_pdb.h"
#include "infrastructure/rekordbox/little_endian.hpp"
#include "infrastructure/rekordbox/pdb_lookup.hpp"

using namespace seabass::application;
using namespace seabass::infrastructure::rekordbox;
namespace fs = std::filesystem;
using Pdb = rekordbox_pdb_t;

namespace
{

constexpr uint32_t LenPage = 512;

// The smallest possible structurally-real export.pdb with one real
// title string -- this test only exercises AnonymizeLibrary's own
// orchestration (calling both catalog anonymizers, writing the
// manifest, computing sizes), not the anonymization mechanics
// themselves, which rekordbox_library_anonymizer_test.cpp and
// pdb_row_writer_string_test.cpp already cover thoroughly.
std::string buildMinimalRekordboxPdb()
{
    std::string buf(static_cast<size_t>(LenPage) * 2, '\0');
    writeU32LE(buf, 4, LenPage);
    writeU32LE(buf, 8, 1);   // num_tables
    writeU32LE(buf, 20, 5);  // sequence
    writeU32LE(buf, 28, static_cast<uint32_t>(Pdb::PAGE_TYPE_TRACKS));
    writeU32LE(buf, 36, 1);
    writeU32LE(buf, 40, 1);

    size_t page1 = LenPage * 1;
    writeU32LE(buf, page1 + 4, 1);
    writeU32LE(buf, page1 + 8, static_cast<uint32_t>(Pdb::PAGE_TYPE_TRACKS));
    writeU32LE(buf, page1 + 12, 1);
    writeU32LE(buf, page1 + 16, 5);
    buf[page1 + 27] = static_cast<char>(0x24);
    uint32_t packed = 1 | (1 << 13);
    buf[page1 + 24] = static_cast<char>(packed & 0xFF);
    buf[page1 + 25] = static_cast<char>((packed >> 8) & 0xFF);
    buf[page1 + 26] = static_cast<char>((packed >> 16) & 0xFF);
    writeU16LE(buf, page1 + LenPage - 4, 0b1);
    writeU16LE(buf, page1 + LenPage - 6, 0);

    constexpr size_t HeapStart = 40;
    constexpr size_t TrackIdFieldOffset = 72;
    constexpr size_t TrackFixedSize = 136;
    constexpr size_t TrackOfsStringsOffset = 94;
    size_t rowStart = page1 + HeapStart;
    writeU32LE(buf, rowStart + TrackIdFieldOffset, 100);

    // analyze_path (ofs_strings index 14) must point at a real,
    // structurally valid device_sql_string -- AnonymizeLibrary's
    // enumeration pass reads it for every track (needed to locate real
    // tracks' ANLZ files), so an unset (zero) offset would have it try
    // to parse a device_sql_string out of the row's own leading fixed
    // bytes instead, with unpredictable results. Empty is fine here
    // since this test never populates any USBANLZ/ files.
    std::string emptyAnalyzePath(1, static_cast<char>(2));  // short_ascii length_and_kind=2 -> 0 text bytes
    buf.replace(rowStart + TrackFixedSize, emptyAnalyzePath.size(), emptyAnalyzePath);
    writeU16LE(buf, rowStart + TrackOfsStringsOffset + 14 * 2, static_cast<uint16_t>(TrackFixedSize));

    std::string titleText = "Real Title";
    std::string title;
    title.push_back(static_cast<char>(((titleText.size() + 1) << 1) | 1));  // short_ascii length_and_kind
    title += titleText;
    size_t titleOffset = TrackFixedSize + emptyAnalyzePath.size();
    buf.replace(rowStart + titleOffset, title.size(), title);
    writeU16LE(buf, rowStart + TrackOfsStringsOffset + 17 * 2, static_cast<uint16_t>(titleOffset));

    // filename (ofs_strings index 19) must be valid too -- the
    // enumeration pass also reads it now, as the correlation key
    // anonymizationFilenamePlaceholder() uses to keep the same real
    // track's obfuscated filename identical across the independently-
    // run rekordbox and Engine anonymizers (see that function's own
    // comment). Deliberately a realistic length (real DJ library
    // filenames are essentially never under ~10 characters) rather than
    // a short synthetic name: the obfuscated replacement needs to fit
    // within this field's own byte capacity on the rekordbox side
    // (PdbRowWriter::overwriteTrackText() is byte-length-preserving), so
    // an unrealistically short original filename here would truncate the
    // hash-derived placeholder differently than the Engine side (which
    // has no such length constraint) and mask exactly the correlation
    // this test exists to prove.
    std::string filenameText = "086_Real Artist Name-Real Track Title.mp3";
    std::string filename;
    filename.push_back(static_cast<char>(((filenameText.size() + 1) << 1) | 1));
    filename += filenameText;
    size_t filenameOffset = titleOffset + title.size();
    buf.replace(rowStart + filenameOffset, filename.size(), filename);
    writeU16LE(buf, rowStart + TrackOfsStringsOffset + 19 * 2, static_cast<uint16_t>(filenameOffset));

    return buf;
}

void writeFile(const fs::path &path, const std::string &content)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string readFile(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

std::string readRekordboxFilename(const fs::path &pdbPath, uint32_t trackId)
{
    std::ifstream ifs(pdbPath, std::ifstream::binary);
    kaitai::kstream ks(&ifs);
    Pdb pdb(false, &ks);
    std::string result;
    for (const auto &table : *pdb.tables()) {
        if (table->type() != Pdb::PAGE_TYPE_TRACKS) {
            continue;
        }
        forEachDataPage(*table, [&](Pdb::page_t *page) {
            for (const auto &group : *page->row_groups()) {
                for (const auto &row : *group->rows()) {
                    if (!row->present()) {
                        continue;
                    }
                    auto *t = dynamic_cast<Pdb::track_row_t *>(row->body());
                    if (t && t->id() == trackId) {
                        result = sqlText(t->filename());
                    }
                }
            }
        });
    }
    return result;
}

}  // namespace

int main()
{
    fs::path root = fs::temp_directory_path() / "seabass_anonymize_library_test";
    fs::remove_all(root);
    fs::create_directories(root);

    fs::path rekordboxSource = root / "rekordbox_source";
    fs::path engineSource = root / "engine_source" / "Engine Library";
    fs::create_directories(engineSource.parent_path());
    writeFile(rekordboxSource / "rekordbox" / "export.pdb", buildMinimalRekordboxPdb());
    {
        auto db = djinterop::engine::create_database(engineSource.string());
        djinterop::track_snapshot snapshot;
        snapshot.title = "Real Engine Title";
        snapshot.relative_path = "Contents/086_Real Artist Name-Real Track Title.mp3";  // same real filename as the rekordbox fixture
        db.create_track(snapshot);
    }

    // Both catalogs together.
    {
        fs::path outDir = root / "out_both";
        AnonymizeLibrary useCase;
        AnonymizationOptions options;
        options.hardware = "CDJ-3000";
        options.notes = "loop points sometimes drift";

        auto summary = useCase.execute(rekordboxSource.string(), engineSource.string(), outDir.string(), options);

        assert(summary.succeeded());
        assert(summary.rekordboxAttempted && summary.rekordboxError.empty());
        assert(summary.engineAttempted && summary.engineError.empty());
        assert(summary.rekordboxTracksKept == 1);
        assert(summary.engineTracksKept == 1);
        assert(summary.outputSizeBytes > 0);
        assert(summary.estimatedZippedBytes > 0);
        assert(summary.estimatedZippedBytes < summary.outputSizeBytes);  // an estimate, but should shrink, not grow
        assert(fs::exists(outDir / "rekordbox" / "rekordbox" / "export.pdb"));
        assert(fs::exists(outDir / "engine" / "Database2"));
        assert(fs::exists(summary.manifestPath));

        std::string manifest = readFile(summary.manifestPath);
        assert(manifest.find("CDJ-3000") != std::string::npos);
        assert(manifest.find("loop points sometimes drift") != std::string::npos);
        assert(manifest.find("sebas@kde.org") != std::string::npos);
        assert(manifest.find("may be published") != std::string::npos);
        std::cout << "case 1 (both catalogs: succeeds, manifest has hardware/notes/privacy text) OK\n";

        // The one property this whole fixture design exists to prove:
        // the fixture's rekordbox and Engine tracks share the same real
        // filename since they describe the same real stick's same real
        // file -- the two anonymizers ran completely independently, but
        // domain::TrackMatcher's cross-catalog sync matching keys
        // primarily on exactly this field (its own comment: "100% of
        // rekordbox tracks matched their Engine counterpart by path"),
        // so the *obfuscated* filename needs to correlate too (see
        // anonymizationFilenamePlaceholder()'s own comment). A per-run
        // sequential-counter placeholder scheme would make this
        // assertion fail even though nothing about real sync matching
        // itself is broken.
        std::string rekordboxFilename = readRekordboxFilename(outDir / "rekordbox" / "rekordbox" / "export.pdb", 100);
        auto engineDb = djinterop::engine::load_database((outDir / "engine").string());
        auto engineTracks = engineDb.tracks();
        assert(engineTracks.size() == 1);
        std::string engineFilename = engineTracks[0].filename();
        assert(!rekordboxFilename.empty());
        assert(!engineFilename.empty());
        // PdbRowWriter's byte-length-preserving overwrite right-pads
        // rekordbox's copy with trailing spaces to fill the real
        // filename's original byte capacity (documented behavior, see
        // pdb_row_writer_string_test.cpp) -- domain::TrackMatcher's own
        // normalizeFilename() strips all whitespace before comparing, so
        // that padding is exactly as harmless to real sync matching as
        // it is here once stripped the same way.
        auto stripSpaces = [](std::string s) {
            s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
            return s;
        };
        assert(stripSpaces(rekordboxFilename) == stripSpaces(engineFilename));
        std::cout << "case 1b (same real filename -> same obfuscated filename across independently-run anonymizers) OK\n";
    }

    // Only rekordbox -- engine side untouched/unattempted.
    {
        fs::path outDir = root / "out_rekordbox_only";
        AnonymizeLibrary useCase;
        AnonymizationOptions options;
        auto summary = useCase.execute(rekordboxSource.string(), std::nullopt, outDir.string(), options);

        assert(summary.succeeded());
        assert(summary.rekordboxAttempted);
        assert(!summary.engineAttempted);
        assert(!fs::exists(outDir / "engine"));
        std::cout << "case 2 (rekordbox only: engine catalog never attempted or written) OK\n";
    }

    // A real failure in one catalog is reflected in succeeded() and the
    // manifest, without crashing the other catalog's run.
    {
        fs::path outDir = root / "out_bad_rekordbox";
        AnonymizeLibrary useCase;
        AnonymizationOptions options;
        auto summary = useCase.execute((root / "does_not_exist").string(), engineSource.string(), outDir.string(),
                                        options);

        assert(!summary.succeeded());
        assert(!summary.rekordboxError.empty());
        assert(summary.engineError.empty());
        assert(summary.engineTracksKept == 1);  // the working catalog still completed
        std::cout << "case 3 (one catalog failing doesn't stop the other, and succeeded() reflects it) OK\n";
    }

    std::cout << "all cases passed\n";
    return 0;
}
