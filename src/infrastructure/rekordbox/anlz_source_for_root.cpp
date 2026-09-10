#include "infrastructure/rekordbox/anlz_source_for_root.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>

#include "infrastructure/stick_backup/archive_anlz_source.hpp"
#include "infrastructure/stick_backup/posix_archive_file.hpp"
#include "infrastructure/stick_backup/zip64_reader.hpp"

namespace seabass::infrastructure::rekordbox
{

namespace fs = std::filesystem;

namespace
{

// Trailing whitespace/newline tolerated: the marker is a text file and
// something may well have re-saved it.
std::string trimmed(std::string text)
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}

// One open archive per backup being browsed, shared by every reader that
// asks for it. Opening means reading and validating the whole central
// directory -- tens of thousands of entries for a real library -- and
// the waveform reader asks on the UI thread, per Play and per list row
// scrolled into view. Without this, every one of those reopened the
// ZIP and threw it away.
//
// Keyed by the archive's path, size and mtime together: a backup that
// has been rewritten since (a newer generation, a different stick) gets
// a fresh open rather than the stale index.
struct OpenedArchive
{
    std::uintmax_t size = 0;
    fs::file_time_type mtime;
    std::shared_ptr<AnlzByteSource> source;
};

std::mutex g_openedMutex;
std::map<std::string, OpenedArchive> g_opened;

std::shared_ptr<AnlzByteSource> openArchiveSource(const std::string &archivePath)
{
    std::error_code ec;
    const auto size = fs::file_size(fs::path(archivePath), ec);
    if (ec) {
        return nullptr;
    }
    const auto mtime = fs::last_write_time(fs::path(archivePath), ec);
    if (ec) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(g_openedMutex);
    auto it = g_opened.find(archivePath);
    if (it != g_opened.end() && it->second.size == size && it->second.mtime == mtime) {
        return it->second.source;
    }
    try {
        // The ArchiveFile is kept alive by the source itself, through
        // the Zip64Reader's shared_ptr, for as long as any reader holds
        // it -- so a scan can keep pulling entries after this returns.
        auto file = std::make_shared<stick_backup::PosixArchiveFile>(
            fs::path(archivePath), stick_backup::PosixArchiveFile::OpenMode::ReadOnly);
        auto reader = std::make_shared<const stick_backup::Zip64Reader>(stick_backup::Zip64Reader::open(*file));
        auto source = std::make_shared<stick_backup::ArchiveAnlzSource>(std::move(reader), "PIONEER/", std::move(file));
        g_opened[archivePath] = OpenedArchive{size, mtime, source};
        return source;
    } catch (const std::exception &) {
        // A truncated or replaced archive: the caller browses the
        // extracted catalogs without cues rather than failing the scan.
        g_opened.erase(archivePath);
        return nullptr;
    }
}

}  // namespace

std::shared_ptr<AnlzByteSource> anlzSourceForPioneerRoot(const std::string &pioneerRoot)
{
    std::error_code ec;
    const fs::path markerPath = fs::path(pioneerRoot).parent_path() / BackupSourceMarkerName;
    if (!fs::exists(markerPath, ec) || ec) {
        return std::make_shared<FilesystemAnlzSource>(pioneerRoot);
    }

    std::string archivePath;
    {
        std::ifstream in(markerPath);
        std::getline(in, archivePath);
        archivePath = trimmed(std::move(archivePath));
    }
    if (archivePath.empty()) {
        return std::make_shared<FilesystemAnlzSource>(pioneerRoot);
    }
    if (auto source = openArchiveSource(archivePath)) {
        return source;
    }
    return std::make_shared<FilesystemAnlzSource>(pioneerRoot);
}

}  // namespace seabass::infrastructure::rekordbox
