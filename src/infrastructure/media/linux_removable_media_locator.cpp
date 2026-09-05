#include "infrastructure/media/linux_removable_media_locator.hpp"

#include <libudev.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>

#include "infrastructure/media/stick_root_scan.hpp"

namespace seabass::infrastructure::media
{

namespace fs = std::filesystem;
using application::DetectedStick;

namespace
{

std::string unescapeMountField(const std::string &field)
{
    std::string result;
    result.reserve(field.size());
    for (size_t i = 0; i < field.size(); ++i) {
        if (field[i] == '\\' && i + 3 < field.size()) {
            std::string octal = field.substr(i + 1, 3);
            result.push_back(static_cast<char>(std::stoi(octal, nullptr, 8)));
            i += 3;
        } else {
            result.push_back(field[i]);
        }
    }
    return result;
}

// Maps a device node (e.g. "/dev/sdb1") to its current mount point, read
// from /proc/mounts.
std::map<std::string, std::string> readDeviceMountPoints()
{
    std::map<std::string, std::string> mounts;
    std::ifstream in("/proc/mounts");
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream iss(line);
        std::string device, mountPoint;
        if (!(iss >> device >> mountPoint) || device.rfind("/dev/", 0) != 0) {
            continue;
        }
        mounts[device] = unescapeMountField(mountPoint);
    }
    return mounts;
}

std::optional<std::string> udevProperty(udev_device *dev, const char *key)
{
    if (const char *value = udev_device_get_property_value(dev, key)) {
        return std::string(value);
    }
    return std::nullopt;
}

}  // namespace

