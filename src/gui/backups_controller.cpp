#include "backups_controller.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "infrastructure/backup/filesystem_backup_store.hpp"

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
    case SizeHumanRole:
        return humanSize(record.sizeBytes);
    case SizeBytesRole:
        return QVariant::fromValue<qulonglong>(record.sizeBytes);
    default:
        return {};
    }
}

QHash<int, QByteArray> BackupListModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {LabelRole, "label"},
        {SizeHumanRole, "sizeHuman"},
        {SizeBytesRole, "sizeBytes"},
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

BackupsController::BackupsController(QObject *parent) : QObject(parent) {}

void BackupsController::load(const QString &rekordboxPath, const QString &enginePath)
{
    m_rekordboxPath = rekordboxPath;
    m_enginePath = enginePath;
    setErrorMessage({});

    try {
        std::string dir = backupDirFor(rekordboxPath, enginePath);
        m_backupDir = QString::fromStdString(dir);
        infrastructure::backup::FilesystemBackupStore store(dir);
        auto records = store.list();

        std::uint64_t total = 0;
        for (const auto &record : records) {
            total += record.sizeBytes;
        }
        m_totalSizeHuman = humanSize(total);
        m_model.setRecords(std::move(records));
    } catch (const std::exception &e) {
        setErrorMessage(QString::fromStdString(e.what()));
    }
    emit backupsChanged();
}

void BackupsController::clean(int keepCount)
{
    if (keepCount < 0) {
        return;
    }
    setErrorMessage({});
    setStatusMessage({});

    try {
        std::string dir = backupDirFor(m_rekordboxPath, m_enginePath);
        infrastructure::backup::FilesystemBackupStore store(dir);
        auto freed = store.prune(static_cast<size_t>(keepCount));
        setStatusMessage(QString("Freed %1 (kept up to %2 most recent backup(s))")
                              .arg(humanSize(freed))
                              .arg(keepCount));
    } catch (const std::exception &e) {
        setErrorMessage(QString::fromStdString(e.what()));
    }

    load(m_rekordboxPath, m_enginePath);
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
