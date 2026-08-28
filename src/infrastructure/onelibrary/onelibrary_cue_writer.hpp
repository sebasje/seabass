#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "domain/track.hpp"

namespace djconvert::infrastructure::onelibrary
{

// Best-effort writer for cue points into Rekordbox's newer "OneLibrary" /
// "Device Library Plus" format (exportLibrary.db, alongside export.pdb on
// the same stick). See docs/onelibrary-format.md for the reverse-
// engineering this is based on, including the parts that are genuinely
// unverified (the exact meaning of a couple of secondary fields) and
// should be re-checked once real hardware-written example data is
// available.
//
// Deliberately NOT an application::CueWriter: that interface identifies
// a track by its format's own sourceId, but OneLibrary's content_id is a
// *separate* id space from export.pdb's track id (confirmed empirically
// during development -- the same file's export.pdb id and OneLibrary
// content_id can differ). The one identifier both databases actually
// share is the track's file path, so that's what this takes instead.
//
// Deliberately does NOT do its own backup: callers should back up
// exportLibrary.db through the same infrastructure::backup::BackupStore
// (and add the resulting record to whatever backup list drives that
// operation's Undo) that already covers export.pdb/m.db for the same
// operation -- see cleanup_controller.cpp's call site. Keeping every
// backup for one user-facing operation under one BackupStore/manifest is
// what makes Undo actually cover everything touched.
class OneLibraryCueWriter
{
public:
    // pioneerRoot: the stick's "PIONEER" folder (same argument
    // RekordboxCleanupWriter/RekordboxCueWriter take). exportLibrary.db
    // lives at pioneerRoot/rekordbox/exportLibrary.db; content.path
    // values inside it are relative to pioneerRoot's *parent* (the stick
    // root) -- e.g. "/Contents/Artist/Track.mp3".
    explicit OneLibraryCueWriter(std::string pioneerRoot);

    // True if exportLibrary.db exists for this stick -- OneLibrary isn't
    // present on every export (only newer hardware/newer rekordbox
    // exports create it), so callers should skip the write entirely
    // rather than treat a missing file as an error.
    static bool existsFor(const std::string &pioneerRoot);
    static std::string dbPathFor(const std::string &pioneerRoot);

    // Replaces the complete cue set for the track at this (absolute)
    // file path -- same "pass the whole set, not a delta" contract as
    // application::CueWriter::writeHotCues(). Looks the track up by
    // content.path (converted from filePath, relative to the stick
    // root); throws if no matching row is found, if the file changed
    // since this writer was constructed (staleness guard -- see .cpp),
    // or if the post-commit read-back doesn't match what was written.
    // Callers should treat any exception here as failure of a
    // *secondary*, best-effort write -- never roll back a primary
    // export.pdb/m.db write that already succeeded because of it.
    void writeCuesForPath(const std::string &filePath, const std::vector<domain::CuePoint> &cues);

private:
    std::string m_pioneerRoot;
    std::string m_dbPath;
    std::uintmax_t m_originalFileSize = 0;
    std::filesystem::file_time_type m_originalMtime;
};

}  // namespace djconvert::infrastructure::onelibrary
