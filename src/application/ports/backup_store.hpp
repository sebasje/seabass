#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace djconvert::application
{

struct BackupRecord
{
    std::string id;         // directory name, sorts chronologically (e.g. "20260826T193000-hot-cues")
    std::string path;       // full path to the backup directory
    std::string label;
    std::uint64_t sizeBytes = 0;
};

// Port for keeping "undo" copies of files before a mutating write touches
// them. Every write djconvert performs must go through here first -- see
// the plan's Backups section: manageable size (only the files actually
// touched, not a full stick copy), with a way to prune old ones.
class BackupStore
{
public:
    virtual ~BackupStore() = default;

    virtual BackupRecord backup(const std::vector<std::string> &filePaths, const std::string &label) = 0;
    virtual std::vector<BackupRecord> list() = 0;

    // Deletes the oldest backups so at most keepCount remain. Returns the
    // number of bytes freed.
    virtual std::uint64_t prune(size_t keepCount) = 0;
};

}  // namespace djconvert::application
