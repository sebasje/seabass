#pragma once

#include <filesystem>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "application/ports/backup_store.hpp"

namespace seabass::infrastructure::backup
{

// Stores backups under baseDirectory, one directory per backup:
// baseDirectory/<timestamp>-<label>/. A record holds either loose copies
// named after the originals' basenames, or a single deflated `backup.zip`
// -- see backupToArchive() for why, and readManifest() in the .cpp for how
// both keep working.
//
// Either way a user can still get a file back by hand without Seabass: a
// zip opens in every file manager, and unlike the loose layout its entry
// names carry the whole relative path, so 200 files all called
// ANLZ0000.EXT are told apart by where they came from rather than by a
// _1/_2 suffix.
class FilesystemBackupStore : public application::BackupStore
{
public:
    explicit FilesystemBackupStore(std::string baseDirectory);

    application::BackupRecord backup(const std::vector<std::string> &filePaths, const std::string &label) override;

    // One record, one archive, written in a single pass and deflated.
    //
    // The loose layout costs one durable whole-file write per file, and
    // that is the whole cost of a save: about 118 ms each on Linux and
    // 15 ms on Windows, against roughly 2 s for the same bytes as one
    // file. It is also permanent -- a backup stays on the stick, nothing
    // prunes automatically, and 400 loose analysis files occupy 71.5 MB
    // against 45.5 MB deflated. See docs/write-path-performance.md.
    //
    // Unlike backup()+addToBackup() this needs the whole file list up
    // front, because an archive gets one central directory rather than
    // one per appended file.
    application::BackupRecord backupToArchive(const std::vector<std::string> &filePaths, const std::string &label);

    // Appends to an archive record, the way addToBackup() appends to a
    // loose one, so a save that discovers its files as it goes can still
    // use an archive. Each call re-writes the archive's central directory
    // and makes it durable, so the record is complete and readable after
    // every item -- the same guarantee the loose layout gives, since it
    // fsyncs every copy. The superseded central directories become dead
    // space; archive_compactor reclaims it.
    //
    // Throws if `id` is not an archive record.
    application::BackupRecord addToArchive(const std::string &id, const std::vector<std::string> &filePaths);
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
    bool restoreFromArchive(const std::filesystem::path &dir,
                            const std::vector<std::pair<std::string, std::string>> &entries);
    // Appends `filePaths` to the archive in `dir`, returns the entry
    // name / recorded path pairs actually written and the archive's new
    // size. Shared by backupToArchive() and addToArchive().
    std::pair<std::vector<std::pair<std::string, std::string>>, std::uint64_t>
    writeArchiveEntries(const std::filesystem::path &dir, const std::vector<std::string> &filePaths);
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
