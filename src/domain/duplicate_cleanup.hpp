#pragma once

#include <vector>

#include "domain/duplicate_cue_consolidation.hpp"
#include "domain/track.hpp"

namespace djconvert::domain
{

// What to do about one DuplicateGroup for the "Clean Up" feature: which
// copy to keep, which to remove, and what cue data the survivor should
// end up with so nothing already on either copy is lost.
struct DuplicateCleanupPlan
{
    DuplicateGroup group;
    Track survivor;               // the copy that would be kept
    std::vector<Track> toRemove;  // every other copy in the group

    // Union of every copy's cues (via LocalRestorePlanner::mergeCues,
    // folded across the whole group), deduplicated -- what the survivor
    // should be written with so cleanup never loses a hot/memory cue
    // that only existed on a copy about to be removed.
    std::vector<CuePoint> mergedCuesForSurvivor;

    // True when picking by quality (bitrate) and picking by length
    // (duration) disagree on which copy is "best" -- e.g. the
    // highest-bitrate copy is also meaningfully shorter than another
    // copy. This can be entirely intentional (a DJ keeping a
    // lower-quality, shorter edit for a specific piece of hardware), so
    // callers should default a group like this to *excluded* from a
    // bulk cleanup and let the caller build a UI-facing explanation from
    // group.tracks' own bitrate/durationSeconds/fileSizeBytes rather
    // than have the domain layer format human text.
    bool differs = false;
};

// Decides survivor/removal/cue-merge for one DuplicateGroup. Only
// meaningful for groups with 2+ tracks -- DuplicateTrackFinder::find()
// never produces fewer, but a 1-track (or empty) group is handled
// harmlessly (survivor is that track, nothing to remove).
class DuplicateCleanupPlanner
{
public:
    static DuplicateCleanupPlan plan(const DuplicateGroup &group);
};

}  // namespace djconvert::domain
