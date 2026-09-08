#include "infrastructure/backup/filesystem_backup_store.hpp"

#include "infrastructure/durable_file_write.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <span>
#include <sstream>

#include "infrastructure/stick_backup/posix_archive_file.hpp"
#include "infrastructure/stick_backup/zip64_reader.hpp"
#include "infrastructure/stick_backup/zip64_writer.hpp"

namespace seabass::infrastructure::backup
{

namespace fs = std::filesystem;
using application::BackupRecord;

namespace
{

// Metadata filenames inside each backup directory. Prefixed with "." so a
// user browsing the directory by hand (the fallback path this store was
// designed for -- see the class comment) sees them as incidental, and so
// they never collide with a real backed-up file's basename.
constexpr const char *ManifestFileName = ".manifest";
constexpr const char *DescriptionFileName = ".description";

std::string timestampNow()
{
    return std::format("{:%Y%m%dT%H%M%S}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
}

std::string sanitize(const std::string &label)
{
    std::string result = label;
    std::replace_if(
        result.begin(), result.end(), [](char c) { return !std::isalnum(static_cast<unsigned char>(c)); }, '-');
    return result;
}

std::uint64_t directorySize(const fs::path &dir)
{
    std::uint64_t total = 0;
    std::error_code ec;
    for (const auto &entry : fs::recursive_directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().filename() != ManifestFileName &&
            entry.path().filename() != DescriptionFileName) {
            total += entry.file_size(ec);
        }
    }
    return total;
}

std::string readWholeFile(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Format version 1: a "MANIFEST-VERSION\t1" header line, then one
// "<name-on-disk>\t<original absolute path>" line per backed-up file.
//
// Version 2 changes only what that second field may hold: a path under
// the stick this store lives on is now recorded RELATIVE to the stick
// root. Absolute paths were a real defect -- a stick does not come back
// at the same mount point after a reboot, and on Windows it gets
// whatever drive letter is free, so restoring a v1 backup on a stick
// that moved wrote to a path that no longer meant anything. Anything
// genuinely off the stick is still recorded absolute.
//
// Reading stays backwards compatible in both directions: a relative
// entry is resolved against the stick root, an absolute one is used as
// it stands, so v1 manifests restore exactly as they always did.
// If this ever changes again, bump this, keep parsing every older
// version exactly as it always has, and add the new version as an
// additional branch in readManifest() below -- the actual backwards-
// compatibility guarantee (see LocalCueStore's identical pattern for
// snapshot blobs, which this mirrors).
// Version 3 changes only what the FIRST field means: instead of a file
// sitting loose in the record directory, it names an entry inside
// `backup.zip` in that directory. The second field is unchanged, so the
// re-rooting v2 introduced applies identically. A v3 record is written
// only by backupToArchive(); backup()/addToBackup() still write v2, and
// restore() branches on the version it finds, so records made by any
// earlier build restore exactly as they always did.
constexpr int CurrentManifestFormatVersion = 2;
constexpr int ArchiveManifestFormatVersion = 3;
constexpr const char *ArchiveFileName = "backup.zip";

struct Manifest
{
    int version = 1;
    std::vector<std::pair<std::string, std::string>> entries;
};

// Backups made before this versioning scheme existed have no
// MANIFEST-VERSION line at all -- since version 1 is the only format that
// ever existed before now, an absent header means version 1, not unknown.
Manifest readManifest(const fs::path &dir)
{
    Manifest manifest;
    std::ifstream in(dir / ManifestFileName);
    if (!in.is_open()) {
        return manifest;
    }
    std::string line;
    while (std::getline(in, line)) {
        size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, tab);
        std::string value = line.substr(tab + 1);
        if (key == "MANIFEST-VERSION") {
            manifest.version = std::atoi(value.c_str());
        } else {
            manifest.entries.emplace_back(std::move(key), std::move(value));
        }
    }
    return manifest;
}

}  // namespace

// The stick this store lives on: baseDirectory is <stick>/.seabass-backups.
fs::path FilesystemBackupStore::stickRoot() const
{
    return fs::path(m_baseDirectory).parent_path();
}

// What goes in the manifest for `source`: relative to the stick when it is
// on the stick, absolute otherwise.
std::string FilesystemBackupStore::recordedPathFor(const fs::path &source) const
{
    const fs::path absolute = fs::absolute(source).lexically_normal();
    const fs::path root = stickRoot().lexically_normal();
    const fs::path relative = absolute.lexically_relative(root);
    if (relative.empty() || *relative.begin() == "..") {
        return absolute.string();  // genuinely off the stick
    }
    return relative.generic_string();
}

// The reverse: a relative manifest entry names a file on whichever stick
// this store is on *now*, which is the whole point of recording it that way.
fs::path FilesystemBackupStore::resolveRecordedPath(const std::string &recorded) const
{
    const fs::path path(recorded);
    if (path.is_absolute()) {
        return path;
    }
    return stickRoot() / path;
}

FilesystemBackupStore::FilesystemBackupStore(std::string baseDirectory) : m_baseDirectory(std::move(baseDirectory)) {}

BackupRecord FilesystemBackupStore::backup(const std::vector<std::string> &filePaths, const std::string &label)
{
    // timestampNow() only has second resolution, and callers that back up
    // several files under the same label in a tight loop (e.g. writing
    // cues to many duplicate/sync targets) can easily make more than one
    // backup() call within the same second. Since every rekordbox track's
    // analysis file is literally named "ANLZ0000.EXT" -- only its
    // containing directory differs -- two such calls landing in the same
    // directory would silently overwrite one track's backup with
    // another's, defeating the entire point of backing up first. Guard
    // against that by never reusing an existing directory.
    std::string baseId = timestampNow() + "-" + sanitize(label);
    std::string id = baseId;
    fs::path dir = fs::path(m_baseDirectory) / id;
    for (int suffix = 1; fs::exists(dir); ++suffix) {
        id = baseId + "-" + std::to_string(suffix);
        dir = fs::path(m_baseDirectory) / id;
    }
    fs::create_directories(dir);

    {
        std::ofstream manifest(dir / ManifestFileName, std::ios::app);
        manifest << "MANIFEST-VERSION\t" << CurrentManifestFormatVersion << '\n';
    }
    appendFiles(dir, filePaths);

    BackupRecord record;
    record.id = id;
    record.path = dir.string();
    record.label = label;
    record.sizeBytes = stateFor(dir).sizeBytes;
    return record;
}

BackupRecord FilesystemBackupStore::backupToArchive(const std::vector<std::string> &filePaths,
                                                    const std::string &label)
{
    std::string baseId = timestampNow() + "-" + sanitize(label);
    std::string id = baseId;
    fs::path dir = fs::path(m_baseDirectory) / id;
    for (int suffix = 1; fs::exists(dir); ++suffix) {
        id = baseId + "-" + std::to_string(suffix);
        dir = fs::path(m_baseDirectory) / id;
    }
    fs::create_directories(dir);

    std::vector<std::pair<std::string, std::string>> written;
    std::uint64_t archiveBytes = 0;
    {
        stick_backup::PosixArchiveFile file(dir / ArchiveFileName,
                                            stick_backup::PosixArchiveFile::OpenMode::ReadWrite);
        stick_backup::Zip64Writer writer(file, {});
        for (const auto &filePath : filePaths) {
            fs::path source(filePath);
            std::error_code ec;
            if (!fs::exists(source, ec)) {
                continue;
            }
            // The recorded path doubles as the entry name, so files that
            // share a basename need no _1/_2 disambiguation at all -- the
            // clash the loose layout has to guard against cannot arise.
            const std::string recorded = recordedPathFor(source);
            std::string entryName = recorded;
            std::replace(entryName.begin(), entryName.end(), '\\', '/');
            while (!entryName.empty() && entryName.front() == '/') {
                entryName.erase(entryName.begin());
            }
            if (entryName.empty()) {
                continue;
            }
            const std::string contents = readWholeFile(source);
            std::int64_t mtime = 0;
            if (auto stamp = fs::last_write_time(source, ec); !ec) {
                mtime = std::chrono::duration_cast<std::chrono::seconds>(
                            stamp.time_since_epoch())
                            .count();
            }
            writer.addFileFromMemory(entryName, mtime,
                                     std::as_bytes(std::span<const char>(contents.data(), contents.size())),
                                     nullptr, stick_backup::Compression::Deflate);
            written.emplace_back(entryName, recorded);
        }
        // The archive's own manifest entry is required by Zip64Writer and
        // is not part of the record; ours is the .manifest beside it.
        writer.finish("{}", "backup-manifest.json", 0);
        // Durable before anything is told this record exists.
        file.barrier();
        archiveBytes = file.size();
    }

    {
        std::ofstream manifest(dir / ManifestFileName, std::ios::app);
        manifest << "MANIFEST-VERSION\t" << ArchiveManifestFormatVersion << '\n';
        for (const auto &[entryName, recorded] : written) {
            manifest << entryName << '\t' << recorded << '\n';
        }
    }

    DirectoryState &state = stateFor(dir);
    state.sizeBytes = archiveBytes;

    BackupRecord record;
    record.id = id;
    record.path = dir.string();
    record.label = label;
    record.sizeBytes = archiveBytes;
    for (const auto &[entryName, recorded] : written) {
        record.filePaths.push_back(recorded);
    }
    return record;
}

bool FilesystemBackupStore::restoreFromArchive(const fs::path &dir,
                                               const std::vector<std::pair<std::string, std::string>> &entries)
{
    std::error_code ec;
    if (!fs::exists(dir / ArchiveFileName, ec)) {
        return false;
    }
    stick_backup::PosixArchiveFile file(dir / ArchiveFileName,
                                        stick_backup::PosixArchiveFile::OpenMode::ReadOnly);
    std::string error;
    auto reader = stick_backup::Zip64Reader::tryOpen(file, &error);
    if (!reader.has_value()) {
        return false;  // damaged archive: say so rather than restore a prefix
    }

    bool anyRestored = false;
    for (const auto &[entryName, originalPath] : entries) {
        auto index = reader->findEntry(entryName);
        if (!index.has_value()) {
            continue;
        }
        // Refuse a mismatch rather than write bytes that failed their own
        // checksum over a live file -- exactly the case restore exists for.
        if (!reader->verifyCrc(*index)) {
            continue;
        }
        const std::string contents = reader->readEntryToString(*index);
        const fs::path target = resolveRecordedPath(originalPath);
        fs::create_directories(target.parent_path(), ec);
        anyRestored = writeFileDurablyAtomic(target.string(), contents) || anyRestored;
    }
    return anyRestored;
}

BackupRecord FilesystemBackupStore::addToBackup(const std::string &id, const std::vector<std::string> &filePaths)
{
    fs::path dir = fs::path(m_baseDirectory) / id;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        throw std::runtime_error("no backup with id " + id + " to add to");
    }
    appendFiles(dir, filePaths);
    BackupRecord record;
    record.id = id;
    record.path = dir.string();
    size_t dash = id.find('-');
    record.label = dash == std::string::npos ? "" : id.substr(dash + 1);
    record.sizeBytes = stateFor(dir).sizeBytes;
    return record;
}

