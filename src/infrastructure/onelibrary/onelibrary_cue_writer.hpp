#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "domain/track.hpp"

namespace seabass::infrastructure::onelibrary
{

// Best-effort writer for Rekordbox's newer "OneLibrary" / "Device Library
// Plus" format (exportLibrary.db, alongside export.pdb on the same
// stick), cue points (writeCuesForPath) and, for cleanup, whole track
// rows (removeTrackByPath). See docs/onelibrary-format.md for the
// reverse-engineering this is based on, including the parts that are
// genuinely unverified (the exact meaning of a couple of secondary
// fields) and should be re-checked once real hardware-written example
// data is available.
//
// Deliberately NOT an application::CueWriter: that interface identifies
// a track by its format's own sourceId, but OneLibrary's content_id is a
// *separate* id space from export.pdb's track id (confirmed empirically
// during development, the same file's export.pdb id and OneLibrary
// content_id can differ). The one identifier both databases actually
// share is the track's file path, so that's what this takes instead.
//
// Deliberately does NOT do its own backup: callers should back up
// exportLibrary.db through the same infrastructure::backup::BackupStore
// (and add the resulting record to whatever backup list drives that
// operation's Undo) that already covers export.pdb/m.db for the same
// operation, see cleanup_controller.cpp's call site. Keeping every
// backup for one user-facing operation under one BackupStore/manifest is
// what makes Undo actually cover everything touched.
class OneLibraryCueWriter
{
public:
    // pioneerRoot: where to find/open exportLibrary.db
    // (pioneerRoot/rekordbox/exportLibrary.db) -- normally the stick's
    // "PIONEER" folder (same argument RekordboxCleanupWriter/
    // RekordboxCueWriter take), but callers may point this at a
    // relocated copy of the database instead (see realStickRoot below).
    //
    // realStickRoot: the stick's actual root directory, used to convert
    // each track's absolute file path into the stick-root-relative form
    // content.path values use, e.g. "/Contents/Artist/Track.mp3".
    // Defaults to pioneerRoot's own parent directory, which is correct
    // whenever pioneerRoot really is the stick's PIONEER folder. Only
    // needs to be passed explicitly when pioneerRoot points somewhere
    // else -- e.g. a fast local scratch copy of exportLibrary.db built to
    // avoid many slow round trips against real removable media -- where
    // pioneerRoot's own parent is no longer the stick at all, but the
    // file paths being looked up still are.
    explicit OneLibraryCueWriter(std::string pioneerRoot, std::optional<std::string> realStickRoot = std::nullopt);

    // True if exportLibrary.db exists for this stick. OneLibrary isn't
    // present on every export (only newer hardware/newer rekordbox
    // exports create it), so callers should skip the write entirely
    // rather than treat a missing file as an error.
    static bool existsFor(const std::string &pioneerRoot);
    static std::string dbPathFor(const std::string &pioneerRoot);

    // Replaces the complete cue set for the track at this (absolute)
    // file path, same "pass the whole set, not a delta" contract as
    // application::CueWriter::writeHotCues(). Looks the track up by
    // content.path (converted from filePath, relative to the stick
    // root); throws if no matching row is found, if the file changed
    // since this writer was constructed (staleness guard, see .cpp),
    // or if the post-commit read-back doesn't match what was written.
    // Callers should treat any exception here as failure of a
    // *secondary*, best-effort write, never roll back a primary
    // export.pdb/m.db write that already succeeded because of it.
    void writeCuesForPath(const std::string &filePath, const std::vector<domain::CuePoint> &cues);

    // Removes a track's row entirely, along with its dependent cue/
    // playlist-membership rows, for cleanup call sites that just
    // deleted this file (and its row) from the primary catalog
    // (rekordbox/Engine) and need to remove the now-orphaned OneLibrary
    // row too, rather than leave it pointing at a file that no longer
    // exists. No FK/cascade enforcement exists in this schema (see
    // docs/onelibrary-format.md), so dependents are deleted explicitly,
    // in dependency order, inside one transaction. Same contract as
    // writeCuesForPath(): looks the track up by content.path (converted
    // from filePath), throws if no matching row is found, if the file
    // changed since this writer was constructed (staleness guard), or if
    // the post-commit read-back doesn't show the row actually gone.
    // Same "secondary, best-effort write" caution applies to callers.
    void removeTrackByPath(const std::string &filePath);

private:
    std::string m_pioneerRoot;
    std::string m_stickRoot;
    std::string m_dbPath;
    std::uintmax_t m_originalFileSize = 0;
    std::filesystem::file_time_type m_originalMtime;
};

}  // namespace seabass::infrastructure::onelibrary
