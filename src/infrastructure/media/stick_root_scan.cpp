#include "infrastructure/media/stick_root_scan.hpp"

#include <filesystem>

namespace djconvert::infrastructure::media
{

namespace fs = std::filesystem;
using application::DetectedStick;

void scanMountedRoot(const std::string &mountPoint, DetectedStick &stick)
{
    std::error_code ec;
    fs::path root(mountPoint);
    fs::path pdbPath = root / "PIONEER" / "rekordbox" / "export.pdb";
    fs::path engineDbPath = root / "Engine Library" / "Database2" / "m.db";

    if (fs::exists(pdbPath, ec)) {
        stick.rekordboxPath = (root / "PIONEER").string();
    }
    if (fs::exists(engineDbPath, ec)) {
        stick.enginePath = (root / "Engine Library").string();
    }
}

}  // namespace djconvert::infrastructure::media
