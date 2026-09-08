#pragma once

#include <vector>

#include "application/use_cases/find_unreferenced_files.hpp"
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
// disk, given a *fresh* read of EVERY catalog on the stick.
//
// Every catalog, not the one whose cleanup created the entry. A stick
// routinely carries the same audio file in rekordbox, Engine and
// OneLibrary at once; cleaning up in one of them and then checking only
// that one would delete a file the other two still play. The entry's own
// `format` field records which cleanup orphaned it and is deliberately
// NOT used here -- it says who created the entry, never who still needs
// the file.
//
// Taking application::CatalogTracks rather than one merged
// `vector<Track>` is the point: a merged parameter makes "I only passed
// one catalog" invisible at the call site, and this is the function
// standing between a stale manifest and permanently destroyed audio.
// See its own doc comment.
//
// Pure decision logic: does no filesystem I/O of its own (doesn't check
// whether the file still exists on disk, doesn't delete anything) -- the
// caller is responsible for acting on the result. Path comparison is
// separator- and case-normalized, matching findUnreferencedFiles(); see
// there for why case matters on exFAT and NTFS.
//
// With no catalog supplied at all, EVERY entry comes back as
// stillReferenced. "I could not read any catalog" and "nothing
// references these files" must never be the same answer when the next
// step is deletion.
PendingDeletionResolution resolvePendingDeletions(const std::vector<PendingDeletion> &pending,
                                                    const application::CatalogTracks &catalogs);

}  // namespace seabass::infrastructure::cleanup
