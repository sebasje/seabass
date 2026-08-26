#pragma once

#include <optional>
#include <string>
#include <vector>

namespace djconvert::application
{

// A mounted removable drive that looks like a rekordbox and/or Engine USB
// stick (a single stick commonly carries both side by side).
struct DetectedStick
{
    std::string mountPoint;
    std::string label;
    std::optional<std::string> rekordboxPath;  // the "PIONEER" folder, if export.pdb was found under it
    std::optional<std::string> enginePath;     // the "Engine Library" folder, if Database2/m.db was found under it
};

// Port for finding candidate USB sticks without the caller needing to know
// how removable media shows up on the host OS (mount points on Linux,
// drive letters on Windows, ...).
class RemovableMediaLocator
{
public:
    virtual ~RemovableMediaLocator() = default;
    virtual std::vector<DetectedStick> detect() = 0;
};

}  // namespace djconvert::application
