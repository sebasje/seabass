#include "application/use_cases/find_unreferenced_files.hpp"

#include <algorithm>
#include <filesystem>
#include <set>

namespace seabass::application
{

namespace
{

namespace fs = std::filesystem;

// Backslash-to-slash explicitly before handing anything to fs::path:
// std::filesystem only treats '\' as a separator on Windows, so relying
// on its native parsing would make this comparison depend on which OS is
// running it. Same reasoning, same first step as
// infrastructure::cleanup::resolvePendingDeletions().
//
// The ASCII lowercase afterwards is the addition, and the header explains
// why it is the safe direction.
std::string normalize(const std::string &path)
{
    if (path.empty()) {
        return path;
    }
    std::string slashed = path;
    std::replace(slashed.begin(), slashed.end(), '\\', '/');
    std::string normalized = fs::path(slashed).lexically_normal().generic_string();
    for (auto &c : normalized) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return normalized;
}

void collect(const std::optional<std::vector<domain::Track>> &catalog, std::set<std::string> &referenced)
{
    if (!catalog) {
        return;
    }
    for (const auto &track : *catalog) {
        if (!track.filePath.empty()) {
            referenced.insert(normalize(track.filePath));
        }
    }
}

}  // namespace

std::vector<std::string> CatalogTracks::present() const
{
    std::vector<std::string> names;
    if (rekordbox) {
        names.push_back("rekordbox");
    }
    if (engine) {
        names.push_back("engine");
    }
    if (oneLibrary) {
        names.push_back("onelibrary");
    }
    return names;
}

UnreferencedFileScan findUnreferencedFiles(const std::vector<AudioFileOnDisk> &filesOnDisk,
                                             const CatalogTracks &catalogs)
{
    UnreferencedFileScan result;
    result.catalogsConsulted = catalogs.present();
    result.audioFilesSeen = filesOnDisk.size();

    if (result.catalogsConsulted.empty()) {
        // No catalog at all: every file would look unreferenced, which
        // would propose deleting the entire stick. Report "unusable" and
        // nothing else.
        return result;
    }
    result.usable = true;

    std::set<std::string> referenced;
    collect(catalogs.rekordbox, referenced);
    collect(catalogs.engine, referenced);
    collect(catalogs.oneLibrary, referenced);
    result.referencedPathsSeen = referenced.size();

    for (const auto &file : filesOnDisk) {
        if (file.filePath.empty()) {
            continue;
        }
        if (!referenced.contains(normalize(file.filePath))) {
            result.unreferenced.push_back(file);
        }
    }
    return result;
}

}  // namespace seabass::application
