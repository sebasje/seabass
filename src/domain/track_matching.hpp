#pragma once

#include <optional>
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

// Tracks under different filenames are still the same song if title+artist
// match exactly -- filenames can differ across formats/re-imports (e.g. a
// filename that embeds a playlist index), so this is the primary signal
// for matching the same underlying song across libraries. Empty
// title/artist is too weak to match on, so those return nullopt rather
// than colliding on an empty key.
std::optional<std::string> titleArtistKey(const Track &track);

// True if two cue sets are the same, ignoring order and allowing a small
// position tolerance (cross-format conversions can introduce sub-second
// rounding).
bool cueSetsEqual(const std::vector<CuePoint> &a, const std::vector<CuePoint> &b);

}  // namespace djconvert::domain
