#include "infrastructure/media/linux_removable_media_locator.hpp"

#include <cstdlib>
#include <filesystem>
#include <set>

namespace djconvert::infrastructure::media
{

namespace fs = std::filesystem;
using application::DetectedStick;

std::vector<DetectedStick> LinuxRemovableMediaLocator::detect()
{
    std::vector<std::string> bases;
    if (const char *user = std::getenv("USER")) {
        bases.push_back(std::string("/media/") + user);
        bases.push_back(std::string("/run/media/") + user);
    }
    bases.push_back("/media");
    bases.push_back("/mnt");

    std::vector<DetectedStick> sticks;
    std::set<std::string> seen;

    for (const auto &base : bases) {
        std::error_code ec;
        if (!fs::is_directory(base, ec)) {
            continue;
        }
        for (const auto &entry : fs::directory_iterator(base, ec)) {
            if (!entry.is_directory()) {
                continue;
            }
            std::string mountPoint = entry.path().string();
            if (!seen.insert(mountPoint).second) {
                continue;
            }

            DetectedStick stick;
            fs::path pdbPath = entry.path() / "PIONEER" / "rekordbox" / "export.pdb";
            fs::path engineDbPath = entry.path() / "Engine Library" / "Database2" / "m.db";

            if (fs::exists(pdbPath, ec)) {
                stick.rekordboxPath = (entry.path() / "PIONEER").string();
            }
            if (fs::exists(engineDbPath, ec)) {
                stick.enginePath = (entry.path() / "Engine Library").string();
            }
            if (!stick.rekordboxPath && !stick.enginePath) {
                continue;
            }

            stick.mountPoint = mountPoint;
            stick.label = entry.path().filename().string();
            sticks.push_back(std::move(stick));
        }
    }

    return sticks;
}

}  // namespace djconvert::infrastructure::media
