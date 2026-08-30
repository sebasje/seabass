#include "infrastructure/media/windows_removable_media_locator.hpp"

// windows.h must come before any other Windows header, and defines macros
// (min/max) this project never wants -- NOMINMAX suppresses those. MinGW's
// own C++ standard headers already define both, hence the #ifndef guards
// (a plain #define would just be redundant there, but MSVC doesn't
// predefine either, so the guards keep this correct for both toolchains).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "infrastructure/media/stick_root_scan.hpp"

namespace seabass::infrastructure::media
{

using application::DetectedStick;

namespace
{

std::string driveLetterToPath(char letter)
{
    return std::string(1, letter) + ":\\";
}

}  // namespace

std::vector<DetectedStick> WindowsRemovableMediaLocator::detect()
{
    std::vector<DetectedStick> sticks;

    // Bit N set means drive letter 'A' + N exists -- the standard way to
    // enumerate assigned drive letters without touching SetupAPI's much
    // heavier device-enumeration API, which isn't needed here since
    // Windows already hands every removable volume a drive letter.
    DWORD driveMask = ::GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(driveMask & (1u << i))) {
            continue;
        }
        std::string rootPath = driveLetterToPath(static_cast<char>('A' + i));
        if (::GetDriveTypeA(rootPath.c_str()) != DRIVE_REMOVABLE) {
            continue;
        }

        DetectedStick stick;
        stick.devicePath = rootPath;
        stick.mountPoint = rootPath;
        stick.mounted = true;

        char volumeName[MAX_PATH + 1] = {};
        if (::GetVolumeInformationA(rootPath.c_str(), volumeName, sizeof(volumeName), nullptr, nullptr, nullptr,
                                     nullptr, 0) &&
            volumeName[0] != '\0') {
            stick.label = volumeName;
        } else {
            stick.label = rootPath;
        }

        scanMountedRoot(rootPath, stick);
        sticks.push_back(std::move(stick));
    }

    return sticks;
}

}  // namespace seabass::infrastructure::media
