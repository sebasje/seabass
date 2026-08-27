#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>

#include <string>
#include <vector>

#include "domain/local_restore.hpp"

namespace djconvert::gui
{

// Read-only Qt list model over the RestoreCandidates LocalCueController
// last computed.
class RestoreCandidateListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Populated by LocalCueController; not constructible from QML")

public:
    enum Roles {
        FilenameRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        DescriptionRole,
    };

    explicit RestoreCandidateListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setCandidates(std::vector<domain::RestoreCandidate> candidates);
    const std::vector<domain::RestoreCandidate> &candidates() const { return m_candidates; }

private:
    std::vector<domain::RestoreCandidate> m_candidates;
};

// Result of a background task -- see LocalCueController::backupToComputer()
// / analyzeRestore(). Built entirely on a worker thread.
struct LocalCueTaskResult
{
    // backupToComputer: number of tracks upserted. analyzeRestore: unused.
    int tracksAffected = 0;
    int stickTrackCount = 0;
    int localTrackCount = 0;
    std::vector<domain::RestoreCandidate> candidates;
    QString errorMessage;  // empty on success
};

// Wraps LocalCueStore for QML: backing up a stick's cues to a local
// SQLite database (application data dir, see LocalCueStore::defaultPath)
// and restoring them back to a stick whose cues were lost or
// overwritten. Backup only ever writes to the local database; restore
// only ever writes to the stick -- mirroring SyncController's two-phase
// analyze/confirm/apply shape for the restore direction (backup needs no
// confirmation, since nothing on the stick is ever at risk from it).
class LocalCueController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(djconvert::gui::RestoreCandidateListModel *restoreCandidates READ restoreCandidatesModel CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int scanCurrent READ scanCurrent NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanProgressChanged)
    Q_PROPERTY(int stickTrackCount READ stickTrackCount NOTIFY analysisChanged)
    Q_PROPERTY(int localTrackCount READ localTrackCount NOTIFY analysisChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit LocalCueController(QObject *parent = nullptr);

    RestoreCandidateListModel *restoreCandidatesModel() { return &m_model; }
    bool busy() const { return m_busy; }
    int scanCurrent() const { return m_scanCurrent; }
    int scanTotal() const { return m_scanTotal; }
    int stickTrackCount() const { return m_stickTrackCount; }
    int localTrackCount() const { return m_localTrackCount; }
    QString errorMessage() const { return m_errorMessage; }
    QString statusMessage() const { return m_statusMessage; }

    // format is "rekordbox" or "engine"; path is the corresponding
    // DetectedStick.rekordboxPath / .enginePath. Scans the stick and
    // upserts every track with cues into the local database -- never
    // touches the stick, so needs no confirmation.
    Q_INVOKABLE void backupToComputer(const QString &format, const QString &path, const QString &stickLabel);

    // Phase 1 (read-only): scans the stick and matches it against the
    // local backup, proposing a restore only for stick tracks that
    // currently have zero cues (see domain::LocalRestorePlanner).
    Q_INVOKABLE void analyzeRestore(const QString &format, const QString &path);

    // Phase 2 (the confirmation gate) + phase 3: backs up the stick, then
    // writes every candidate currently proposed. Only call this from a
    // confirm dialog.
    Q_INVOKABLE void applyRestore();

signals:
    void busyChanged();
    void scanProgressChanged();
    void analysisChanged();
    void errorMessageChanged();
    void statusMessageChanged();

private:
    void onBackupFinished();
    void onAnalyzeFinished();
    void setBusy(bool busy);
    void setScanProgress(int current, int total);
    void setErrorMessage(const QString &message);
    void setStatusMessage(const QString &message);

    RestoreCandidateListModel m_model;
    QFutureWatcher<LocalCueTaskResult> m_backupWatcher;
    QFutureWatcher<LocalCueTaskResult> m_analyzeWatcher;
    QString m_format;
    QString m_path;
    bool m_busy = false;
    int m_scanCurrent = 0;
    int m_scanTotal = 0;
    int m_stickTrackCount = 0;
    int m_localTrackCount = 0;
    QString m_errorMessage;
    QString m_statusMessage;
};

}  // namespace djconvert::gui