// What is already in one backup directory. Read from disk the first time
// that directory is touched, then kept up to date as files are added.
FilesystemBackupStore::DirectoryState &FilesystemBackupStore::stateFor(const fs::path &dir)
{
    auto it = m_directoryState.find(dir.string());
    if (it != m_directoryState.end()) {
        return it->second;
    }
    DirectoryState state;
    std::error_code ec;
    for (const auto &entry : fs::recursive_directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        state.takenNames.insert(name);
        if (name != ManifestFileName && name != DescriptionFileName) {
            state.sizeBytes += entry.file_size(ec);
        }
    }
    return m_directoryState.emplace(dir.string(), std::move(state)).first->second;
}

// Copies each file into `dir` and appends it to the manifest; shared by
// backup() and addToBackup(). Returns the bytes added.
std::uint64_t FilesystemBackupStore::appendFiles(const fs::path &dir, const std::vector<std::string> &filePaths)
{
    DirectoryState &state = stateFor(dir);
    std::ofstream manifest(dir / ManifestFileName, std::ios::app);
    std::uint64_t added = 0;
    for (const auto &filePath : filePaths) {
        fs::path source(filePath);
        if (!fs::exists(source)) {
            continue;
        }
        // Guard against two files in the same call sharing a basename
        // (e.g. rekordbox's ANLZ0000.EXT under different track
        // directories) the same way directory ids are guarded above --
        // otherwise the second copy would silently clobber the first
        // on disk, and restore() would only ever recover the last one.
        // Answered from the set of names already taken here rather than
        // by asking the filesystem once per candidate, which was one stat
        // per already-taken name on removable media.
        std::string destName = source.filename().string();
        for (int suffix = 1; state.takenNames.count(destName) > 0; ++suffix) {
            destName = source.filename().stem().string() + "_" + std::to_string(suffix) + source.extension().string();
        }
        state.takenNames.insert(destName);
        // Durable + atomic, not a plain copy_file: a crash mid-copy must
        // never leave a truncated file here that restore() would later
        // trust and silently write over the live original with garbage.
        const std::string contents = readWholeFile(source);
        if (!writeFileDurablyAtomic((dir / destName).string(), contents)) {
            throw std::runtime_error("failed to durably write backup copy of " + source.string());
        }
        added += contents.size();
        manifest << destName << '\t' << recordedPathFor(source) << '\n';
    }
    state.sizeBytes += added;
    return added;
}

