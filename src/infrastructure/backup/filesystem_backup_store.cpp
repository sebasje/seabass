#include "infrastructure/backup/filesystem_backup_store.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>

namespace djconvert::infrastructure::backup
{

namespace fs = std::filesystem;
using application::BackupRecord;

namespace
{

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
        if (entry.is_regular_file()) {
            total += entry.file_size(ec);
        }
    }
    return total;
}

}  // namespace

FilesystemBackupStore::FilesystemBackupStore(std::string baseDirectory) : m_baseDirectory(std::move(baseDirectory)) {}

BackupRecord FilesystemBackupStore::backup(const std::vector<std::string> &filePaths, const std::string &label)
{
    std::string id = timestampNow() + "-" + sanitize(label);
    fs::path dir = fs::path(m_baseDirectory) / id;
    fs::create_directories(dir);

    for (const auto &filePath : filePaths) {
        fs::path source(filePath);
        if (fs::exists(source)) {
            fs::copy_file(source, dir / source.filename(), fs::copy_options::overwrite_existing);
        }
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
        record.sizeBytes = directorySize(entry.path());
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

}  // namespace djconvert::infrastructure::backup
