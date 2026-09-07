#pragma once

#include <QString>

#include <vector>

#include "domain/library_consistency.hpp"
#include "domain/track.hpp"

namespace seabass::gui
{

// The few naming helpers a change class and its controller both need.
// Everything else a change needs lives in that change's own source file.
//
// The keys matter beyond cosmetics: staging is keyed on PendingChange::id(),
// so a key that is not stable across a re-scan would stage the same repair
// twice, and the controller derives the same key when it asks whether an
// issue is already staged. One definition, used by both, keeps those two
// answers from drifting apart.

// Which catalog an issue came from. Every reader sets Track::format, and a
// consistency check only ever runs against one catalog, so the survivor or
// the first broken row name it identically.
QString issueFormat(const domain::LibraryConsistencyIssue &issue);

// Identifies an issue across re-scans: its format plus every row id it
// covers, survivor first.
QString issueKeyFor(const domain::LibraryConsistencyIssue &issue);

// Identifies one track's stray-cue issue: format plus row id.
QString junkKeyFor(const domain::Track &track);

// "3 hot, 1 memory (...)" -- the human summary of a cue set. Mirrors the
// command line's own wording so both report a sync identically.
QString describeCues(const std::vector<domain::CuePoint> &cues);

}  // namespace seabass::gui
