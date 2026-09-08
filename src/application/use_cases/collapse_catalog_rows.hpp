#pragma once

#include <vector>

#include "domain/track.hpp"

namespace seabass::application
{

// Turns catalog rows into files: one Track per distinct file path, with
// every catalog's row for it recorded in Track::catalogRows.
//
// This is the difference between "three catalogs listing one file" and
// "three copies of a track", and nothing downstream can tell them apart
// without it. DuplicateTrackFinder matches on artist, title and length
// and has no notion of paths, so handed the raw rows of a real stick it
// groups a file with itself: measured on one, 4369 rows point at 1564
// files, and grouping them uncollapsed put a row for the survivor's own
// file into the removal list of 1156 groups -- 1465 catalog rows that
// would have been deleted for files being kept.
//
// What the collapsed Track carries, and why:
//
// - The FIRST row for a file, in the order given, is the base. Callers
//   pass their catalogs in a fixed order, so the result is stable.
// - Every empty field is then filled from the other rows, first
//   non-empty wins: bitrate, duration, bpm, key, artwork, rating,
//   comment, play count, last played. This is not cosmetic. Engine
//   stores no bitrate at all, so before the fill a file catalogued there
//   scored as 0 kbps against its own uncatalogued copy and lost the
//   survivor comparison on every one of a real stick's 435 groups.
// - Cues are the union across rows (LocalRestorePlanner::mergeCues),
//   the same rule the cleanup planner uses to merge across copies: a
//   cue that exists in only one catalog is still a cue on that file.
//
// Filling a gap never loses anything, because collapsing removes no row
// from any catalog -- it only stops one file from looking like several.
// Where two catalogs disagree on a rating or a comment for the SAME
// file, the first is kept for comparison purposes and the disagreement
// stays exactly where it was, in the catalogs; reconciling it is
// synchronization's job, not deduplication's.
//
// A track with no resolvable path passes through untouched and alone:
// with nothing to compare, "same file" cannot be established, and
// guessing would merge two real files. Same for a streaming track, whose
// path names a cache on some other machine -- callers filter those out
// before this, and this refuses to fold them together regardless.
std::vector<domain::Track> collapseCatalogRows(const std::vector<domain::Track> &rows);

}  // namespace seabass::application
