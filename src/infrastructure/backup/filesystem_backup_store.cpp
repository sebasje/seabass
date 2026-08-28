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
#include <sstream>

namespace djconvert::infrastructure::backup
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
// "<name-on-disk>\t<original absolute path>" line per backed-up file. If
// this format ever needs to change, bump this, keep parsing every older
// version exactly as it always has, and add the new version as an
// additional branch in readManifest() below -- the actual backwards-
// compatibility guarantee (see LocalCueStore's identical pattern for
// snapshot blobs, which this mirrors).
constexpr int CurrentManifestFormatVersion = 1;

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

    std::ofstream manifest(dir / ManifestFileName, std::ios::app);
    manifest << "MANIFEST-VERSION\t" << CurrentManifestFormatVersion << '\n';
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
        fs::path destName = source.filename();
        for (int suffix = 1; fs::exists(dir / destName); ++suffix) {
            destName = source.filename().stem().string() + "_" + std::to_string(suffix) + source.extension().string();
        }
        // Durable + atomic, not a plain copy_file: a crash mid-copy must
        // never leave a truncated file here that restore() would later
        // trust and silently write over the live original with garbage.
        if (!writeFileDurablyAtomic((dir / destName).string(), readWholeFile(source))) {
            throw std::runtime_error("failed to durably write backup copy of " + source.string());
        }
        manifest << destName.string() << '\t' << fs::absolute(source).string() << '\n';
    }

    BackupRecord record;
    record.id = id;
    record.path = dir.string();
    record.label = label;
    record.sizeBytes = directorySize(dir);
    return record;
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
            record.filePaths.push_back(originalPath);
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
    if (manifest.version > CurrentManifestFormatVersion) {
        // Written by some future djconvert version this build doesn't
        // understand -- refuse rather than misinterpret it (the same
        // guarantee LocalCueStore's snapshot versioning makes).
        return false;
    }

    // Preserve the "always back up before writing" invariant for restore
    // itself: the current on-disk contents of every target path get their
    // own backup (label "pre-restore") before being overwritten.
    std::vector<std::string> currentPaths;
    for (const auto &[onDisk, originalPath] : manifest.entries) {
        if (fs::exists(originalPath, ec)) {
            currentPaths.push_back(originalPath);
        }
    }
    if (!currentPaths.empty()) {
        backup(currentPaths, "pre-restore");
    }

    bool anyRestored = false;
    for (const auto &[onDisk, originalPath] : manifest.entries) {
        fs::path source = dir / onDisk;
        if (!fs::exists(source, ec)) {
            continue;
        }
        fs::create_directories(fs::path(originalPath).parent_path(), ec);
        // Durable + atomic, not a plain copy_file: this overwrites a
        // *live* file, and it's specifically the moment djconvert is
        // trusted to put things right -- a crash mid-copy must never
        // leave that file half-written (worse than either the backup or
        // what was there before).
        bool ok = writeFileDurablyAtomic(originalPath, readWholeFile(source));
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

}  // namespace djconvert::infrastructure::backup
