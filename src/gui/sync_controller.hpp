#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "domain/cross_source_sync_conflict.hpp"
#include "domain/sync_planning.hpp"
#include "domain/track_scope.hpp"
#include "gui/qt_progress_reporter.hpp"
#include "gui/undo_tracking.hpp"

namespace seabass::gui
{

// Read-only Qt list model over the SyncPlans SyncController last computed,
// across every pair of catalogs actually present on the stick (rekordbox
// <->Engine, rekordbox<->OneLibrary, Engine<->OneLibrary -- see
// SyncController's own class comment). Only plans with an actual
// direction are exposed; AlreadyConsistent/NoCues need no attention.
class SyncPlanListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Populated by SyncController; not constructible from QML")

public:
    enum Roles {
        // "rekordbox"/"engine"/"onelibrary" -- which catalog the cues are
        // coming from/going to for this specific plan. Read off the
        // matched tracks' own Track::format rather than assuming a fixed
        // pair, so the same role works for any of the three pairs.
        SourceFormatRole = Qt::UserRole + 1,
        TargetFormatRole,
        FilenameRole,
        DescriptionRole,
        ConflictRole,
        TracksRole,
    };

    explicit SyncPlanListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Waveforms are NOT precomputed here -- see this class's own history:
    // this used to take a waveformsByKey map built eagerly (one file read
    // per actionable track) during the whole analyze() scan, which for a
    // stick where most of the library is actionable meant thousands of
    // individual reads against removable media before the user had even
    // looked at a single row -- confirmed as the actual cause of a real
    // "scanning takes forever" report. QML now fetches a given track's
    // waveform on demand via PlaybackController::waveformFor(), the same
    // already-proven pattern LibraryConsistencyPage's own track cards use
    // -- only the rows actually rendered (ListView's own virtualization)
    // ever pay for a waveform read at all.
    void setPlans(std::vector<domain::SyncPlan> plans);
    const std::vector<domain::SyncPlan> &plans() const { return m_plans; }

    // Appends one plan without disturbing the rest -- for a manually-
    // resolved cross-source conflict (see SyncController::
    // resolveConflict()) becoming an ordinary, immediately-appliable plan.
    void addPlan(domain::SyncPlan plan);
    // Removes one plan without a full rescan -- for SyncController::
    // applyOne() after a successful write: that one plan is now
    // consistent, nothing else in the model could have changed (see
    // SyncController::onWriteFinished()'s own comment on why).
    void removePlanAt(int index);

private:
    std::vector<domain::SyncPlan> m_plans;
};

// Result of a background analyze task, see SyncController::analyze().
// Built entirely on a worker thread, with no access to the controller.
struct SyncTaskResult
{
    std::vector<domain::SyncPlan> plans;  // combined across every present pair, actionable only, conflict-free
    std::vector<domain::CrossSourceSyncConflict> conflicts;  // see CrossSourceConflictDetector::detect()
    int rekordboxTrackCount = 0;
    int engineTrackCount = 0;
    int oneLibraryTrackCount = 0;  // 0 when this stick has no OneLibrary export
    // Union of playlist names across every catalog scanned, built from the
    // *unfiltered* scan regardless of which TrackScope analyze() was asked
    // for -- so picking a playlist never shrinks the picker's own list of
    // choices. Same shape as ScanController's own playlistNames/
    // playlistTrackCounts.
    QStringList playlistNames;
    QVariantMap playlistTrackCounts;
    QString errorMessage;  // empty on success
};

// Result of a background write task, see SyncController::apply()/
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

