#pragma once

#include <string>

namespace seabass::infrastructure::system
{

// Best-effort: scans this machine's running processes for one whose own
// process name (not full command line) case-insensitively matches `name`.
// A `false` result means "not detected," never "definitely not running" --
// a background/tray process, a different user session, or a platform this
// detector doesn't support yet (see the .cpp) can all defeat it. Callers
// must treat this as an advisory safety net on top of, never instead of,
// the app's own write-locking (see infrastructure::backup::StickWriteLock).
bool isProcessRunning(const std::string &name);

// Convenience wrapper for the one process this app actually cares about:
// writing to a stick's files while rekordbox itself might also have them
// open is a real corruption risk.
bool isRekordboxRunning();

}  // namespace seabass::infrastructure::system
