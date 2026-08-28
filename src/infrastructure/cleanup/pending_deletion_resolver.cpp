#include "infrastructure/cleanup/pending_deletion_resolver.hpp"

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
    return fs::path(path).lexically_normal().generic_string();
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
