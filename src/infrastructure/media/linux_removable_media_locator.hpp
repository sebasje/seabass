#pragma once

#include <vector>

#include "application/ports/removable_media_locator.hpp"

namespace seabass::infrastructure::media
{

// Finds candidate USB sticks via udev: every USB block device (mounted or
// not) is reported, with the on-disk signature of either format checked
// for on any that are currently mounted.
class LinuxRemovableMediaLocator : public application::RemovableMediaLocator
{
public:
    std::vector<application::DetectedStick> detect() override;
};

}  // namespace seabass::infrastructure::media
