#pragma once

#include <string>

#include "application/ports/backup_store.hpp"

namespace djconvert::infrastructure::backup
{

// Stores backups as plain directories under baseDirectory, one per backup:
// baseDirectory/<timestamp>-<label>/<original file basenames>. Deliberately
// simple (no compression/archive format) so a user can just look inside and
// copy a file back by hand if djconvert itself isn't available.
class FilesystemBackupStore : public application::BackupStore
{
public:
    explicit FilesystemBackupStore(std::string baseDirectory);

    application::BackupRecord backup(const std::vector<std::string> &filePaths, const std::string &label) override;
    std::vector<application::BackupRecord> list() override;
    std::uint64_t prune(size_t keepCount) override;

private:
    std::string m_baseDirectory;
};

}  // namespace djconvert::infrastructure::backup
