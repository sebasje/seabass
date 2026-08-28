#include "backups_controller.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "gui/write_guard.hpp"
#include "infrastructure/backup/filesystem_backup_store.hpp"
#include "infrastructure/backup/stick_write_lock.hpp"

namespace djconvert::gui
{

namespace fs = std::filesystem;

namespace
{

// Mirrors cli/main.cpp's humanSize() exactly.
QString humanSize(std::uint64_t bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units)) {
        value /= 1024.0;
        unit++;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f %s", value, units[unit]);
    return QString::fromUtf8(buf);
}

// Mirrors cli/main.cpp's backupDirFor(): backups live under
// <stick root>/.djconvert-backups, shared across formats.
std::string backupDirFor(const QString &rekordboxPath, const QString &enginePath)
{
    fs::path anyPath = !enginePath.isEmpty() ? fs::path(enginePath.toStdString())
                                              : fs::path(rekordboxPath.toStdString());
    return (anyPath.parent_path() / ".djconvert-backups").string();
}

}  // namespace

BackupListModel::BackupListModel(QObject *parent) : QAbstractListModel(parent) {}

int BackupListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_records.size());
}

QVariant BackupListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<size_t>(index.row()) >= m_records.size()) {
        return {};
    }
    const auto &record = m_records[static_cast<size_t>(index.row())];
    switch (role) {
    case IdRole:
        return QString::fromStdString(record.id);
    case LabelRole:
        return QString::fromStdString(record.label);
    case DescriptionRole:
        return QString::fromStdString(record.description);
    case SizeHumanRole:
        return humanSize(record.sizeBytes);
    case SizeBytesRole:
        return QVariant::fromValue<qulonglong>(record.sizeBytes);
    case FileNamesRole: {
        // Basenames only, not full paths -- this is a "what did this
        // backup touch" glance (e.g. is OneLibrary's exportLibrary.db in
        // here alongside export.pdb?), not a restore-path browser.
        QStringList names;
        for (const auto &path : record.filePaths) {
            names << QString::fromStdString(fs::path(path).filename().string());
        }
        return names;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> BackupListModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {LabelRole, "label"},
        {DescriptionRole, "description"},
        {SizeHumanRole, "sizeHuman"},
        {SizeBytesRole, "sizeBytes"},
        {FileNamesRole, "fileNames"},
    };
}

void BackupListModel::setRecords(std::vector<application::BackupRecord> records)
{
    // Newest first -- matches the natural reading order for "what did I
    // just back up," and id sorts chronologically (see BackupRecord's
    // doc comment), so a plain reverse-lexicographic sort is enough.
    std::sort(records.begin(), records.end(),
              [](const auto &a, const auto &b) { return a.id > b.id; });
    beginResetModel();
    m_records = std::move(records);
    endResetModel();
}

namespace
{

// Runs entirely on a background thread (see BackupsController::startTask())
// -- no access to the controller itself. Every action re-lists the
// directory afterward so the caller always gets a fresh, consistent view.
BackupsTaskResult runBackupsTask(BackupsAction action, QString dir, int keepCount, QString id,
                                  QString description)
{
    BackupsTaskResult result;
    bool mutating = action != BackupsAction::Load;
    if (mutating) {
        QString refusal = refuseIfRekordboxRunning();
        if (!refusal.isEmpty()) {
            result.errorMessage = refusal;
            return result;
        }
    }
    try {
        // Held for every action, including Load -- reading this directory
        // while another writer is mid-Clean/Restore/Delete could otherwise
        // see a half-mutated view.
        infrastructure::backup::StickWriteLock lock(dir.toStdString() + "/.write.lock");

        infrastructure::backup::FilesystemBackupStore store(dir.toStdString());
        switch (action) {
        case BackupsAction::Clean: {
            auto freed = store.prune(static_cast<size_t>(keepCount));
            result.statusMessage =
                QString("Freed %1 (kept up to %2 most recent backup(s))").arg(humanSize(freed)).arg(keepCount);
            break;
        }
        case BackupsAction::SetDescription:
            store.setDescription(id.toStdString(), description.toStdString());
            break;
        case BackupsAction::Restore:
            if (store.restore(id.toStdString())) {
                result.statusMessage = "Restored -- the files that were overwritten have their own new backup.";
            } else {
                result.errorMessage = "Could not restore -- this backup predates restore support or its files are gone.";
            }
            break;
        case BackupsAction::Delete:
            if (!store.remove(id.toStdString())) {
                result.errorMessage = "Could not delete that backup.";
            }
            break;
        case BackupsAction::Load:
            break;
        }

        auto records = store.list();
        std::uint64_t total = 0;
        for (const auto &record : records) {
            total += record.sizeBytes;
        }
        result.totalSizeHuman = humanSize(total);
        result.backupDir = dir;
        result.records = std::move(records);
    } catch (const std::exception &e) {
        result.errorMessage = QString::fromStdString(e.what());
    }
    return result;
}

}  // namespace

BackupsController::BackupsController(QObject *parent) : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<BackupsTaskResult>::finished, this, &BackupsController::onTaskFinished);
}

void BackupsController::load(const QString &rekordboxPath, const QString &enginePath)
{
    if (m_busy) {
        return;
    }
    m_rekordboxPath = rekordboxPath;
    m_enginePath = enginePath;
    startTask(BackupsAction::Load, 0, {}, {});
}

void BackupsController::clean(int keepCount)
{
    if (m_busy || keepCount < 0) {
        return;
    }
    startTask(BackupsAction::Clean, keepCount, {}, {});
}

void BackupsController::setDescription(const QString &id, const QString &description)
{
    if (m_busy) {
        return;
    }
    startTask(BackupsAction::SetDescription, 0, id, description);
}

void BackupsController::restoreBackup(const QString &id)
{
    if (m_busy) {
        return;
    }
    startTask(BackupsAction::Restore, 0, id, {});
}

void BackupsController::deleteBackup(const QString &id)
{
    if (m_busy) {
        return;
    }
    startTask(BackupsAction::Delete, 0, id, {});
}

void BackupsController::startTask(BackupsAction action, int keepCount, const QString &id, const QString &description)
{
    setErrorMessage({});
    setStatusMessage({});
    setBusy(true);
    std::string dir = backupDirFor(m_rekordboxPath, m_enginePath);
    m_watcher.setFuture(QtConcurrent::run(runBackupsTask, action, QString::fromStdString(dir), keepCount, id, description));
}

void BackupsController::onTaskFinished()
{
    BackupsTaskResult result = m_watcher.result();
    m_backupDir = result.backupDir;
    m_totalSizeHuman = result.totalSizeHuman;
    m_model.setRecords(std::move(result.records));
    if (!result.errorMessage.isEmpty()) {
        setErrorMessage(result.errorMessage);
    }
    if (!result.statusMessage.isEmpty()) {
        setStatusMessage(result.statusMessage);
    }
    emit backupsChanged();
    setBusy(false);
}

void BackupsController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void BackupsController::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    emit errorMessageChanged();
}

void BackupsController::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) {
        return;
    }
    m_statusMessage = message;
    emit statusMessageChanged();
}

}  // namespace djconvert::gui
