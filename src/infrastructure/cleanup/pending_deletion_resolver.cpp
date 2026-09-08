#include "infrastructure/cleanup/pending_deletion_resolver.hpp"

#include <algorithm>
#include <filesystem>
#include <set>

namespace seabass::infrastructure::cleanup
{

namespace
{

namespace fs = std::filesystem;

std::string normalize(const std::string &path)
{
    if (path.empty()) {
        return path;
    }
    // Backslash-to-slash first, explicitly -- std::filesystem::path only
    // treats '\' as a separator on Windows, so relying on its native
    // parsing here would make this function's behavior (and this
    // manifest-vs-scan safety check) depend on which OS it's running on.
    std::string slashed = path;
    std::replace(slashed.begin(), slashed.end(), '\\', '/');
    std::string normalized = fs::path(slashed).lexically_normal().generic_string();
    // ASCII-lowercased for the same reason findUnreferencedFiles() does
    // it: exFAT and NTFS are case-insensitive, so two spellings that
    // differ only in case name one physical file, and comparing them
    // case-sensitively would call a referenced file safe to delete.
    // Normalizing can only ever move an entry toward "still referenced".
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

PendingDeletionResolution resolvePendingDeletions(const std::vector<PendingDeletion> &pending,
                                                    const application::CatalogTracks &catalogs)
{
    PendingDeletionResolution result;

    if (catalogs.present().empty()) {
        // Nothing was read, so nothing can be shown to be unreferenced.
        // Protect every entry rather than clearing the stick.
        result.stillReferenced = pending;
        return result;
    }

    std::set<std::string> referenced;
    collect(catalogs.rekordbox, referenced);
    collect(catalogs.engine, referenced);
    collect(catalogs.oneLibrary, referenced);

    for (const auto &entry : pending) {
        // No resolved path to check at all -- never guess, leave it alone.
        if (entry.filePath.empty() || referenced.contains(normalize(entry.filePath))) {
            result.stillReferenced.push_back(entry);
        } else {
            result.safeToDelete.push_back(entry);
        }
    }
    return result;
}

}  // namespace seabass::infrastructure::cleanup
