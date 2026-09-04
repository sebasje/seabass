#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QVariantList>

#include <string>
#include <vector>

#include <memory>

#include "domain/local_restore.hpp"
#include "gui/qt_progress_reporter.hpp"
#include "gui/undo_tracking.hpp"

namespace seabass::gui
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
    // backupToComputer: tracks upserted for each format actually present on
    // the stick, -1 for a format the stick doesn't have (so the UI can tell
    // "backed up 0 tracks" apart from "this stick has no Engine data").
    // analyzeRestore: all three unused.
    int tracksAffectedRekordbox = -1;
    int tracksAffectedEngine = -1;
    int tracksAffectedOneLibrary = -1;
    int stickTrackCount = 0;
    int localTrackCount = 0;
    std::vector<domain::RestoreCandidate> candidates;
    QString errorMessage;  // empty on success
};

// Result of a background write task -- see LocalCueController::
// applyRestore()/undoLastOperation(). Built entirely on a worker thread,
// with no access to the controller. Shared by both: an undo task always
// returns an empty `backups`, which is exactly what should replace the
// controller's undo trail either way.
struct LocalCueWriteResult
{
    QString errorMessage;  // empty on success
    QString statusMessage;
    std::vector<UndoableBackup> backups;
};

// Wraps LocalCueStore for QML: backing up a stick's cues to a local
// SQLite database (application data dir, see LocalCueStore::defaultPath)
// and merging them back onto a stick -- adding whatever cues the backup
// has that the stick doesn't (a missing hot cue slot, a memory cue at a
// position nothing existing is close to), never overwriting a cue already
// there (see domain::LocalRestorePlanner::mergeCues() for the exact rule).
// Backup only ever writes to the local database; merge only ever writes to
// the stick -- mirroring SyncController's two-phase analyze/confirm/apply
// shape for the merge direction (backup needs no confirmation, since
// nothing on the stick is ever at risk from it).
class LocalCueController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(seabass::gui::RestoreCandidateListModel *restoreCandidates READ restoreCandidatesModel CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int scanCurrent READ scanCurrent NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanProgressChanged)
    Q_PROPERTY(int stickTrackCount READ stickTrackCount NOTIFY analysisChanged)
    Q_PROPERTY(int localTrackCount READ localTrackCount NOTIFY analysisChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)
    Q_PROPERTY(bool writing READ writing NOTIFY writingChanged)

