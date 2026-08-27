#pragma once

#include <QAbstractListModel>
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
        SizeHumanRole,
        SizeBytesRole,
    };

    explicit BackupListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRecords(std::vector<application::BackupRecord> records);

private:
    std::vector<application::BackupRecord> m_records;
};

// Wraps FilesystemBackupStore for QML: lists the backups made under a
// stick's .djconvert-backups directory (shared across rekordbox/Engine on
// that stick, see backupDirFor() in cli/main.cpp) and prunes old ones.
// Both operations are fast directory scans, not library parses, so --
// unlike Scan/Duplicates/Sync -- this runs synchronously on the UI thread,
// matching how lightly cli/main.cpp's own runBackupsCommand treats it.
class BackupsController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(djconvert::gui::BackupListModel *backups READ backupsModel CONSTANT)
    Q_PROPERTY(QString totalSizeHuman READ totalSizeHuman NOTIFY backupsChanged)
    Q_PROPERTY(QString backupDir READ backupDir NOTIFY backupsChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit BackupsController(QObject *parent = nullptr);

    BackupListModel *backupsModel() { return &m_model; }
    QString totalSizeHuman() const { return m_totalSizeHuman; }
    QString backupDir() const { return m_backupDir; }
    QString errorMessage() const { return m_errorMessage; }
    QString statusMessage() const { return m_statusMessage; }

    // rekordboxPath/enginePath: pass whichever are non-empty (at least one
    // must be) -- the backup directory is shared per stick, not per format.
    Q_INVOKABLE void load(const QString &rekordboxPath, const QString &enginePath);
    Q_INVOKABLE void clean(int keepCount);

signals:
    void backupsChanged();
    void errorMessageChanged();
    void statusMessageChanged();

private:
    void setErrorMessage(const QString &message);
    void setStatusMessage(const QString &message);

    BackupListModel m_model;
    QString m_rekordboxPath;
    QString m_enginePath;
    QString m_totalSizeHuman;
    QString m_backupDir;
    QString m_errorMessage;
    QString m_statusMessage;
};

}  // namespace djconvert::gui
