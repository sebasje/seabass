#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "application/ports/progress_reporter.hpp"

namespace seabass::infrastructure::engine
{

struct EngineAnonymizationResult
{
    int tracksKept = 0;
    int tracksDropped = 0;  // only nonzero when maxTracks was set and exceeded
    int playlistsRenamed = 0;
    std::string errorMessage;  // empty on success
};

// Produces an obfuscated copy of a real Engine Library at
// destinationRoot (created fresh -- refuses if it already exists):
// copies sourceRoot's Database2/ only (not Artwork/, dropped entirely),
// opens the *copy* via djinterop::engine::load_database(), and mutates
// it in place -- title/artist/comment/relative_path on every kept
// track (djinterop::track's own live setters, backed directly by the
// row -- no byte-length constraint the way rekordbox's DeviceSQL format
// has, since this is SQLite), every hot cue's/loop's label (the
// Engine-side equivalent of a rekordbox cue comment -- real free text a
// DJ typed per cue point, not just position/color), and every
// playlist/folder's name (whole tree, via root_playlists()/children()).
//
// Deliberately mutates the real database in place rather than
// rebuilding one via EngineLibraryCreator: that class doesn't carry
// over playlists yet, and the goal here is testing against a library as
// close to what real Engine DJ software actually produces as possible,
// not one this app generated.
//
// If maxTracks is set and the library has more real tracks than that,
// prunes the excess (in db.tracks()'s own order) via
// database::remove_track() -- the same real, tested primitive
// LibdjinteropEngineCleanupWriter already relies on -- after first
// removing each doomed track from every playlist's track list it
// appears in (no survivor to repoint to, unlike that class's
// consolidation use case). Otherwise every real track is kept.
//
// sourceRoot/destinationRoot are both "engineLibraryPath" paths -- the
// directory directly containing Database2/, matching
// LibdjinteropEngineReader's own convention.
EngineAnonymizationResult anonymizeEngineLibrary(
    const std::string &sourceRoot, const std::string &destinationRoot, std::optional<size_t> maxTracks,
    application::ProgressReporter &reporter = application::NullProgressReporter::instance());

}  // namespace seabass::infrastructure::engine
