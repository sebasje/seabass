#pragma once

#include <chrono>
#include <vector>

#include "domain/track.hpp"

namespace djconvert::domain
{

// One rekordbox track paired with the Engine track believed to be the same
// underlying song.
struct SyncMatch
{
    Track rekordboxTrack;
    Track engineTrack;
};

// What to do about one SyncMatch's cues, decided without touching
// anything -- applying a plan is a separate, infrastructure-backed step,
// and is only actually possible for directions we have a writer for
// (Engine today; rekordbox has no write path yet).
struct SyncPlan
{
    enum class Kind {
        // Only one side has cues: propagate them to the other side.
        RekordboxOnly,
        EngineOnly,
        // Both sides have cues, and they differ: resolved by direction
        // (see below) using last-write-wins on the underlying file's
        // mtime, but the resolution is a heuristic, not a certainty --
        // reported clearly as a conflict either way.
        Conflict,
        // Both sides already have the same cues.
        AlreadyConsistent,
        // Neither side has any cues.
        NoCues,
    };
    enum class Direction { None, ToEngine, ToRekordbox };

    Kind kind = Kind::NoCues;
    SyncMatch match;
    Direction direction = Direction::None;
    std::vector<CuePoint> cuesToApply;  // the source side's cues, when direction != None
};

// Matches tracks across a rekordbox scan and an Engine scan by normalized
// filename + duration tolerance (the same heuristic used for intra-library
// duplicate detection, applied across formats instead of within one).
class TrackMatcher
{
public:
    static std::vector<SyncMatch> match(const std::vector<Track> &rekordboxTracks,
                                         const std::vector<Track> &engineTracks);
};

// Decides what should happen to one matched pair's cues. rekordboxMtime/
// engineMtime are the last-modified times of each side's underlying data
// file, used only to break ties on a genuine conflict (both sides have
// different cues) -- documented as a heuristic in the plan, not a true
// edit timestamp.
class SyncPlanner
{
public:
    static SyncPlan plan(const SyncMatch &match, std::chrono::system_clock::time_point rekordboxMtime,
                          std::chrono::system_clock::time_point engineMtime);
};

}  // namespace djconvert::domain
