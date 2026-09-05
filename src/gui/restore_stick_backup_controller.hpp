#pragma once

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

#include "application/ports/cancellation_token.hpp"
#include "application/use_cases/restore_stick_backup.hpp"

namespace seabass::gui
{

// Wraps application::RestoreStickBackup for RestoreStickBackupPage.qml --
// the top-level "Restore a Stick Backup" flow (pick a backup file, pick a
// mounted drive, see what will happen, confirm). `disks` is a plain
// QVariantList like FormatUsbController's, for the same testability
// reason; a drive is a valid target only when mounted with a filesystem.
class RestoreStickBackupController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList disks READ disks NOTIFY disksChanged)
    Q_PROPERTY(QString archivePath READ archivePath WRITE setArchivePath NOTIFY archivePathChanged)
    Q_PROPERTY(QString defaultBackupDirectory READ defaultBackupDirectory WRITE setDefaultBackupDirectory NOTIFY
                   defaultBackupDirectoryChanged)
    Q_PROPERTY(QVariantMap archiveInfo READ archiveInfo NOTIFY archiveInfoChanged)
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY previewChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool restoring READ restoring NOTIFY busyChanged)
    Q_PROPERTY(bool analyzing READ analyzing NOTIFY busyChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY progressChanged)
    Q_PROPERTY(qlonglong filesDone READ filesDone NOTIFY progressChanged)
    Q_PROPERTY(qlonglong filesTotal READ filesTotal NOTIFY progressChanged)
    Q_PROPERTY(qlonglong bytesDone READ bytesDone NOTIFY progressChanged)
    Q_PROPERTY(qlonglong bytesTotal READ bytesTotal NOTIFY progressChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY progressChanged)
    Q_PROPERTY(QVariantMap result READ result NOTIFY resultChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit RestoreStickBackupController(QObject *parent = nullptr);
    ~RestoreStickBackupController() override;

    QVariantList disks() const { return m_disks; }
    QString archivePath() const { return m_archivePath; }
    void setArchivePath(const QString &path);
    QString defaultBackupDirectory() const { return m_defaultBackupDirectory; }
    void setDefaultBackupDirectory(const QString &directory);
    QVariantMap archiveInfo() const { return m_archiveInfo; }
    QVariantMap preview() const { return m_preview; }
    bool busy() const { return m_restoring || m_analyzing; }
    bool restoring() const { return m_restoring; }
    bool analyzing() const { return m_analyzing; }
    QString phase() const { return m_phase; }
    qlonglong filesDone() const { return m_filesDone; }
    qlonglong filesTotal() const { return m_filesTotal; }
    qlonglong bytesDone() const { return m_bytesDone; }
    qlonglong bytesTotal() const { return m_bytesTotal; }
    QString currentFile() const { return m_currentFile; }
    QVariantMap result() const { return m_result; }
    QString errorMessage() const { return m_errorMessage; }
    QString statusMessage() const { return m_statusMessage; }

    // Re-enumerates removable drives (synchronous, cheap).
    Q_INVOKABLE void refresh();
    // Reads the archive's manifest (background) and, when `targetRoot` is
    // a directory, what restoring onto it would do.
    Q_INVOKABLE void analyze(const QString &targetRoot);
    Q_INVOKABLE void restore(const QString &targetRoot, bool exact);
    Q_INVOKABLE void cancel();
    // Full path of the archive a stick with this label would have in the
    // default directory -- the per-stick page's shortcut.
    Q_INVOKABLE QString archivePathForLabel(const QString &label) const;

signals:
    void disksChanged();
    void archivePathChanged();
    void defaultBackupDirectoryChanged();
    void archiveInfoChanged();
    void previewChanged();
    void busyChanged();
    void progressChanged();
    void resultChanged();
    void errorMessageChanged();
    void statusMessageChanged();
    void actionFeedback(const QString &message, bool isError);

private:
    struct AnalyzeResult;
    struct RestoreResult;

    void onAnalyzeFinished();
    void onRestoreFinished();
    void applyProgress(const application::RestoreProgress &progress);
    void setErrorMessage(const QString &message);
    void setStatusMessage(const QString &message);

    QVariantList m_disks;
    QString m_archivePath;
    QString m_defaultBackupDirectory;
    QVariantMap m_archiveInfo;
    QVariantMap m_preview;
    QVariantMap m_result;
    bool m_restoring = false;
    bool m_analyzing = false;
    QString m_phase;
    qlonglong m_filesDone = 0;
    qlonglong m_filesTotal = 0;
    qlonglong m_bytesDone = 0;
    qlonglong m_bytesTotal = 0;
    QString m_currentFile;
    QString m_errorMessage;
    QString m_statusMessage;
    application::CancellationToken m_cancel;
    QFutureWatcher<std::shared_ptr<AnalyzeResult>> m_analyzeWatcher;
    QFutureWatcher<std::shared_ptr<RestoreResult>> m_restoreWatcher;
};

}  // namespace seabass::gui
