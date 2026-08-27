#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "domain/sync_planning.hpp"
#include "domain/waveform.hpp"
#include "gui/qt_progress_reporter.hpp"
#include "gui/undo_tracking.hpp"

namespace djconvert::gui
{

// Read-only Qt list model over the SyncPlans SyncController last computed.
// Only plans with an actual direction (ToEngine/ToRekordbox) are exposed --
// AlreadyConsistent/NoCues need no attention, matching cli/main.cpp's
// runSyncCommand's toEngine/toRekordbox split.
class SyncPlanListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Populated by SyncController; not constructible from QML")

public:
    enum Roles {
        DirectionRole = Qt::UserRole + 1,
        FilenameRole,
        DescriptionRole,
        ConflictRole,
        TracksRole,
    };

    explicit SyncPlanListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // waveformsByKey is best-effort, precomputed by the caller, keyed
    // "rb:"+sourceId / "en:"+sourceId (rekordbox and Engine each have
    // their own independent sourceId space, so the format prefix avoids
    // collisions -- see DuplicatesController::runRescanTask for the
    // single-format equivalent that doesn't need this).
    void setPlans(std::vector<domain::SyncPlan> plans,
                  std::unordered_map<std::string, std::vector<domain::WaveformColumn>> waveformsByKey = {});
    const std::vector<domain::SyncPlan> &plans() const { return m_plans; }

private:
    std::vector<domain::SyncPlan> m_plans;
    std::unordered_map<std::string, std::vector<domain::WaveformColumn>> m_waveformsByKey;
};

// Result of a background analyze task -- see SyncController::analyze().
// Built entirely on a worker thread, with no access to the controller.
struct SyncTaskResult
{
    std::vector<domain::SyncPlan> plans;
    std::unordered_map<std::string, std::vector<domain::WaveformColumn>> waveformsByKey;
    int rekordboxTrackCount = 0;
    int engineTrackCount = 0;
    QString errorMessage;  // empty on success
};

// Result of a background write task -- see SyncController::apply()/
// applyOne()/undoLastOperation(). Built entirely on a worker thread, with
// no access to the controller. Shared by both apply and undo: an undo
// task always returns an empty `backups` (there is nothing left to undo
// once it's done), which is exactly what should replace the controller's
// undo trail either way.
struct SyncWriteResult
{
    QString errorMessage;  // empty on success
    QString statusMessage;
    std::vector<UndoableBackup> backups;
};

// Wraps SyncLibraries for QML: two-phase, non-destructive sync between a
// stick's rekordbox and Engine libraries, mirroring cli/main.cpp's
// runSyncCommand exactly -- analyze() only ever reads (see
// domain::TrackMatcher / domain::SyncPlanner), apply() is the single
// confirmation gate the QML confirm dialog calls into.
class SyncController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(djconvert::gui::SyncPlanListModel *plans READ plansModel CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int scanCurrent READ scanCurrent NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanProgressChanged)
    Q_PROPERTY(int rekordboxTrackCount READ rekordboxTrackCount NOTIFY analysisChanged)
    Q_PROPERTY(int engineTrackCount READ engineTrackCount NOTIFY analysisChanged)
    Q_PROPERTY(int toEngineCount READ toEngineCount NOTIFY analysisChanged)
    Q_PROPERTY(int toRekordboxCount READ toRekordboxCount NOTIFY analysisChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)
    Q_PROPERTY(bool writing READ writing NOTIFY writingChanged)

public:
    explicit SyncController(QObject *parent = nullptr);

    SyncPlanListModel *plansModel() { return &m_model; }
    bool busy() const { return m_busy; }
    // True only while actually writing to the stick (apply()/applyOne()/
    // undoLastOperation()) -- unlike busy(), which is also true during the
    // read-only analyze() scan, which is safe to interrupt.
    bool writing() const { return m_writing; }
    int scanCurrent() const { return m_scanCurrent; }
    int scanTotal() const { return m_scanTotal; }
    int rekordboxTrackCount() const { return m_rekordboxTrackCount; }
    int engineTrackCount() const { return m_engineTrackCount; }
    int toEngineCount() const { return m_toEngineCount; }
    int toRekordboxCount() const { return m_toRekordboxCount; }
    QString errorMessage() const { return m_errorMessage; }
    QString statusMessage() const { return m_statusMessage; }
    bool canUndo() const { return !m_lastBackups.empty(); }

    // Phase 1: read-only. rekordboxPath/enginePath are the stick's
    // DetectedStick.rekordboxPath / .enginePath.
    Q_INVOKABLE void analyze(const QString &rekordboxPath, const QString &enginePath);

    // Phase 2 (the confirmation gate) + phase 3: writes every plan
    // currently in the model. Only call this from a confirm dialog.
    Q_INVOKABLE void apply();

    // Same as apply(), scoped to the single plan at index -- lets a track
    // be synced on its own without waiting on (or being blocked by) every
    // other matched track.
    Q_INVOKABLE void applyOne(int index);

    // Reverts every file the last apply()/applyOne() touched back to what
    // it was immediately before that write -- using the very backups that
    // write made, restored via FilesystemBackupStore::restore(). Available
    // only right after a write (canUndo), and only once: a fresh apply
    // clears the trail.
    Q_INVOKABLE void undoLastOperation();

signals:
    void busyChanged();
    void scanProgressChanged();
    void analysisChanged();
    void errorMessageChanged();
    void statusMessageChanged();
    void canUndoChanged();
    void writingChanged();

private:
    void onAnalyzeFinished();
    void onWriteFinished();
    void setBusy(bool busy);
    void setWriting(bool writing);
    void setScanProgress(int current, int total);
    void setErrorMessage(const QString &message);
    void setStatusMessage(const QString &message);
    void recomputeDirectionCounts();
    void startApply(std::vector<domain::SyncPlan> toEngine, std::vector<domain::SyncPlan> toRekordbox);
    std::shared_ptr<QtProgressReporter> makeReporter();

    SyncPlanListModel m_model;
    QFutureWatcher<SyncTaskResult> m_watcher;
    QFutureWatcher<SyncWriteResult> m_writeWatcher;
    QString m_rekordboxPath;
    QString m_enginePath;
    bool m_busy = false;
    int m_scanCurrent = 0;
    int m_scanTotal = 0;
    int m_rekordboxTrackCount = 0;
    int m_engineTrackCount = 0;
    int m_toEngineCount = 0;
    int m_toRekordboxCount = 0;
    QString m_errorMessage;
    QString m_statusMessage;
    std::vector<UndoableBackup> m_lastBackups;
    bool m_writing = false;
};

}  // namespace djconvert::gui
