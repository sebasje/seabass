#pragma once

#include <optional>
#include <string>
#include <vector>

#include "domain/duplicate_cue_consolidation.hpp"
#include "domain/track.hpp"

namespace seabass::domain
{

// What to do about one DuplicateGroup for the "Clean Up" feature: which
// copy to keep, which to remove, and what data the survivor should end
// up with so nothing already on either copy is lost.
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

    // "Fill a gap" propagation, not a merge: if the survivor is missing
    // bpm/key/artwork and some other copy in the group has it, that
    // value carries forward. Unlike cues (which can legitimately exist
    // on multiple copies and all need keeping), a track has exactly one
    // bpm/key/artwork -- so this is only ever "use the one value that
    // exists" not "reconcile several". Each optional is set only when
    // the survivor itself lacks the field AND some other copy has it;
    // the matching *DonorSourceId names which track it came from (empty
    // if nothing was propagated for that field), since some writers
    // (rekordbox's PdbRowWriter, OneLibraryCueWriter) copy the donor's
    // own already-valid field reference directly rather than
    // re-deriving one from the parsed value alone -- see
    // pdb_row_writer.hpp's copyTrackFieldsIfMissing() for why.
    std::optional<double> bpmForSurvivor;
    std::string bpmDonorSourceId;
    std::optional<std::string> keyForSurvivor;
    std::string keyDonorSourceId;
    std::optional<std::string> artworkPathForSurvivor;
    std::string artworkDonorSourceId;

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

    // Distinct from `differs` above: true when two or more copies in
    // the group carry a genuinely different rating, comment, play
    // count, or last-played timestamp -- none of which this planner (or
    // any writer in this codebase) propagates onto the survivor, unlike
    // bpm/key/artwork above. Removing the other copies would silently
    // and permanently lose whichever of those values didn't happen to
    // land on the survivor. A group where only the survivor has a value
    // (nothing to lose) or every copy already agrees is NOT flagged --
    // this is specifically "real, differing, unpreservable data is
    // about to be discarded", not "some copy has more metadata than
    // another". Callers should default a group like this to *excluded*
    // too, same as `differs`, but with different UI text: this is about
    // per-copy DJ data (usage/opinion), not encode quality.
    bool hasUnpreservableDataAtRisk = false;
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

}  // namespace seabass::domain
