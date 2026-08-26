#include "infrastructure/media/linux_removable_media_locator.hpp"

#include <libudev.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace djconvert::infrastructure::media
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

        DetectedStick stick;
        stick.devicePath = devnode;

        auto fsLabel = udevProperty(dev.get(), "ID_FS_LABEL");
        auto model = udevProperty(dev.get(), "ID_MODEL");
        stick.label = fsLabel ? *fsLabel : (model ? *model : fs::path(devnode).filename().string());

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

}  // namespace djconvert::infrastructure::media
