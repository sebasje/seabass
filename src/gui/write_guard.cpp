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

QString refuseIfDjSoftwareRunning()
{
    std::string name = infrastructure::system::conflictingDjSoftwareName();
    if (name.empty()) {
        return {};
    }
    return QStringLiteral("Refused: %1 appears to be running on this machine. Close it first -- it may be writing to "
                          "the stick's database while Seabass reads or replaces it.")
        .arg(QString::fromStdString(name));
}

}  // namespace seabass::gui
