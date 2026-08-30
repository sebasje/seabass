#pragma once

#include <vector>

#include "domain/track.hpp"

namespace seabass::domain
{

// One memory cue sitting at the very first frame of a track (positionMs
// == 0) -- in practice almost always an accidental/junk cue rather than
// a deliberate marker: unlike a hot cue (which some DJs do use at 0:00
// on purpose as an explicit "track start" pad), a memory cue there
// serves no real navigational purpose (the track already starts at
// 0:00) and most often comes from a stray click during analysis or an
// import artifact.
struct JunkCueIssue
{
    Track track;    // the track carrying the cue
    CuePoint cue;   // the specific memory cue at position 0
};

// Pure, no filesystem/database access -- callers already have a fresh
// track list from the same scan that also feeds
// LibraryConsistencyChecker, this just looks at cues directly rather
// than file existence.
class JunkCueFinder
{
public:
    static std::vector<JunkCueIssue> find(const std::vector<Track> &tracks);
};

}  // namespace seabass::domain
