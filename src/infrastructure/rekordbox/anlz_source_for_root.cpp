#include "infrastructure/rekordbox/anlz_source_for_root.hpp"

#include <filesystem>
#include <fstream>
#include <memory>

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
    if (archivePath.empty() || !fs::exists(fs::path(archivePath), ec) || ec) {
        return std::make_shared<FilesystemAnlzSource>(pioneerRoot);
    }

    try {
        // The ArchiveFile is kept alive by the source itself, through the
        // Zip64Reader's shared_ptr, for as long as the reader that holds
        // it -- so a scan can keep pulling entries after this returns.
        auto file = std::make_shared<stick_backup::PosixArchiveFile>(
            fs::path(archivePath), stick_backup::PosixArchiveFile::OpenMode::ReadOnly);
        auto reader = std::make_shared<const stick_backup::Zip64Reader>(stick_backup::Zip64Reader::open(*file));
        return std::make_shared<stick_backup::ArchiveAnlzSource>(std::move(reader), "PIONEER/", std::move(file));
    } catch (const std::exception &) {
        // A truncated or replaced archive: browse the extracted catalogs
        // without cues rather than failing the scan outright.
        return std::make_shared<FilesystemAnlzSource>(pioneerRoot);
    }
}

}  // namespace seabass::infrastructure::rekordbox
