#include "gui/edit/save_context.hpp"

#include <map>
#include <set>
#include <vector>

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
    return archiveStore();
}

infrastructure::backup::FilesystemBackupStore &SaveContext::archiveStore()
{
    if (!m_backupStore) {
        m_backupStore = std::make_unique<infrastructure::backup::FilesystemBackupStore>(
            infrastructure::backup::backupDirForStickRoot(stickRoot()));
    }
    return *m_backupStore;
}

void SaveContext::backupAllNow(const std::vector<BackupTarget> &targets)
{
    // Grouped by label, in first-seen order, because a save may hold
    // several kinds of change from one page and each keeps its own record.
    std::vector<std::string> labelOrder;
    std::map<std::string, std::vector<std::string>> byLabel;
    std::set<std::string> seen;
    for (const auto &target : targets) {
        if (target.file.empty() || m_backedUp.contains(target.file) || !seen.insert(target.file).second) {
            continue;
        }
        if (!byLabel.contains(target.label)) {
            labelOrder.push_back(target.label);
        }
        byLabel[target.label].push_back(target.file);
    }

    for (const std::string &label : labelOrder) {
        const std::vector<std::string> &files = byLabel[label];
        auto existing = m_recordByLabel.find(label);
        application::BackupRecord record;
        if (existing == m_recordByLabel.end()) {
            record = archiveStore().backupToArchive(files, label);
            m_recordByLabel[label] = record.id;
            m_backups.push_back({QString::fromStdString(fs::path(record.path).parent_path().string()),
                                 QString::fromStdString(record.id)});
        } else {
            record = archiveStore().addToArchive(existing->second, files);
        }
        log().record(label + ": backed up " + std::to_string(files.size()) + " file(s) -> " + record.path);
        for (const std::string &file : files) {
            m_backedUp[file] = record.id;
        }
    }
}

bool SaveContext::backupOnce(const std::string &file, const std::string &label)
{
    if (file.empty() || m_backedUp.contains(file)) {
        return false;
    }
    auto existing = m_recordByLabel.find(label);
    application::BackupRecord record;
    // One deflated archive per save rather than a directory of loose
    // copies. A save that removes a cue from 200 tracks used to make 200
    // durable whole-file writes here -- about 118 ms each on Linux -- and
    // leave 71.5 MB on a stick permanently; the same files are 45.5 MB in
    // an archive. The central directory is rewritten and made durable
    // after every file, so the record stays complete and readable at
    // every point a crash could happen, exactly as the loose layout was.
    // See docs/write-path-performance.md, rounds 9-16.
    if (existing == m_recordByLabel.end()) {
        record = archiveStore().backupToArchive({file}, label);
        m_recordByLabel[label] = record.id;
        m_backups.push_back({QString::fromStdString(fs::path(record.path).parent_path().string()),
                             QString::fromStdString(record.id)});
    } else {
        record = archiveStore().addToArchive(existing->second, {file});
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
