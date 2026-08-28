#pragma once

#include <cstdint>
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
    // doc comment describes), then atomically replaces the file at
    // pdbPath with the edited copy. Returns false, leaving the original
    // file completely untouched, if nothing was ever successfully
    // edited or the write/rename failed.
    bool commit();

private:
    std::string m_pdbPath;
    std::string m_buffer;
    std::set<uint32_t> m_editedPageIndices;
};

}  // namespace djconvert::infrastructure::rekordbox