std::vector<DetectedStick> LinuxRemovableMediaLocator::detect()
{
    std::unique_ptr<udev, decltype(&udev_unref)> udevContext(udev_new(), udev_unref);
    if (!udevContext) {
        return {};
    }

    std::unique_ptr<udev_enumerate, decltype(&udev_enumerate_unref)> enumerate(
        udev_enumerate_new(udevContext.get()), udev_enumerate_unref);
    udev_enumerate_add_match_subsystem(enumerate.get(), "block");
    udev_enumerate_scan_devices(enumerate.get());
    udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate.get());

    // A USB disk that has partitions is represented by its partitions, not
    // by the whole-disk device -- so first find every parent disk that has
    // at least one USB partition, and skip listing those disks directly.
    std::set<std::string> disksWithPartitions;
    udev_list_entry *entry;
    udev_list_entry_foreach(entry, devices)
    {
        std::unique_ptr<udev_device, decltype(&udev_device_unref)> dev(
            udev_device_new_from_syspath(udevContext.get(), udev_list_entry_get_name(entry)), udev_device_unref);
        if (!dev) {
            continue;
        }
        auto bus = udevProperty(dev.get(), "ID_BUS");
        const char *devtype = udev_device_get_devtype(dev.get());
        if (!bus || *bus != "usb" || !devtype || std::string(devtype) != "partition") {
            continue;
        }
        if (udev_device *parent = udev_device_get_parent_with_subsystem_devtype(dev.get(), "block", "disk")) {
            if (const char *parentSyspath = udev_device_get_syspath(parent)) {
                disksWithPartitions.insert(parentSyspath);
            }
        }
    }

    const auto mountedByDevice = readDeviceMountPoints();
    std::vector<DetectedStick> sticks;

    udev_list_entry_foreach(entry, devices)
    {
        const char *syspath = udev_list_entry_get_name(entry);
        std::unique_ptr<udev_device, decltype(&udev_device_unref)> dev(
            udev_device_new_from_syspath(udevContext.get(), syspath), udev_device_unref);
        if (!dev) {
            continue;
        }
        auto bus = udevProperty(dev.get(), "ID_BUS");
        if (!bus || *bus != "usb") {
            continue;
        }
        const char *devtypeC = udev_device_get_devtype(dev.get());
        std::string devtype = devtypeC ? devtypeC : "";
        if (devtype != "partition" && devtype != "disk") {
            continue;
        }
        if (devtype == "disk" && disksWithPartitions.count(syspath)) {
            continue;
        }

        const char *devnode = udev_device_get_devnode(dev.get());
        if (!devnode) {
            continue;
        }

        // A multislot card reader with nothing inserted in a given slot
        // still shows up as a real block device node here (unlike
        // Windows, where GetLogicalDrives() simply never assigns a drive
        // letter to an empty slot at all -- see
        // WindowsRemovableMediaLocator::detect()'s own comment). sysfs's
        // "size" attribute (512-byte sectors) is 0 for exactly that case
        // -- the same signal udisks2/gnome-disks use to hide empty
        // slots. A real stick or card always reports its actual nonzero
        // capacity here, whether or not it's currently mounted.
        const char *sizeAttr = udev_device_get_sysattr_value(dev.get(), "size");
        if (!sizeAttr || std::string(sizeAttr) == "0") {
            continue;
        }

        DetectedStick stick;
        stick.devicePath = devnode;

        // wholeDiskPath is the parent disk for a partition (e.g. "/dev/sdb"
        // for "/dev/sdb1"); for a disk-devtype entry (blank or superfloppy
        // media with no partition table) it's the same device as
        // devicePath. Needed by the Format USB Stick feature, which always
        // operates on the whole disk, never a single partition.
        if (devtype == "partition") {
            if (udev_device *parent = udev_device_get_parent_with_subsystem_devtype(dev.get(), "block", "disk")) {
                if (const char *parentDevnode = udev_device_get_devnode(parent)) {
                    stick.wholeDiskPath = parentDevnode;
                }
            }
        } else {
            stick.wholeDiskPath = devnode;
            // A disk-devtype entry only reaches here when it has no
            // partitions (see disksWithPartitions above) -- it's blank
            // exactly when it also has no filesystem or partition table
            // of its own (the rare superfloppy case: a filesystem written
            // directly to the raw disk, no partition table at all).
            stick.hasNoFilesystem =
                !udevProperty(dev.get(), "ID_FS_TYPE") && !udevProperty(dev.get(), "ID_PART_TABLE_TYPE");
        }

        // sizeAttr is in 512-byte sectors regardless of the device's real
        // block size -- a fixed unit the kernel always reports this
        // attribute in, not the drive's actual sector size.
        stick.capacityBytes = std::stoull(sizeAttr) * 512ULL;

        auto fsLabel = udevProperty(dev.get(), "ID_FS_LABEL");
        auto model = udevProperty(dev.get(), "ID_MODEL");
        stick.label = fsLabel ? *fsLabel : (model ? *model : fs::path(devnode).filename().string());

        // ID_DRIVE_FLASH_SD is set by udev's own built-in rules (the same
        // property udisks2/gnome-disks key their own SD-card icon off of,
        // rather than guessing from ID_MODEL/label text) -- usually on
        // the whole-disk device, not every partition of it, so check the
        // parent disk too when this device itself doesn't have it.
        auto isFlashSd = [](udev_device *d) { return udevProperty(d, "ID_DRIVE_FLASH_SD").has_value(); };
        stick.isSdCard = isFlashSd(dev.get());
        if (!stick.isSdCard) {
            if (udev_device *parent = udev_device_get_parent_with_subsystem_devtype(dev.get(), "block", "disk")) {
                stick.isSdCard = isFlashSd(parent);
            }
        }

        auto mountIt = mountedByDevice.find(devnode);
        if (mountIt != mountedByDevice.end()) {
            stick.mounted = true;
            stick.mountPoint = mountIt->second;
            scanMountedRoot(mountIt->second, stick);
        } else {
            stick.mounted = false;
        }

        sticks.push_back(std::move(stick));
    }

    return sticks;
}

}  // namespace seabass::infrastructure::media
