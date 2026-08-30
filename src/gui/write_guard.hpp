#pragma once

#include <QString>

namespace seabass::gui
{

// Empty if it's safe to proceed with a write; otherwise the exact error
// message to surface, because Rekordbox appears to be running on this
// machine. Call this at the very top of every background write task,
// before acquiring the stick's write lock -- see
// infrastructure::system::isRekordboxRunning()'s doc comment for why
// detection can only ever be advisory, never a substitute for that lock.
QString refuseIfRekordboxRunning();

}  // namespace seabass::gui
