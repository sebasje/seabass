#include "infrastructure/local/browsed_backup_root.hpp"

#include <fstream>
#include <string>
#include <system_error>

#include "infrastructure/paths/seabass_paths.hpp"

namespace seabass::infrastructure::local
{

namespace fs = std::filesystem;

namespace
{

// True when `path` is `base` or lies inside it, compared on canonical
// forms so a symlinked home directory or a "..\" spelling cannot fool it
// either way.
bool isUnder(const fs::path &path, const fs::path &base)
{
    std::error_code ec;
    const fs::path p = fs::weakly_canonical(path, ec);
    if (ec) {
        return false;
    }
    const fs::path b = fs::weakly_canonical(base, ec);
    if (ec) {
        return false;
    }
    auto pi = p.begin();
    for (auto bi = b.begin(); bi != b.end(); ++bi, ++pi) {
        if (pi == p.end() || *pi != *bi) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool isBrowsedBackupRoot(const fs::path &libraryRoot)
{
    std::error_code ec;
    if (!fs::exists(libraryRoot / BrowsedBackupMarkerName, ec) || ec) {
        return false;
    }
    return isUnder(libraryRoot, paths::localBrowsedBackupsDir());
}

std::optional<fs::path> browsedBackupArchive(const fs::path &libraryRoot)
{
    if (!isBrowsedBackupRoot(libraryRoot)) {
        return std::nullopt;
    }
    std::string line;
    std::ifstream in(libraryRoot / BrowsedBackupMarkerName);
    std::getline(in, line);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) {
        line.pop_back();
    }
    if (line.empty()) {
        return std::nullopt;
    }
    return fs::path(line);
}

}  // namespace seabass::infrastructure::local
