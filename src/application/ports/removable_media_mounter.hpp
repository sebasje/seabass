#pragma once

#include <optional>
#include <string>

namespace seabass::application
{

// Port for mounting/unmounting a removable device by its device path (e.g.
// "/dev/sdb1"), without the caller needing to know the host mechanism
// (udisks2 on Linux, ...).
class RemovableMediaMounter
{
public:
    virtual ~RemovableMediaMounter() = default;

    // On success, returns the mount point. On failure, returns nullopt and
    // fills errorMessage with a human-readable reason.
    virtual std::optional<std::string> mount(const std::string &devicePath, std::string &errorMessage) = 0;
    virtual bool unmount(const std::string &devicePath, std::string &errorMessage) = 0;
};

}  // namespace seabass::application
