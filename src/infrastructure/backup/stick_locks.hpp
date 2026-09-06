#pragma once

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "infrastructure/backup/stick_write_lock.hpp"

// The on-stick names every write path shares, and the one way to take
// the per-stick write lock for a set of directories. Qt-free so the CLI
// can use the exact same helpers as the GUI.
namespace seabass::infrastructure::backup
{

inline constexpr const char *BackupsDirName = ".seabass-backups";
inline constexpr const char *WriteLockName = ".write.lock";
inline constexpr const char *OperationLogName = ".seabass.log";

// A catalog path is the "PIONEER" or "Engine Library" folder; the stick
// root is its parent.
inline std::string stickRootForCatalogPath(const std::string &catalogPath)
{
    return std::filesystem::path(catalogPath).parent_path().string();
}

inline std::string backupDirForStickRoot(const std::string &stickRoot)
{
    return (std::filesystem::path(stickRoot) / BackupsDirName).string();
}

inline std::string backupDirForCatalogPath(const std::string &catalogPath)
{
    return backupDirForStickRoot(stickRootForCatalogPath(catalogPath));
}

inline std::string operationLogForStickRoot(const std::string &stickRoot)
{
    return (std::filesystem::path(stickRoot) / OperationLogName).string();
}

inline std::string writeLockPathForBackupDir(const std::string &backupDir)
{
    return (std::filesystem::path(backupDir) / WriteLockName).string();
}

// Acquires one StickWriteLock per distinct directory (sorted first so two
// concurrent multi-lock callers always acquire in the same order, and
// deduped so locking the same directory twice, e.g. rekordbox and
// OneLibrary sharing one stick root's .seabass-backups, never self-
// deadlocks). Held for the caller's whole scope via RAII. Throws
// StickBusyError if any of them is held elsewhere.
inline std::vector<std::unique_ptr<StickWriteLock>> acquireStickLocks(std::vector<std::string> backupDirs)
{
    std::sort(backupDirs.begin(), backupDirs.end());
    backupDirs.erase(std::unique(backupDirs.begin(), backupDirs.end()), backupDirs.end());
    std::vector<std::unique_ptr<StickWriteLock>> locks;
    locks.reserve(backupDirs.size());
    for (const auto &dir : backupDirs) {
        locks.push_back(std::make_unique<StickWriteLock>(writeLockPathForBackupDir(dir)));
    }
    return locks;
}

}  // namespace seabass::infrastructure::backup
