#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>

namespace djconvert::infrastructure::rekordbox
{

// Mutates a rekordbox export.pdb file's row-presence bits and select
// fixed-size row fields -- see specs/rekordbox_pdb.ksy for the format.
//
// Clearing a row's presence bit is the format's own real deletion
// mechanism, not a workaround: the spec's own doc comment on that bit
// says "Will be false if the row has been deleted." Rows are never
// moved or reflowed on delete (the spec describes real deletions
// leaving heap "gaps"), so every edit here is a precise, bounded
// overwrite of a handful of already-existing bytes -- never a
// structural change to the file.
//
// All edits happen against an in-memory copy of the file, read once at
// construction; the real file on disk is never opened for writing at
// all until commit(), which writes the edited copy to a temp file next
// to the original and atomically renames it into place. If commit() is
// never called (an exception, a caller deciding to abort, a crash), the
// original file is completely untouched.
//
// Hardening beyond the core edit path:
//  - the constructor rejects a file that doesn't even look like a real
//    export.pdb (implausible len_page/num_tables) before trusting any
//    offset computed from it;
//  - every byte read/written goes through little_endian.hpp's
//    bounds-checked accessors, so a corrupt/truncated file fails with a
//    clear exception instead of undefined behavior;
//  - commit() refuses (leaving the real file untouched) if the file on
//    disk has changed size or mtime since construction -- something
//    else touched it in the meantime, so our in-memory copy is stale;
//  - commit() re-parses the fully edited buffer with the real parser
//    before ever writing it anywhere, refusing if that throws -- a bug
//    in this class should never reach disk;
//  - the temp file is fsync()'d (FlushFileBuffers on Windows) before
//    the rename, so the edit is actually durable on the physical medium
//    the moment commit() returns true, not just sitting in a write-back
//    cache a pulled USB stick could lose.
class PdbRowWriter
{
public:
    explicit PdbRowWriter(std::string pdbPath);

    // Clears the presence bit for the track row with this id in the
    // tracks table. Returns false if no such (present) row is found.
    bool removeTrack(uint32_t trackId);

    // Clears the presence bit for the playlist_entry row matching
    // (playlistId, trackId). Returns false if not found.
    bool removePlaylistEntry(uint32_t playlistId, uint32_t trackId);

    // Overwrites an existing playlist_entry row's track_id field in
    // place, leaving its entry_index/playlist_id untouched. Returns
    // false if no (playlistId, oldTrackId) row is found.
    bool repointPlaylistEntry(uint32_t playlistId, uint32_t oldTrackId, uint32_t newTrackId);

    // Bumps the sequence number for every page touched this session
    // (page.sequence <- the header's current sequence; then the header's
    // own sequence is incremented -- matching the order the format's own
    // doc comment describes), re-parses the result to confirm it's
    // structurally valid, then atomically replaces the file at pdbPath
    // with the edited copy (fsync'd/flushed before the rename). Returns
    // false, leaving the original file completely untouched, if nothing
    // was ever successfully edited, the file on disk changed since
    // construction, the edited buffer fails to re-parse, or the
    // write/rename failed.
    bool commit();

private:
    std::string m_pdbPath;
    std::string m_buffer;
    std::set<uint32_t> m_editedPageIndices;
    std::uintmax_t m_originalFileSize = 0;
    std::filesystem::file_time_type m_originalMtime;
};

}  // namespace djconvert::infrastructure::rekordbox
