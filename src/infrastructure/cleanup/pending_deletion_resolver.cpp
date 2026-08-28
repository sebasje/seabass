#include "infrastructure/cleanup/pending_deletion_resolver.hpp"

#include <algorithm>
#include <filesystem>
#include <set>

namespace djconvert::infrastructure::cleanup
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
    return fs::path(slashed).lexically_normal().generic_string();
}

}  // namespace

PendingDeletionResolution resolvePendingDeletions(const std::vector<PendingDeletion> &pending,
                                                    const std::vector<domain::Track> &currentTracks)
{
    std::set<std::string> referenced;
    for (const auto &track : currentTracks) {
        if (!track.filePath.empty()) {
            referenced.insert(normalize(track.filePath));
        }
    }

    PendingDeletionResolution result;
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

}  // namespace djconvert::infrastructure::cleanup
