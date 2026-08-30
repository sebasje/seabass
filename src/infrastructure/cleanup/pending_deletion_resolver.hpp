#pragma once

#include <vector>

#include "domain/track.hpp"
#include "infrastructure/cleanup/pending_deletion_manifest.hpp"

namespace seabass::infrastructure::cleanup
{

struct PendingDeletionResolution
{
    // Entries whose filePath is genuinely not referenced by any track in
    // the fresh scan passed to resolvePendingDeletions() -- safe to
    // actually delete from disk.
    std::vector<PendingDeletion> safeToDelete;

    // Entries whose filePath IS still referenced by some current track,
    // or that have no filePath to check at all -- must NOT be deleted no
    // matter what the manifest says. The manifest is only ever a record
    // of what the DB edit *intended* to orphan; this re-check against a
    // fresh scan is the real safety gate (the manifest could be stale --
    // a race, a manual DB edit since the entry was recorded).
    std::vector<PendingDeletion> stillReferenced;
};

// Decides which of `pending`'s entries are genuinely safe to delete from
// disk, given a *fresh* scan of the same format's current tracks
// (`currentTracks`). Pure decision logic: does no filesystem I/O of its
// own (doesn't check whether the file still exists on disk, doesn't
// delete anything) -- the caller is responsible for acting on the
// result. Path comparison is separator-normalized (so a manifest entry
// recorded with backslashes still matches a scan that reports forward
// slashes, or vice versa) but otherwise exact.
PendingDeletionResolution resolvePendingDeletions(const std::vector<PendingDeletion> &pending,
                                                    const std::vector<domain::Track> &currentTracks);

}  // namespace seabass::infrastructure::cleanup
