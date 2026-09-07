#pragma once

#include <QString>

#include <vector>

#include "domain/library_consistency.hpp"
#include "domain/track.hpp"
#include <optional>

#include "infrastructure/engine/libdjinterop_engine_cue_writer.hpp"
#include "infrastructure/onelibrary/onelibrary_cue_writer.hpp"
#include "infrastructure/rekordbox/anlz_path_index.hpp"

namespace seabass::gui
{

class SaveContext;

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

// The save's one analysis-path index for this catalog, built by whichever
// change asks first and shared by the rest.
//
// Without it a rekordbox save parses the whole 1.4 MB export.pdb twice per
// item -- once to find the file to back up, once inside the cue writer --
// so a 200-item save parsed it 400 times. This makes it once.
//
// Returns nullptr when the database cannot be read at all, which leaves
// every caller on the old per-call lookup rather than failing the save.
const infrastructure::rekordbox::AnlzPathIndex *sharedAnlzPathIndex(SaveContext &ctx, const QString &pioneerRoot);

// The save's one OneLibrary writer for this database, built by whichever
// change asks first and shared by the rest.
//
// Nine call sites used to construct one per item. Each construction is
// cheap, but the first write through it opens two SQLCipher connections,
// and each open derives the key from a passphrase: 115 ms of CPU that a
// scratch copy or a faster disk does nothing for. That derivation is the
// single largest per-item cost in a save against a real stick. One writer
// per save pays it once.
//
// realStickRoot matters when the database being written is a scratch copy:
// its parent is a temp directory, not the stick, and every content-path
// lookup resolves against the real stick's layout. Pass it explicitly
// there.
infrastructure::onelibrary::OneLibraryCueWriter &sharedOneLibraryWriter(
    SaveContext &ctx, const std::string &pioneerRoot,
    const std::optional<std::string> &realStickRoot = std::nullopt);

// The save's one Engine cue writer for this library, for the call sites
// that would otherwise build one per item. Opening an Engine library is a
// full SQLite open plus schema detection, about 151 ms against a stick.
infrastructure::engine::LibdjinteropEngineCueWriter &sharedEngineCueWriter(SaveContext &ctx,
                                                                           const std::string &engineLibraryPath);

}  // namespace seabass::gui
