#pragma once

#include <string>

#include "application/ports/library_reader.hpp"
#include "domain/track.hpp"

struct sqlite3;

namespace djconvert::infrastructure::local
{

// A stick-independent copy of cue data, kept on this computer so cues
// survive a stick being reformatted or a track being re-imported. Backed
// by a small SQLite database (schema: tracks + cues tables) -- SQLite was
// chosen over vendoring a JSON library because libdjinterop already
// requires it to build (see third_party/libdjinterop/CMakeLists.txt), so
// this adds no new third-party dependency.
//
// Implements LibraryReader so it can be matched against a stick's tracks
// with the exact same domain::matchTracks() heuristic used for
// rekordbox<->Engine sync -- see domain::LocalRestorePlanner for how a
// restore proposal gets built from that match.
class LocalCueStore : public application::LibraryReader
{
public:
    // path: defaults to $XDG_DATA_HOME/djconvert/cues.db (or
    // $HOME/.local/share/djconvert/cues.db), creating the schema on first
    // use. An explicit path is accepted for tests.
    explicit LocalCueStore(std::string path = defaultPath());
    ~LocalCueStore() override;

    LocalCueStore(const LocalCueStore &) = delete;
    LocalCueStore &operator=(const LocalCueStore &) = delete;

    std::vector<domain::Track> readAll() override;

    // Upserts every track in `tracks` that has cues: matched against an
    // existing row by normalizeFilename + duration tolerance (mirroring
    // domain::matchTracks' fallback criterion -- title+artist is tried
    // first when available), replacing that row's cues and metadata.
    // Tracks with no cues are skipped -- there is nothing useful to back
    // up for them. Only ever writes to this database, never to the
    // source the tracks were read from.
    void upsert(const std::vector<domain::Track> &tracks, const std::string &sourceFormat,
                const std::string &sourceLabel);

    static std::string defaultPath();

private:
    sqlite3 *m_db = nullptr;
};

}  // namespace djconvert::infrastructure::local
