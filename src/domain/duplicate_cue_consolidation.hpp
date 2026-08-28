#pragma once

#include <optional>
#include <string>
#include <vector>

#include "domain/track.hpp"

namespace djconvert::domain
{

// Two or more tracks in the same library scan that are believed to be
// copies of the same underlying song (same filename, matching duration).
struct DuplicateGroup
{
    std::vector<Track> tracks;
};

// What to do about one DuplicateGroup's cues, decided without touching
// anything -- applying the plan is a separate, infrastructure-backed step.
struct ConsolidationPlan
{
    enum class Kind {
        // Exactly one copy has cues, the rest have none: propagate source's
        // cues onto every track in targets.
        Unambiguous,
        // More than one copy has cues, and they differ: a human has to
        // decide, we never guess.
        Conflict,
        // Every copy that has cues already has the same ones.
        AlreadyConsistent,
        // No copy in the group has any cues at all.
        NoCues,
    };

    Kind kind = Kind::NoCues;
    DuplicateGroup group;
    std::optional<Track> source;  // set only for Kind::Unambiguous
    std::vector<Track> targets;   // set only for Kind::Unambiguous
};

// Groups tracks that look like duplicates of each other (matched by
// normalized filename + duration tolerance, or title+artist + duration
// tolerance). Format-agnostic -- has no notion of which catalog a Track
// came from, so callers can pass a single library's tracks (intra-library
// duplicates) or a concatenation of both rekordbox's and Engine's tracks
// (cross-library duplicates, used by the Clean Up feature) with no
// difference in behavior.
class DuplicateTrackFinder
{
public:
    static std::vector<DuplicateGroup> find(const std::vector<Track> &tracks);
};

// Decides, for a single DuplicateGroup, whether its cues can be
// consolidated unambiguously.
class DuplicateCueConsolidator
{
public:
    static ConsolidationPlan plan(const DuplicateGroup &group);
};

}  // namespace djconvert::domain