std::vector<BackupRecord> FilesystemBackupStore::list()
{
    std::vector<BackupRecord> records;
    std::error_code ec;
    if (!fs::is_directory(m_baseDirectory, ec)) {
        return records;
    }

    for (const auto &entry : fs::directory_iterator(m_baseDirectory, ec)) {
        if (!entry.is_directory()) {
            continue;
        }
        BackupRecord record;
        record.id = entry.path().filename().string();
        record.path = entry.path().string();
        size_t dash = record.id.find('-');
        record.label = dash == std::string::npos ? "" : record.id.substr(dash + 1);
        record.description = readWholeFile(entry.path() / DescriptionFileName);
        record.sizeBytes = directorySize(entry.path());
        for (const auto &[onDisk, originalPath] : readManifest(entry.path()).entries) {
            record.filePaths.push_back(resolveRecordedPath(originalPath).string());
        }
        records.push_back(std::move(record));
    }

    // Backup ids are timestamp-prefixed, so lexical order is chronological.
    std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return records;
}

std::uint64_t FilesystemBackupStore::prune(size_t keepCount)
{
    auto records = list();
    if (records.size() <= keepCount) {
        return 0;
    }

    std::uint64_t freed = 0;
    size_t toRemove = records.size() - keepCount;
    for (size_t i = 0; i < toRemove; ++i) {
        std::error_code ec;
        freed += records[i].sizeBytes;
        fs::remove_all(records[i].path, ec);
    }
    return freed;
}

