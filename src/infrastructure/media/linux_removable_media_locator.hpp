#pragma once

#include <vector>

#include "application/ports/removable_media_locator.hpp"

namespace djconvert::infrastructure::media
{

// Finds candidate USB sticks by scanning the mount-point directories Linux
// desktop environments use for removable media (/media/$USER,
// /run/media/$USER, /media, /mnt), looking for the on-disk signature of
// either format under each mounted directory.
class LinuxRemovableMediaLocator : public application::RemovableMediaLocator
{
public:
    std::vector<application::DetectedStick> detect() override;
};

}  // namespace djconvert::infrastructure::media
