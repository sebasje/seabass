#include "write_guard.hpp"

#include "infrastructure/system/rekordbox_process_detector.hpp"

namespace seabass::gui
{

QString refuseIfRekordboxRunning()
{
    if (infrastructure::system::isRekordboxRunning()) {
        return "Refused: rekordbox appears to be running on this machine. Close it before writing to "
               "this stick -- both writing to the same files at once risks corrupting your library.";
    }
    return {};
}

}  // namespace seabass::gui
