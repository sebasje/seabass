#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace seabass::infrastructure::stick_backup
{

enum class TargetOs
{
    Posix,
    Windows,
};

constexpr TargetOs hostTargetOs()
{
#if defined(_WIN32)
    return TargetOs::Windows;
#else
    return TargetOs::Posix;
#endif
}

// Turns an archive entry name into a path that is safe to create under a
// restore target, or explains why it is not. Rejects everything that
// could land outside the target or cannot exist on the target OS:
// absolute paths, drive letters, `.` / `..` segments, empty segments,
// backslashes and NUL (never legal in a FAT/exFAT/NTFS name), and on
// Windows the reserved device names (CON, AUX, COM1 ...) with any
// extension, trailing dots or spaces, and the characters <>:"|?*.
// A rejected entry is reported by the caller, never silently skipped.
//
// Trailing '/' (a directory entry) is stripped; `isDirectory` says so.
std::optional<std::filesystem::path> sanitizeEntryName(std::string_view entryName, TargetOs os, std::string *reason = nullptr,
                                                      bool *isDirectory = nullptr);

// On Windows, paths longer than the classic MAX_PATH need the \\?\ prefix
// to be created at all; long artist/album/title paths get there easily.
// Identity elsewhere.
std::filesystem::path longPathSafe(const std::filesystem::path &absolute);

}  // namespace seabass::infrastructure::stick_backup