public:
    explicit LocalCueController(QObject *parent = nullptr);

    RestoreCandidateListModel *restoreCandidatesModel() { return &m_model; }
    bool busy() const { return m_busy; }
    // True only while actually writing to the stick (applyRestore()/
    // undoLastOperation()) -- unlike busy(), which is also true during
    // backupToComputer() and the read-only analyze*Restore() scans, both
    // safe to interrupt (backup never touches the stick; analysis never
    // writes).
    bool writing() const { return m_writing; }
    int scanCurrent() const { return m_scanCurrent; }
    int scanTotal() const { return m_scanTotal; }
    int stickTrackCount() const { return m_stickTrackCount; }
    int localTrackCount() const { return m_localTrackCount; }
    QString errorMessage() const { return m_errorMessage; }
    QString statusMessage() const { return m_statusMessage; }
    bool canUndo() const { return !m_lastBackups.empty(); }

    // Backs up whichever of rekordboxPath/enginePath is non-empty -- both,
    // for a stick that has both formats, in one call, rather than making
    // the user switch formats and click twice (and risk only ever backing
    // up whichever format happened to be selected). Scans each present
    // side, upserts every track with cues into the local database's merged
    // "current state" (never touches the stick, so needs no confirmation),
    // and freezes an independently-restorable snapshot of each under
    // `description`.
    Q_INVOKABLE void backupToComputer(const QString &stickLabel, const QString &description,
                                       const QString &rekordboxPath, const QString &enginePath,
                                       const QString &oneLibraryPath = QString());

    // Same convention as CleanupController::hasOneLibrary()/
    // DuplicatesController::hasOneLibrary()/ScanController::hasOneLibrary().
    Q_INVOKABLE bool hasOneLibrary(const QString &pioneerRoot) const;

    // Phase 1 (read-only): scans the stick and matches it against the
    // local backup's merged "current state," proposing to merge in
    // whichever cues the backup has that the stick doesn't yet (see
    // domain::LocalRestorePlanner::mergeCues()). reportFeedback controls
    // whether the outcome (found N / found nothing / busy / failed) is
    // announced via actionFeedback -- false for the automatic calls this
    // class makes itself (page load, format switch, the silent re-scan
    // after a successful write), true for a direct user click ("Re-Analyze
    // Latest"), so an automatic background refresh never pops up a message
    // the user didn't ask for, while a button they actually pressed always
    // gets a reaction, even a no-op one.
    Q_INVOKABLE void analyzeRestore(const QString &format, const QString &path, bool reportFeedback = false);

    // Same as analyzeRestore(), but matches against one specific past
    // snapshot instead of the merged current state. Always user-initiated
    // (the "Restore From Here" button, never called automatically), so
    // always reports feedback.
    Q_INVOKABLE void analyzeSnapshotRestore(qint64 snapshotId, const QString &format, const QString &path);

    // Phase 2 (the confirmation gate) + phase 3: backs up the stick, then
    // writes every candidate currently proposed (from whichever of
    // analyzeRestore()/analyzeSnapshotRestore() ran last). Only call this
    // from a confirm dialog.
    Q_INVOKABLE void applyRestore();

    // Snapshot history management -- synchronous (a lightweight metadata
    // read/write, not a library scan).
    Q_INVOKABLE QVariantList listSnapshots();
    Q_INVOKABLE void setSnapshotDescription(qint64 id, const QString &description);
    Q_INVOKABLE bool deleteSnapshot(qint64 id);

    // Reverts every file the last applyRestore() write touched back to
    // what it was immediately before, using the backups that write made.
    // Available only right after a write (canUndo).
    Q_INVOKABLE void undoLastOperation();

signals:
    void busyChanged();
    void scanProgressChanged();
    void analysisChanged();
    void errorMessageChanged();
    void statusMessageChanged();
    void canUndoChanged();
    void writingChanged();
    // The one signal LocalCuePage.qml's popup actually listens to.
    // Unlike a Q_PROPERTY change notification (errorMessage/statusMessage
    // above), which only fires when the new value differs from the old
    // one, this fires on every single emission -- including two outcomes
    // in a row with identical text (e.g. "Restore From Here" on two
    // different snapshots that both turn out to offer nothing new) and a
    // busy-guard no-op, both of which a diffed property would risk
    // silently swallowing. isError just picks the popup's color.
    void actionFeedback(const QString &message, bool isError);

private:
    void onBackupFinished();
    void onAnalyzeFinished();
    void onWriteFinished();
    void setBusy(bool busy);
    void setWriting(bool writing);
    void setScanProgress(int current, int total);
    void setErrorMessage(const QString &message);
    void setStatusMessage(const QString &message);
    std::shared_ptr<QtProgressReporter> makeReporter();

    RestoreCandidateListModel m_model;
    QFutureWatcher<LocalCueTaskResult> m_backupWatcher;
    QFutureWatcher<LocalCueTaskResult> m_analyzeWatcher;
    QFutureWatcher<LocalCueWriteResult> m_writeWatcher;
    QString m_format;
    QString m_path;
    bool m_busy = false;
    int m_scanCurrent = 0;
    int m_scanTotal = 0;
    int m_stickTrackCount = 0;
    int m_localTrackCount = 0;
    QString m_errorMessage;
    QString m_statusMessage;
    std::vector<UndoableBackup> m_lastBackups;
    bool m_writing = false;
    // Set right before kicking off the analyze task in flight, read back
    // in onAnalyzeFinished() once it completes -- see analyzeRestore()'s
    // own doc comment on why this needs to travel with the specific
    // request, not just be inferred from context.
    bool m_analyzeReportsFeedback = false;
};

}  // namespace seabass::gui
