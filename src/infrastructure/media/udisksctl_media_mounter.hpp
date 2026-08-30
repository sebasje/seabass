#pragma once

#include "application/ports/removable_media_mounter.hpp"

namespace seabass::infrastructure::media
{

// Mounts/unmounts removable devices via the "udisksctl" command-line tool
// (part of udisks2), which handles polkit authorization for the active
// desktop session without requiring root.
class UdisksctlMediaMounter : public application::RemovableMediaMounter
{
public:
    std::optional<std::string> mount(const std::string &devicePath, std::string &errorMessage) override;
    bool unmount(const std::string &devicePath, std::string &errorMessage) override;
};

}  // namespace seabass::infrastructure::media
