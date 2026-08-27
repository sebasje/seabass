#pragma once

#include <utility>
#include <vector>

#include "domain/track.hpp"

namespace djconvert::domain
{

// One stick track with no cues, paired with a local-backup copy that has
// them -- the candidate for the "restore cues from this computer" action.
struct RestoreCandidate
{
    Track stickTrack;
    Track localTrack;
};

// Decides which title+artist/filename matches between a stick's tracks
// and a local cue backup are worth restoring. Restoring only ever writes
// to the stick (never back to the local backup), so unlike SyncPlanner
// there is no mtime-based conflict resolution to do: a candidate is
// proposed only where the stick track currently has zero cues (the
// scenario this feature exists for -- cues lost or overwritten) and the
// matched local track has some to offer. A stick track that already has
// cues is left alone, even if the local backup's cues differ.
class LocalRestorePlanner
{
public:
    static std::vector<RestoreCandidate> plan(
        const std::vector<std::pair<const Track *, const Track *>> &matches);
};

}  // namespace djconvert::domain
