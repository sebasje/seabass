#pragma once

#include <memory>
#include <string>

#include "infrastructure/rekordbox/anlz_byte_source.hpp"

namespace seabass::infrastructure::rekordbox
{

// The file OpenStickBackup writes next to an extracted catalog to say
// "the analysis files for this library are still in that archive". Sits
// in the library root (the directory holding PIONEER / Engine Library),
// and holds the absolute archive path, one line, UTF-8.
inline constexpr const char *BackupSourceMarkerName = ".seabass-backup-source";

// The right AnlzByteSource for a PIONEER folder, decided by looking for
// the marker above in its parent directory: an archive-backed source for
// a stick backup being browsed, the plain filesystem otherwise.
//
// Deciding it here, from what is on disk, rather than through a
// process-wide registry the composition roots have to remember to
// populate: every existing construction of a rekordbox reader then works
// against a browsed backup without changing, and it survives a restart
// because the marker does. A cache directory that says what it is beats
// one that needs a live object to explain it.
//
// Falls back to the filesystem source whenever the marker is missing,
// unreadable, or names an archive that will not open -- browsing then
// simply shows no cues, rather than failing the whole scan.
std::shared_ptr<AnlzByteSource> anlzSourceForPioneerRoot(const std::string &pioneerRoot);

}  // namespace seabass::infrastructure::rekordbox