// Wraps SyncLibraries for QML: two-phase, non-destructive sync across
// every pair of catalogs actually present on a stick. Originally this
// only ever compared rekordbox against Engine, with OneLibrary bolted on
// as a one-directional best-effort mirror whenever a write landed on
// rekordbox; that meant OneLibrary's own cues (if it had any rekordbox/
// Engine didn't) never propagated anywhere, and the mirror only fired for
// one of the three possible pairs. Every pair now gets the exact same
// real diff+direction treatment (domain::TrackMatcher / domain::
// SyncPlanner, matching primarily by exact resolved file path -- the
// same physical file on the same stick, format-agnostic and far more
// reliable than title+artist+duration), and all three pairs' actionable
// plans are combined into one list. analyze() only ever reads, apply()
// is the single confirmation gate the QML confirm dialog calls into.
class SyncController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(seabass::gui::SyncPlanListModel *plans READ plansModel CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int scanCurrent READ scanCurrent NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanProgressChanged)
    Q_PROPERTY(int rekordboxTrackCount READ rekordboxTrackCount NOTIFY analysisChanged)
    Q_PROPERTY(int engineTrackCount READ engineTrackCount NOTIFY analysisChanged)
    Q_PROPERTY(int oneLibraryTrackCount READ oneLibraryTrackCount NOTIFY analysisChanged)
    // Backs the Playlist picker in SyncPage.qml -- same shape/convention as
    // ScanController's own playlistNames/playlistTrackCounts (index 0 of
    // ["All tracks"] + these is the "no filter" choice; see
    // PlaylistListView.qml).
    Q_PROPERTY(QStringList playlistNames READ playlistNames NOTIFY analysisChanged)
    Q_PROPERTY(QVariantMap playlistTrackCounts READ playlistTrackCounts NOTIFY analysisChanged)
    // One entry per (sourceFormat, targetFormat, count) actually present
    // among the current plans, e.g. [{sourceFormat:"engine",
    // targetFormat:"rekordbox", count:12}, ...] -- replaces the old fixed
    // toEngineCount/toRekordboxCount pair, which had no way to represent
    // a third catalog's own counts.
    Q_PROPERTY(QVariantList directionCounts READ directionCounts NOTIFY analysisChanged)
    // One entry per still-unresolved domain::CrossSourceSyncConflict --
    // two different pairs proposing genuinely different cues to the same
    // target track, which SyncPlanner alone can't detect (it only ever
    // sees two catalogs at a time). See resolveConflict(). Never
    // includes plans already in `plans` -- a target stays out of the
    // appliable list entirely until its conflict here is resolved.
    Q_PROPERTY(QVariantList unresolvedConflicts READ unresolvedConflicts NOTIFY conflictsChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)
    Q_PROPERTY(bool writing READ writing NOTIFY writingChanged)

public:
    explicit SyncController(QObject *parent = nullptr);

    SyncPlanListModel *plansModel() { return &m_model; }
    bool busy() const { return m_busy; }
    // True only while actually writing to the stick (apply()/applyOne()/
    // undoLastOperation()), unlike busy(), which is also true during the
    // read-only analyze() scan, which is safe to interrupt.
    bool writing() const { return m_writing; }
    int scanCurrent() const { return m_scanCurrent; }
    int scanTotal() const { return m_scanTotal; }
    int rekordboxTrackCount() const { return m_rekordboxTrackCount; }
    int engineTrackCount() const { return m_engineTrackCount; }
    int oneLibraryTrackCount() const { return m_oneLibraryTrackCount; }
    QStringList playlistNames() const { return m_playlistNames; }
    QVariantMap playlistTrackCounts() const { return m_playlistTrackCounts; }
    QVariantList directionCounts() const { return m_directionCounts; }
    QVariantList unresolvedConflicts() const { return m_unresolvedConflicts; }
    QString errorMessage() const { return m_errorMessage; }
    QString statusMessage() const { return m_statusMessage; }
    bool canUndo() const { return !m_lastBackups.empty(); }

    // Phase 1: read-only. rekordboxPath/enginePath are the stick's
    // DetectedStick.rekordboxPath / .enginePath (either may be empty if
    // that catalog isn't present); OneLibrary is picked up automatically
    // whenever exportLibrary.db exists under rekordboxPath, same
    // convention as every other feature in this app. playlistName empty
    // (the default) analyzes/syncs the whole library, same as before this
    // parameter existed; a real name scopes matching, the plan list, and
    // rekordboxTrackCount/engineTrackCount/oneLibraryTrackCount to just
    // that playlist's tracks -- playlistNames/playlistTrackCounts
    // themselves stay unfiltered so the picker never shrinks its own
    // choices.
    // searchQuery empty (the default) applies no text filter; a real query
    // narrows by case-insensitive title/artist substring match, same rule
    // ScanController's own search box uses, applied on top of
    // playlistName (both can narrow at once, same as ScanPage's own
    // playlist+search combination).
    Q_INVOKABLE void analyze(const QString &rekordboxPath, const QString &enginePath,
                              const QString &playlistName = QString(), const QString &searchQuery = QString());

    // Phase 2 (the confirmation gate) + phase 3: writes every plan
    // currently in the model, across every pair. Only call this from a
    // confirm dialog.
    Q_INVOKABLE void apply();

    // Same as apply(), scoped to the single plan at index, lets a track
    // be synced on its own without waiting on (or being blocked by) every
    // other matched track.
    Q_INVOKABLE void applyOne(int index);

    // Reverts every file the last apply()/applyOne() touched back to what
    // it was immediately before that write, using the very backups that
    // write made, restored via FilesystemBackupStore::restore(). Available
    // only right after a write (canUndo), and only once: a fresh apply
    // clears the trail.
    Q_INVOKABLE void undoLastOperation();

    // Picks one side of unresolvedConflicts[index] as the winner: turns
    // it into an ordinary actionable plan (added to `plans`, so it's
    // immediately appliable and counted in directionCounts like any
    // other) and removes it from unresolvedConflicts. useSourceA selects
    // CrossSourceSyncConflict::sourceA/cuesFromA when true, sourceB/
    // cuesFromB when false.
    Q_INVOKABLE void resolveConflict(int index, bool useSourceA);

