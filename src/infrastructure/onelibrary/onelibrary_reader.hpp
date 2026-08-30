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
// Read-only. Tracks it returns carry format "onelibrary" -- SyncController
// treats it as a real source of truth, running the same diff+direction
// logic against it as against rekordbox/Engine (see SyncController's own
// class comment); most other write-oriented controllers (Clean Up, Local
// Cue Backup, Add Cue, duplicate matching) still only branch on
// "rekordbox"/"engine" and have no path for a third format, so callers
// there must keep tracks read this way out of those flows.
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
