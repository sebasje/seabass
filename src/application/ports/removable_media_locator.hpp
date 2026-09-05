#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace seabass::application
{

// A removable USB drive, mounted or not. Sticks that look like a rekordbox
// and/or Engine export (a single stick commonly carries both side by side)
// have rekordboxPath/enginePath set; that's only possible once mounted.
struct DetectedStick
{
    std::string devicePath;  // e.g. "/dev/sdb1" -- a partition, empty if the whole disk has no partition at all
    std::string mountPoint;  // empty if not currently mounted
    std::string label;
    bool mounted = false;
    // Best-effort, for the UI to show a different icon (BRAINSTORM.md:
    // "show different icon for an SD card for improved clarity") -- not a
    // hard guarantee. See each RemovableMediaLocator implementation's own
    // comment for how (or whether, on that platform) it's actually
    // determined; a locator that can't tell just leaves this false.
    bool isSdCard = false;
    std::optional<std::string> rekordboxPath;  // the "PIONEER" folder, if export.pdb was found under it
    std::optional<std::string> enginePath;     // the "Engine Library" folder, if Database2/m.db was found under it

    // ---- Added for the Format USB Stick feature -- see the approved
    // plan for why: formatting needs the *whole disk*, not a partition,
    // and must be able to see a drive with no partition table at all
    // (invisible to devicePath/mounted/label above, which all assume an
    // existing filesystem). Populated by every RemovableMediaLocator
    // implementation, not just for sticks that already look like a DJ
    // library. ----

    std::string wholeDiskPath;      // e.g. "/dev/sdb" (Linux) or "\\\\.\\PhysicalDrive1" (Windows)
    std::uint64_t capacityBytes = 0;
    // True when the whole disk has no partition table/filesystem at all
    // (a genuinely blank drive) -- distinct from `mounted`, which only
    // says whether an *existing* filesystem happens to be mounted right
    // now.
    bool hasNoFilesystem = false;
    // A cheap, read-only peek at what's actually on the drive already --
    // top-level file/folder names, capped to a small number by the
    // locator. Deliberately not a full recursive scan (this is meant for
    // "let a human recognize what they're about to erase," not a library
    // scan) and only ever populated when the drive is already mounted
    // (a blank/unpartitioned drive has nothing to list, and this project
    // never mounts something purely to list it). See the plan's
    // "Windows installer stick" near-miss for why this exists: a raw
    // disk index/size alone isn't something a person can reliably
    // recognize across reboots and replugs.
    std::vector<std::string> rootEntries;
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

}  // namespace seabass::application
