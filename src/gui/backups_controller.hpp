#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>

#include <cstdint>
#include <string>
#include <vector>

#include "application/ports/backup_store.hpp"

namespace djconvert::gui
{

// Read-only Qt list model over the backups BackupsController last listed.
class BackupListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Populated by BackupsController; not constructible from QML")

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        LabelRole,
        DescriptionRole,
        SizeHumanRole,
        SizeBytesRole,
        FileNamesRole,
    };

    explicit BackupListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRecords(std::vector<application::BackupRecord> records);

private:
    std::vector<application::BackupRecord> m_records;
};

// What a background task should do before re-listing the directory -- see
// BackupsController::startTask().
enum class BackupsAction { Load, Clean, SetDescription, Restore, Delete };

// Result of a background task -- see BackupsAction and
// BackupsController::startTask(). Built entirely on a worker thread, with
// no access to the controller: every operation ends by re-listing the
// directory, so the controller always has a fresh, consistent view once
// the task returns.
struct BackupsTaskResult
{
    std::vector<application::BackupRecord> records;
    QString totalSizeHuman;
    QString backupDir;
    QString errorMessage;  // empty on success
    QString statusMessage;
};

// Wraps FilesystemBackupStore for QML: lists the backups made under a
// stick's .djconvert-backups directory (shared across rekordbox/Engine on
// that stick, see backupDirFor() in cli/main.cpp), prunes old ones,
// restores/deletes individual ones, and edits their descriptions. Every
// operation -- even a plain list -- is disk I/O against a directory that
// can hold many, possibly large, backup copies, so (like every other
// write-capable controller) it runs on a background thread via
// QtConcurrent rather than ever blocking the UI thread.
class BackupsController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(djconvert::gui::BackupListModel *backups READ backupsModel CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString totalSizeHuman READ totalSizeHuman NOTIFY backupsChanged)
    Q_PROPERTY(QString backupDir READ backupDir NOTIFY backupsChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit BackupsController(QObject *parent = nullptr);

    BackupListModel *backupsModel() { return &m_model; }
    bool busy() const { return m_busy; }
    QString totalSizeHuman() const { return m_totalSizeHuman; }
    QString backupDir() const { return m_backupDir; }
    QString errorMessage() const { return m_errorMessage; }
    QString statusMessage() const { return m_statusMessage; }

    // rekordboxPath/enginePath: pass whichever are non-empty (at least one
    // must be) -- the backup directory is shared per stick, not per format.
    Q_INVOKABLE void load(const QString &rekordboxPath, const QString &enginePath);
    Q_INVOKABLE void clean(int keepCount);
    Q_INVOKABLE void setDescription(const QString &id, const QString &description);

    // Overwrites the backup's original files with this backup's copies
    // (the current files are themselves backed up first -- see
    // BackupStore::restore()).
    Q_INVOKABLE void restoreBackup(const QString &id);

    // Permanently deletes a single backup.
    Q_INVOKABLE void deleteBackup(const QString &id);

signals:
    void backupsChanged();
    void busyChanged();
    void errorMessageChanged();
    void statusMessageChanged();

private:
    void startTask(BackupsAction action, int keepCount, const QString &id, const QString &description);
    void onTaskFinished();
    void setBusy(bool busy);
    void setErrorMessage(const QString &message);
    void setStatusMessage(const QString &message);

    BackupListModel m_model;
    QFutureWatcher<BackupsTaskResult> m_watcher;
    QString m_rekordboxPath;
    QString m_enginePath;
    bool m_busy = false;
    QString m_totalSizeHuman;
    QString m_backupDir;
    QString m_errorMessage;
    QString m_statusMessage;
};

}  // namespace djconvert::gui