void FilesystemBackupStore::setDescription(const std::string &id, const std::string &description)
{
    fs::path dir = fs::path(m_baseDirectory) / id;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return;
    }
    std::ofstream out(dir / DescriptionFileName, std::ios::trunc);
    out << description;
}

bool FilesystemBackupStore::restore(const std::string &id)
{
    fs::path dir = fs::path(m_baseDirectory) / id;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return false;
    }
    auto manifest = readManifest(dir);
    if (manifest.entries.empty()) {
        return false;  // predates restore support, or nothing was ever backed up for this id
    }
    if (manifest.version > ArchiveManifestFormatVersion) {
        // Written by some future Seabass version this build doesn't
        // understand -- refuse rather than misinterpret it (the same
        // guarantee LocalCueStore's snapshot versioning makes).
        return false;
    }

    // Preserve the "always back up before writing" invariant for restore
    // itself: the current on-disk contents of every target path get their
    // own backup (label "pre-restore") before being overwritten. Same for
    // both layouts -- it is keyed on the recorded original path, which v3
    // did not change.
    std::vector<std::string> currentPaths;
    for (const auto &[onDisk, originalPath] : manifest.entries) {
        const fs::path target = resolveRecordedPath(originalPath);
        if (fs::exists(target, ec)) {
            currentPaths.push_back(target.string());
        }
    }
    if (!currentPaths.empty()) {
        backup(currentPaths, "pre-restore");
    }

    if (manifest.version >= ArchiveManifestFormatVersion) {
        return restoreFromArchive(dir, manifest.entries);
    }

    bool anyRestored = false;
    for (const auto &[onDisk, originalPath] : manifest.entries) {
        fs::path source = dir / onDisk;
        if (!fs::exists(source, ec)) {
            continue;
        }
        const fs::path target = resolveRecordedPath(originalPath);
        fs::create_directories(target.parent_path(), ec);
        // Durable + atomic, not a plain copy_file: this overwrites a
        // *live* file, and it's specifically the moment Seabass is
        // trusted to put things right -- a crash mid-copy must never
        // leave that file half-written (worse than either the backup or
        // what was there before).
        bool ok = writeFileDurablyAtomic(target.string(), readWholeFile(source));
        anyRestored = anyRestored || ok;
    }
    return anyRestored;
}

bool FilesystemBackupStore::remove(const std::string &id)
{
    fs::path dir = fs::path(m_baseDirectory) / id;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return false;
    }
    return fs::remove_all(dir, ec) > 0;
}

}  // namespace seabass::infrastructure::backup
