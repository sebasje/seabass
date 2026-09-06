#pragma once

#include <QFutureWatcher>
#include <QMap>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantMap>

#include <memory>
#include <vector>

namespace seabass::gui
{

// Per-stick backup advice for the stick list: for each mounted stick,
// which backup in the backup folder it relates to and what to offer
// (update it, restore it, back up fresh). Decided by
// application::adviseStickBackup from the library's content fingerprint,
// the stick's hardware identifier and its label; everything is gathered
// on a worker thread, one stick at a time.
class BackupAdvisorController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString backupDirectory READ backupDirectory WRITE setBackupDirectory NOTIFY backupDirectoryChanged)
    // mountPoint -> {state, matchedBy, backupPath, backupLabel,
    // backupCreatedAt, trackOverlap, cueOverlap, detail}; see
    // StickBackupAdvice for the state and matchedBy values.
    Q_PROPERTY(QVariantMap advice READ advice NOTIFY adviceChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit BackupAdvisorController(QObject *parent = nullptr);
    ~BackupAdvisorController() override;

    QString backupDirectory() const { return m_backupDirectory; }
    void setBackupDirectory(const QString &directory);
    QVariantMap advice() const { return m_advice; }
    bool busy() const { return m_watcher.isRunning(); }

    // Queues an assessment; the result lands in advice[mountPoint].
    Q_INVOKABLE void assess(const QString &stickLabel, const QString &mountPoint, const QString &rekordboxPath,
                            const QString &enginePath);
    // Re-runs every assessment made so far (after a backup or restore, or
    // when the stick list is shown again).
    Q_INVOKABLE void reassessAll();
    Q_INVOKABLE void forget(const QString &mountPoint);

signals:
    void backupDirectoryChanged();
    void adviceChanged();
    void busyChanged();

private:
    struct Request
    {
        QString stickLabel;
        QString mountPoint;
        QString rekordboxPath;
        QString enginePath;
    };
    struct Result;

    void startNext();
    void onFinished();

    QString m_backupDirectory;
    QVariantMap m_advice;
    QMap<QString, Request> m_known;  // by mountPoint
    std::vector<Request> m_queue;
    QFutureWatcher<std::shared_ptr<Result>> m_watcher;
};

}  // namespace seabass::gui
