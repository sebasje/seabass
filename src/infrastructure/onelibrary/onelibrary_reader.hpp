#pragma once

#include <string>

#include "application/ports/library_reader.hpp"

namespace djconvert::infrastructure::onelibrary
{

// Reads tracks (with cues and playlists) out of Rekordbox's "OneLibrary" /
// "Device Library Plus" format (exportLibrary.db), the third catalog a
// stick can carry alongside the classic Device Library (export.pdb) and
// Engine (m.db) -- see docs/onelibrary-format.md for the schema this is
// based on.
//
// Read-only and deliberately standalone: unlike OneLibraryCueWriter (which
// only ever mirrors a write already made through the export.pdb path),
// this is the first thing in this codebase that treats OneLibrary as a
// real source of truth in its own right rather than a secondary target.
// Tracks it returns carry format "onelibrary" -- every write-oriented
// controller in this app (Clean Up, Sync, Local Cue Backup, Add Cue,
// duplicate matching) only ever branches on "rekordbox"/"engine" and has
// no path for a third format, so callers must keep tracks read this way
// out of those flows; ScanPage.qml's own "browse this catalog" use is the
// only place this is wired in.
class OneLibraryReader : public application::LibraryReader
{
public:
    // pioneerRoot: the stick's "PIONEER" folder, same argument
    // OneLibraryCueWriter takes. Throws if exportLibrary.db doesn't exist
    // for this stick -- callers should check OneLibraryCueWriter::
    // existsFor() first, same convention as the writer.
    explicit OneLibraryReader(std::string pioneerRoot);

    std::vector<domain::Track> readAll() override;

private:
    std::string m_pioneerRoot;
};

}  // namespace djconvert::infrastructure::onelibrary
