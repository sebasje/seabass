#pragma once

#include <set>
#include <string>
#include <vector>

#include "infrastructure/cleanup/pending_deletion_manifest.hpp"

namespace seabass::infrastructure::cleanup
{

struct PendingDeletionOutcome
{
    PendingDeletion entry;
    enum class Status
    {
        Deleted,        // removed from disk this call
        AlreadyAbsent,  // gone already (e.g. removed by hand since) -- still cleared from the manifest
        Failed          // filesystem::remove() failed; left in the manifest for a future retry
    };
    Status status = Status::Failed;
    std::string failureReason;  // only set when status == Failed
};

// The one place in the app that permanently destroys real audio file
// content: for every entry in `safeToDelete` (which the caller must
// already have run through resolvePendingDeletions() against a *fresh*
// scan -- this function does no re-verification of its own, and trusts
// its input completely), deletes the file at entry.filePath from disk
// (or, if it's already gone, treats that as done rather than an error),
// then clears every successfully-processed entry from `manifest` in one
// rewrite. Entries that fail to delete stay in the manifest so a later
// pass can retry them.
std::vector<PendingDeletionOutcome> applyPendingDeletions(const std::vector<PendingDeletion> &safeToDelete,
                                                            PendingDeletionManifest &manifest);

}  // namespace seabass::infrastructure::cleanup
