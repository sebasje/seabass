#pragma once

#include <filesystem>
#include <optional>

namespace seabass::infrastructure::local
{

// The file OpenStickBackup writes next to an extracted catalog to say
// "the analysis files for this library are still in that archive". One
// line, the absolute archive path, UTF-8.
inline constexpr const char *BrowsedBackupMarkerName = ".seabass-backup-source";

// Whether `libraryRoot` (the directory holding PIONEER / Engine Library)
// is the cache of a stick backup being browsed -- the one question every
// guard in the app asks, answered in one place.
//
// Two conditions, both required. The marker must be there, and the root
// must sit under paths::localBrowsedBackupsDir(). The second is what
// stops the marker from turning a real stick read-only: it is an
// ordinary dotfile, so a backup taken from a browse cache would carry it,
// a restore would write it onto the next stick, and a drag-copy of the
// cache folder would bring it along. Anywhere but the browse cache it is
// just a stray file, and it is treated as one.
bool isBrowsedBackupRoot(const std::filesystem::path &libraryRoot);

// The archive a browsed-backup root came from, or nullopt when
// isBrowsedBackupRoot() is false or the marker cannot be read.
std::optional<std::filesystem::path> browsedBackupArchive(const std::filesystem::path &libraryRoot);

}  // namespace seabass::infrastructure::local
