#pragma once

#include <filesystem>
#include <cstdint>
#include <map>
#include <set>
#include <string>

#include "application/ports/backup_store.hpp"

namespace seabass::infrastructure::backup
{

// Stores backups as plain directories under baseDirectory, one per backup:
// baseDirectory/<timestamp>-<label>/<original file basenames>. Deliberately
// simple (no compression/archive format) so a user can just look inside and
// copy a file back by hand if Seabass itself is not available.
class FilesystemBackupStore : public application::BackupStore
{
public:
    explicit FilesystemBackupStore(std::string baseDirectory);

    application::BackupRecord backup(const std::vector<std::string> &filePaths, const std::string &label) override;
    std::vector<application::BackupRecord> list() override;
    application::BackupRecord addToBackup(const std::string &id,
                                          const std::vector<std::string> &filePaths) override;
    std::uint64_t prune(size_t keepCount) override;
    void setDescription(const std::string &id, const std::string &description) override;
    bool restore(const std::string &id) override;
    bool remove(const std::string &id) override;

private:
    // Copies each file in and returns how many bytes were added, so a
    // record's size can be accumulated rather than recomputed by walking
    // the whole directory again after every call.
    std::uint64_t appendFiles(const std::filesystem::path &dir, const std::vector<std::string> &filePaths);
    // Paths on the stick are stored relative to it, so a backup still
    // restores after the stick comes back at a different mount point or
    // drive letter. See CurrentManifestFormatVersion in the .cpp.
    std::filesystem::path stickRoot() const;
    std::string recordedPathFor(const std::filesystem::path &source) const;
    std::filesystem::path resolveRecordedPath(const std::string &recorded) const;

    std::string m_baseDirectory;

    // Per backup directory: the names already taken in it, and the bytes
    // it holds. Both exist to keep a save linear in the number of files
    // it backs up rather than quadratic.
    //
    // A save backing up 200 rekordbox analysis files hits both. Every one
    // of them is named ANLZ0000.EXT -- only the containing directory
    // differs -- so the clash guard used to stat the destination once per
    // already-taken name, 20,000 stats on removable media for 200 files.
    // And the byte total was recomputed by walking the whole directory
    // after every single call.
    //
    // Seeded from disk the first time a directory is touched, so this
    // stays correct for a store pointed at backups an earlier run made.
    struct DirectoryState
    {
        std::set<std::string> takenNames;
        std::uint64_t sizeBytes = 0;
    };
    DirectoryState &stateFor(const std::filesystem::path &dir);

    std::map<std::string, DirectoryState> m_directoryState;
};

}  // namespace seabass::infrastructure::backup
