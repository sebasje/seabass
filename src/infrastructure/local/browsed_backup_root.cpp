#include "infrastructure/local/browsed_backup_root.hpp"

#include <fstream>
#include <string>
#include <system_error>

namespace seabass::infrastructure::local
{

namespace fs = std::filesystem;

namespace
{

std::string trimmedLine(std::string line)
{
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) {
        line.pop_back();
    }
    return line;
}

struct Marker
{
    fs::path archive;
    fs::path cacheRoot;
};

// The marker's two lines, or nullopt when it is absent, unreadable, or
// (an older single-line form, or a copy) does not name this directory.
std::optional<Marker> validMarkerFor(const fs::path &libraryRoot)
{
    std::ifstream in(libraryRoot / BrowsedBackupMarkerName);
    if (!in) {
        return std::nullopt;
    }
    std::string archiveLine;
    std::string rootLine;
    std::getline(in, archiveLine);
    std::getline(in, rootLine);
    archiveLine = trimmedLine(std::move(archiveLine));
    rootLine = trimmedLine(std::move(rootLine));
    if (archiveLine.empty() || rootLine.empty()) {
        return std::nullopt;
    }
    std::error_code ec;
    const fs::path here = fs::weakly_canonical(libraryRoot, ec);
    if (ec || here != fs::path(rootLine)) {
        return std::nullopt;
    }
    return Marker{fs::path(archiveLine), here};
}

}  // namespace

bool isBrowsedBackupRoot(const fs::path &libraryRoot)
{
    return validMarkerFor(libraryRoot).has_value();
}

std::optional<fs::path> browsedBackupArchive(const fs::path &libraryRoot)
{
    auto marker = validMarkerFor(libraryRoot);
    if (!marker) {
        return std::nullopt;
    }
    return marker->archive;
}

bool writeBrowsedBackupMarker(const fs::path &markerDir, const fs::path &archivePath, const fs::path &cacheRoot)
{
    std::error_code ec;
    // weakly_canonical resolves what exists and keeps the rest, so the
    // final cache directory need not exist yet when this is written.
    const fs::path root = fs::weakly_canonical(cacheRoot, ec);
    if (ec) {
        return false;
    }
    std::ofstream marker(markerDir / BrowsedBackupMarkerName, std::ios::trunc);
    marker << fs::absolute(archivePath).string() << "\n" << root.string() << "\n";
    return static_cast<bool>(marker);
}

}  // namespace seabass::infrastructure::local
