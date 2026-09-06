#include "infrastructure/local/app_data_directory.hpp"

#include <cstdlib>

namespace seabass::infrastructure::local
{

namespace fs = std::filesystem;

fs::path appDataDirectory()
{
#if defined(_WIN32)
    // Windows has no XDG_DATA_HOME/HOME -- LOCALAPPDATA is the equivalent
    // convention for per-user app data that shouldn't roam. Falling
    // through to USERPROFILE if it's ever unset (Windows always sets it
    // for real user sessions) rather than crashing.
    const char *localAppData = std::getenv("LOCALAPPDATA");
    const char *userProfile = std::getenv("USERPROFILE");
    fs::path dataDir = (localAppData && *localAppData) ? fs::path(localAppData)
                                                        : fs::path(userProfile ? userProfile : ".") / "AppData" / "Local";
#else
    const char *xdgDataHome = std::getenv("XDG_DATA_HOME");
    const char *home = std::getenv("HOME");
    fs::path dataDir = (xdgDataHome && *xdgDataHome) ? fs::path(xdgDataHome)
                                                      : fs::path(home ? home : ".") / ".local" / "share";
#endif
    return dataDir / "seabass";
}

}  // namespace seabass::infrastructure::local
