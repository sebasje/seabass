#pragma once

#include <string>
#include <vector>

#include "domain/track.hpp"

namespace djconvert::domain
{

// Shared low-level helpers used by both intra-library duplicate detection
// (DuplicateTrackFinder) and cross-format track matching (TrackMatcher).

// Lowercases and strips whitespace, so "Song.mp3" and "song.mp3 " compare
// equal.
std::string normalizeFilename(const std::string &filename);

// True if two cue sets are the same, ignoring order and allowing a small
// position tolerance (cross-format conversions can introduce sub-second
// rounding).
bool cueSetsEqual(const std::vector<CuePoint> &a, const std::vector<CuePoint> &b);

}  // namespace djconvert::domain
