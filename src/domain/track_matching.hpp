#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "domain/track.hpp"

namespace djconvert::domain
{

// Shared low-level helpers used by both intra-library duplicate detection
// (DuplicateTrackFinder) and cross-format track matching (TrackMatcher,
// and the local-machine cue restore path).

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

// Pairs up tracks from two lists believed to be the same underlying song:
// title+artist is the primary signal (see titleArtistKey), falling back
// to filename when either side is missing title/artist metadata, then
// requiring duration to match within a small tolerance either way. At
// most one match per `a` track, since callers use this to propagate a
// single track's cues, not to build a full many-to-many mapping.
//
// Returned pointers stay valid exactly as long as `a` and `b` do --
// callers matching across formats (TrackMatcher) or against a local
// cue backup (the restore path) both consume the result immediately,
// well within that scope.
std::vector<std::pair<const Track *, const Track *>> matchTracks(const std::vector<Track> &a,
                                                                   const std::vector<Track> &b);

// True if two cue sets are the same, ignoring order and allowing a small
// position tolerance (cross-format conversions can introduce sub-second
// rounding).
bool cueSetsEqual(const std::vector<CuePoint> &a, const std::vector<CuePoint> &b);

}  // namespace djconvert::domain
