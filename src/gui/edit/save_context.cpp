#include "gui/edit/save_context.hpp"

#include <filesystem>

#include "infrastructure/backup/filesystem_backup_store.hpp"
#include "infrastructure/backup/stick_locks.hpp"
#include "infrastructure/logging/file_operation_log.hpp"

namespace seabass::gui
{

namespace fs = std::filesystem;

SaveContext::SaveContext(application::CancellationToken cancel, application::ProgressReporter &progress,
                         StatusSink status, QString rekordboxPath, QString enginePath)
    : m_cancel(std::move(cancel)),
      m_progress(progress),
      m_status(std::move(status)),
      m_rekordboxPath(std::move(rekordboxPath)),
      m_enginePath(std::move(enginePath))
{
}

// Shared resources die here, after the finish hooks ran (see
// runFinishHooks): a FormatWriteSession's scratch directory is removed
// only once its commit hook has had its chance.
SaveContext::~SaveContext() = default;

void SaveContext::status(const QString &text)
{
    if (m_status) {
        m_status(text);
    }
}

QString SaveContext::pathFor(const QString &format) const
{
    return format == "engine" ? m_enginePath : m_rekordboxPath;
}

std::string SaveContext::stickRoot() const
{
    const QString &any = m_rekordboxPath.isEmpty() ? m_enginePath : m_rekordboxPath;
    return infrastructure::backup::stickRootForCatalogPath(any.toStdString());
}

application::OperationLog &SaveContext::log()
{
    if (!m_log) {
        m_log = std::make_unique<infrastructure::logging::FileOperationLog>(
            infrastructure::backup::operationLogForStickRoot(stickRoot()));
    }
    return *m_log;
}

application::BackupStore &SaveContext::backupStore()
{
    if (!m_backupStore) {
        m_backupStore = std::make_unique<infrastructure::backup::FilesystemBackupStore>(
            infrastructure::backup::backupDirForStickRoot(stickRoot()));
    }
    return *m_backupStore;
}

bool SaveContext::backupOnce(const std::string &file, const std::string &label)
{
    if (file.empty() || m_backedUp.contains(file)) {
        return false;
    }
    auto existing = m_recordByLabel.find(label);
    application::BackupRecord record;
    if (existing == m_recordByLabel.end()) {
        record = backupStore().backup({file}, label);
        m_recordByLabel[label] = record.id;
        m_backups.push_back({QString::fromStdString(fs::path(record.path).parent_path().string()),
                             QString::fromStdString(record.id)});
    } else {
        record = backupStore().addToBackup(existing->second, {file});
    }
    log().record(label + ": backed up " + fs::path(file).filename().string() + " -> " + record.path);
    m_backedUp[file] = record.id;
    return true;
}

std::string SaveContext::backupIdOf(const std::string &file) const
{
    auto it = m_backedUp.find(file);
    return it == m_backedUp.end() ? std::string() : it->second;
}

void SaveContext::onFinish(std::function<void(bool)> hook)
{
    m_finishHooks.push_back(std::move(hook));
}

std::optional<QString> SaveContext::runFinishHooks(bool ok)
{
    if (m_hooksRan) {
        return std::nullopt;
    }
    m_hooksRan = true;
    std::optional<QString> firstError;
    for (auto &hook : m_finishHooks) {
        try {
            hook(ok);
        } catch (const std::exception &e) {
            if (!firstError) {
                firstError = QString::fromStdString(e.what());
            }
        }
    }
    return firstError;
}

std::vector<UndoableBackup> SaveContext::takeBackups()
{
    return std::move(m_backups);
}

}  // namespace seabass::gui
