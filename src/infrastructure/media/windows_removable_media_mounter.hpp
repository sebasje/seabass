#pragma once

#include "application/ports/removable_media_mounter.hpp"

namespace djconvert::infrastructure::media
{

// Windows auto-assigns a drive letter to a removable volume as soon as
// it's inserted, so there's no separate "mount" step to perform the way
// udisksctl needs one on Linux -- mount() just confirms the drive is
// present and ready. unmount() does real work: the same
// lock-volume -> dismount-volume -> eject-media sequence Windows' own
// "Safely Remove Hardware" uses, via DeviceIoControl.
class WindowsRemovableMediaMounter : public application::RemovableMediaMounter
{
public:
    std::optional<std::string> mount(const std::string &devicePath, std::string &errorMessage) override;
    bool unmount(const std::string &devicePath, std::string &errorMessage) override;
};

}  // namespace djconvert::infrastructure::media
