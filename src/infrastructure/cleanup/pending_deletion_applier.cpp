#include "infrastructure/cleanup/pending_deletion_applier.hpp"

#include "infrastructure/long_paths.hpp"

#include <filesystem>
#include <system_error>

namespace seabass::infrastructure::cleanup
{

namespace fs = std::filesystem;

std::vector<PendingDeletionOutcome> applyPendingDeletions(const std::vector<PendingDeletion> &safeToDelete,
                                                            PendingDeletionManifest &manifest)
{
    std::vector<PendingDeletionOutcome> outcomes;
    std::set<std::string> processed;

    for (const auto &entry : safeToDelete) {
        PendingDeletionOutcome outcome;
        outcome.entry = entry;

        std::error_code ec;
        // Prefixed: a track under a long artist/album path can sit past
        // MAX_PATH, and there the unprefixed calls answer "not there" and
        // "could not remove" about a file that is present and removable.
        const fs::path path = longPathSafe(entry.filePath);
        if (!fs::exists(path, ec)) {
            outcome.status = PendingDeletionOutcome::Status::AlreadyAbsent;
            processed.insert(entry.filePath);
        } else if (fs::remove(path, ec)) {
            outcome.status = PendingDeletionOutcome::Status::Deleted;
            processed.insert(entry.filePath);
        } else {
            outcome.status = PendingDeletionOutcome::Status::Failed;
            // fs::remove returns false both for a real failure and for
            // "there was nothing to remove", and only the first sets ec.
            // Passing ec.message() through regardless put the string
            // "The operation completed successfully" in front of the user
            // as the reason their file could not be deleted.
            outcome.failureReason = ec ? ec.message() : "the file could not be removed";
        }
        outcomes.push_back(std::move(outcome));
    }

    manifest.removeProcessed(processed);
    return outcomes;
}

}  // namespace seabass::infrastructure::cleanup
