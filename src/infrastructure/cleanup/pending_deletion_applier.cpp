#include "infrastructure/cleanup/pending_deletion_applier.hpp"

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
        if (!fs::exists(entry.filePath, ec)) {
            outcome.status = PendingDeletionOutcome::Status::AlreadyAbsent;
            processed.insert(entry.filePath);
        } else if (fs::remove(entry.filePath, ec)) {
            outcome.status = PendingDeletionOutcome::Status::Deleted;
            processed.insert(entry.filePath);
        } else {
            outcome.status = PendingDeletionOutcome::Status::Failed;
            outcome.failureReason = ec.message();
        }
        outcomes.push_back(std::move(outcome));
    }

    manifest.removeProcessed(processed);
    return outcomes;
}

}  // namespace seabass::infrastructure::cleanup