signals:
    void busyChanged();
    void scanProgressChanged();
    void analysisChanged();
    void conflictsChanged();
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
    void rebuildUnresolvedConflictsList();
    void startApply(std::vector<domain::SyncPlan> plans);
    std::shared_ptr<QtProgressReporter> makeReporter();

    SyncPlanListModel m_model;
    QFutureWatcher<SyncTaskResult> m_watcher;
    QFutureWatcher<SyncWriteResult> m_writeWatcher;
    QString m_rekordboxPath;
    QString m_enginePath;
    // The playlistName analyze() was last called with -- so the automatic
    // re-analyze onWriteFinished() runs after every apply()/applyOne()/
    // undoLastOperation() stays scoped to whatever playlist was selected,
    // instead of silently reverting to "All tracks".
    QString m_currentPlaylistName;
    // Same reasoning as m_currentPlaylistName -- the search box's own text
    // must also survive the automatic post-write re-analyze.
    QString m_currentSearchQuery;
    // What onWriteFinished() should do once the write completes, set by
    // whichever public write method just kicked it off. apply()/
    // applyOne() write only plans already in the model -- domain::
    // SyncPlanner classifies each match independently and
    // CrossSourceConflictDetector::detect() already ran once at analyze()
    // time, so applying one plan can't change any other plan's
    // classification (see onWriteFinished()'s own comment) -- a full
    // re-analyze is never needed for those two. undoLastOperation()
    // restores prior file bytes this session already discarded the old
    // plan list for, so it keeps doing a real re-analyze.
    enum class PendingWriteKind { ApplyAll, ApplyOne, Undo };
    PendingWriteKind m_pendingWriteKind = PendingWriteKind::Undo;
    // Valid only when m_pendingWriteKind is ApplyOne: the row applyOne()
    // was called with, removed from m_model once the write succeeds.
    int m_pendingApplyOneIndex = -1;
    bool m_busy = false;
    int m_scanCurrent = 0;
    int m_scanTotal = 0;
    int m_rekordboxTrackCount = 0;
    int m_engineTrackCount = 0;
    int m_oneLibraryTrackCount = 0;
    QStringList m_playlistNames;
    QVariantMap m_playlistTrackCounts;
    QVariantList m_directionCounts;
    std::vector<domain::CrossSourceSyncConflict> m_conflicts;
    QVariantList m_unresolvedConflicts;
    QString m_errorMessage;
    QString m_statusMessage;
    std::vector<UndoableBackup> m_lastBackups;
    bool m_writing = false;
};

}  // namespace seabass::gui
