#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantMap>

#include <memory>
#include <utility>
#include <vector>

#include "domain/junk_cue.hpp"
#include "domain/library_consistency.hpp"
#include "gui/qt_progress_reporter.hpp"

namespace seabass::gui
{

// Read-only Qt list model over the LibraryConsistencyIssues
// LibraryConsistencyController last computed, across every present
// catalog at once (see the controller's own class comment).
class LibraryConsistencyIssueListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Populated by LibraryConsistencyController; not constructible from QML")

public:
    enum Roles {
        KindRole = Qt::UserRole + 1,
        FormatRole,  // "rekordbox", "engine", or "onelibrary", which catalog this issue is in
        SurvivorRole,  // {sourceId, title, artist, filePath}, or an empty map when there's no survivor
        BrokenTracksRole,  // QVariantList of {sourceId, title, artist, filePath}
        CueMergeNeededRole,
    };

    explicit LibraryConsistencyIssueListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void clear();
    // Adds one format's worth of freshly-scanned issues to whatever's
    // already shown, how the progressive, per-format scan builds up the
    // combined view instead of replacing it each time (see
    // LibraryConsistencyController::scanNextPendingFormat()).
    void appendIssues(std::vector<domain::LibraryConsistencyIssue> issues);
    // Replaces one specific issue's own entry in place, used right
    // after a per-item repair/delete so that one row updates without
    // disturbing every other format's already-shown results the way a
    // full clear()+appendIssues() rescan would.
    void removeIssueAt(int index);
    const std::vector<domain::LibraryConsistencyIssue> &issues() const { return m_issues; }

private:
    std::vector<domain::LibraryConsistencyIssue> m_issues;
};

// Read-only Qt list model over the JunkCueIssues LibraryConsistencyController
// last found, a memory cue sitting at position 0 on some track (see
// domain::JunkCueFinder's own doc comment for why only memory, not hot,
// cues count). Same progressive per-format population as
// LibraryConsistencyIssueListModel above, and populated by the same scan.
class JunkCueIssueListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Populated by LibraryConsistencyController; not constructible from QML")

public:
    enum Roles {
        FormatRole = Qt::UserRole + 1,  // "rekordbox", "engine", or "onelibrary"
        TitleRole,
        ArtistRole,
        // Same brokenTrackToMap() shape LibraryConsistencyIssueListModel's
        // own survivor/brokenTracks roles already use (sourceId, title,
        // artist, filePath, artworkPath, durationMs, cues) -- everything
        // TrackWaveformCard needs, for the memory-cue section to show one
        // instead of a bare title/artist line.
        TrackRole,
    };

    explicit JunkCueIssueListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void clear();
    void appendIssues(std::vector<domain::JunkCueIssue> issues);
    // Removes one row with no write, "Ignore" is just "stop showing me
    // this in the current view," not a persisted dismissal.
    void removeAt(int index);
    const std::vector<domain::JunkCueIssue> &issues() const { return m_issues; }

private:
    std::vector<domain::JunkCueIssue> m_issues;
};

// Result of a background scan task for one format, see
// LibraryConsistencyController::scanNextPendingFormat(). Built entirely
// on a worker thread, no access to the controller.
struct LibraryConsistencyScanResult
{
    std::vector<domain::LibraryConsistencyIssue> issues;
    std::vector<domain::JunkCueIssue> junkCues;
    // This format's own playlist membership tally, unfiltered by
    // whatever TrackScope playlistName below scoped `issues`/`junkCues`
    // to -- computed before that filtering, same "picking a playlist
    // never shrinks the picker's own list of choices" convention
    // SyncController's own playlistNames/playlistTrackCounts follow. One
    // format's worth only; LibraryConsistencyController::onScanFinished()
    // merges each format's contribution into the running cross-catalog
    // union as the progressive per-format scan completes.
    QStringList playlistNames;
    QVariantMap playlistTrackCounts;
    QString errorMessage;
};

// Result of a background repair/delete task.
struct LibraryConsistencyWriteResult
{
    QString errorMessage;
    QString statusMessage;
};

// Finds catalog rows whose backing audio file is missing and, where a
// healthy same-catalog duplicate exists, repairs them: merges any cues
// the broken row(s) have onto the survivor (if it doesn't already have
// them), then removes the broken row via that format's own existing
// track-removal primitive. Works across all three catalogs at once,
// scan() finds every format actually present on the stick and scans them
// one after another, adding each format's results to the shared list as
// soon as that format finishes rather than waiting for all three (see
// scanNextPendingFormat()). Every issue carries its own format (read off
// the domain::Track objects it holds, every reader already tags each
// track with the catalog it came from), so repairAll()/repairOne()/
// deleteOrphan() dispatch to the right writer per issue rather than
// assuming one format for the whole list.
//
// Like CleanupController::apply(), repairs act on the issues already in
// the model, not a fresh re-scan, the per-writer staleness guard
// (RekordboxCueWriter/OneLibraryCueWriter etc., each constructed fresh
// right before its own write) is what catches "the stick changed
// underneath us," matching this codebase's established convention for a
// batch apply.
//
// A row with no healthy survivor anywhere (Kind::Missing) is never
// auto-repaired. There's nothing to consolidate onto. OneLibrary rows
// in that state can be deleted manually via deleteOrphan() (safe,
// independent of anything else); rekordbox/Engine have no equivalent
// "delete with no replacement, clear playlist memberships instead of
// reassigning them" primitive yet, so those stay informational only
// ("re-add via Rekordbox/Engine software"), deleteOrphan() is a no-op
// for them. A Kind::Conflict issue is never auto-resolved either (the
// whole point is that guessing would be wrong). LibraryConsistencyPage
// .qml offers manual resolution for those via CleanupController's own
// existing two-track-merge feature instead of anything in this class.
class LibraryConsistencyController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(seabass::gui::LibraryConsistencyIssueListModel *issues READ issuesModel CONSTANT)
    Q_PROPERTY(seabass::gui::JunkCueIssueListModel *junkCues READ junkCuesModel CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool writing READ writing NOTIFY writingChanged)
    Q_PROPERTY(int scanCurrent READ scanCurrent NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanProgressChanged)
    Q_PROPERTY(QString scanningFormat READ scanningFormat NOTIFY scanningFormatChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(int repairableCount READ repairableCount NOTIFY issuesChanged)
    // Backs the Playlist picker in JunkCuePage.qml -- same shape/
    // convention as SyncController's own playlistNames/
    // playlistTrackCounts (index 0 of ["All tracks"] + these is the "no
    // filter" choice; see PlaylistPickerCombo.qml). Unfiltered by
    // whatever playlist scan() was last given, so picking one never
    // shrinks the picker's own list of choices.
    Q_PROPERTY(QStringList playlistNames READ playlistNames NOTIFY issuesChanged)
    Q_PROPERTY(QVariantMap playlistTrackCounts READ playlistTrackCounts NOTIFY issuesChanged)

public:
    explicit LibraryConsistencyController(QObject *parent = nullptr);

    LibraryConsistencyIssueListModel *issuesModel() { return &m_model; }
    JunkCueIssueListModel *junkCuesModel() { return &m_junkCueModel; }
    bool busy() const { return m_busy; }
    bool writing() const { return m_writing; }
    int scanCurrent() const { return m_scanCurrent; }
    int scanTotal() const { return m_scanTotal; }
    // "rekordbox"/"engine"/"onelibrary" while that format's scan is
    // actually running, empty once the whole sequence finishes, lets
    // the page show "Scanning Engine..." progressively.
    QString scanningFormat() const { return m_scanningFormat; }
    QString errorMessage() const { return m_errorMessage; }
    QString statusMessage() const { return m_statusMessage; }
    QStringList playlistNames() const { return m_playlistNames; }
    QVariantMap playlistTrackCounts() const { return m_playlistTrackCounts; }
    // Computed directly from the model rather than counted via realized
    // ListView delegates in QML. ListView virtualizes, so itemAtIndex()
    // is null for anything outside the visible/cache range, which would
    // silently undercount on a long list.
    int repairableCount() const;

    // Scans every format actually present: rekordbox if rekordboxPath is
    // non-empty, engine if enginePath is non-empty, onelibrary if
    // exportLibrary.db exists under rekordboxPath. Progressive: each
    // format's issues are added to the model as soon as that format's
    // scan finishes, not all at once at the end. playlistName empty (the
    // default) scans/checks the whole library, same as before this
    // parameter existed; a real name scopes both the junk-cue list and
    // the consistency check to just that playlist's tracks, via
    // domain::TrackScope -- playlistNames/playlistTrackCounts themselves
    // stay unfiltered so the picker never shrinks its own choices.
    Q_INVOKABLE void scan(const QString &rekordboxPath, const QString &enginePath,
                           const QString &playlistName = QString());

    // Repairs every currently-Repairable issue across every format:
    // writes merged cues onto each survivor where needed, then removes
    // every broken row. Never touches Conflict/Missing issues.
    Q_INVOKABLE void repairAll();

    // Same as repairAll(), scoped to the single issue at index, lets
    // one row be repaired on its own without waiting on every other
    // format's batch.
    Q_INVOKABLE void repairOne(int index);

    // Deletes a single Missing issue's broken row(s), OneLibrary only
    // (see class comment). No-op for a rekordbox/Engine issue.
    Q_INVOKABLE void deleteOrphan(int index);

    // Rewrites the track's full cue list with the offending 0:00 memory
    // cue (and any other memory cue also sitting at 0, if somehow more
    // than one) removed, same "pass the complete replacement set"
    // contract as CueWriter::writeHotCues() everywhere else.
    Q_INVOKABLE void removeJunkCue(int index);
    // Same as removeJunkCue(), for every currently-listed junk-cue issue
    // across every format at once, one real write per format (chained
    // the same way repairAll() chains m_pendingRepairs).
    Q_INVOKABLE void removeAllJunkCues();
    // Local-only dismiss, no write, see JunkCueIssueListModel::removeAt().
    Q_INVOKABLE void ignoreJunkCue(int index);
    Q_INVOKABLE void ignoreAllJunkCues();

signals:
    void busyChanged();
    void writingChanged();
    void scanProgressChanged();
    void scanningFormatChanged();
    void errorMessageChanged();
    void statusMessageChanged();
    void issuesChanged();

private:
    void onScanFinished();
    void onWriteFinished();
    void scanNextPendingFormat();
    void setBusy(bool busy);
    void setWriting(bool writing);
    void setScanProgress(int current, int total);
    void setScanningFormat(const QString &format);
    void setErrorMessage(const QString &message);
    void setStatusMessage(const QString &message);
    QString pathForFormat(const QString &format) const;
    std::shared_ptr<QtProgressReporter> makeReporter();
    // Folds one format's own playlist tally (LibraryConsistencyScanResult
    // ::playlistNames/playlistTrackCounts) into the running cross-catalog
    // union, max count per name across whichever catalogs have it -- same
    // semantics as SyncController's own collectPlaylistSummary(), just
    // applied incrementally as each format's progressive scan completes
    // instead of all at once.
    void mergePlaylistSummary(const QStringList &names, const QVariantMap &counts);

    LibraryConsistencyIssueListModel m_model;
    JunkCueIssueListModel m_junkCueModel;
    QFutureWatcher<LibraryConsistencyScanResult> m_watcher;
    QFutureWatcher<LibraryConsistencyWriteResult> m_writeWatcher;
    QString m_rekordboxPath;
    QString m_enginePath;
    // The playlistName scan() was last called with -- so the automatic
    // re-scan onWriteFinished() runs after every repair/removal stays
    // scoped to whatever playlist was selected, instead of silently
    // reverting to "All tracks".
    QString m_currentPlaylistName;
    QStringList m_playlistNames;
    QVariantMap m_playlistTrackCounts;
    std::vector<QString> m_pendingScanFormats;
    // Each pending repair-batch entry is one format's worth of
    // currently-Repairable issues, consumed one format at a time the
    // same way m_pendingScanFormats drives the scan sequence. Empty for
    // a repairOne()/deleteOrphan() single-item write, whose completion
    // handler tells apart from a batch by this being empty.
    std::vector<std::pair<QString, std::vector<domain::LibraryConsistencyIssue>>> m_pendingRepairs;
    // Same idea, for removeAllJunkCues(): one format's worth of tracks
    // to strip the 0:00 memory cue from, consumed the same way.
    std::vector<std::pair<QString, std::vector<domain::Track>>> m_pendingJunkCueRemovals;
    // What onWriteFinished() should do once every pending format's write
    // is done, set by whichever public write method just kicked things
    // off. A junk-cue removal can't change file existence or which
    // track matches which (that's all LibraryConsistencyChecker looks
    // at), so it never needs the full re-scan repairAll()/repairOne()/
    // deleteOrphan() still trigger -- those really can change what
    // other issues exist (removing a broken row, merging cues onto a
    // survivor). See onWriteFinished()'s own comment for the local-
    // update each junk-cue-removal kind performs instead of scanning.
    enum class PendingWriteKind { Repair, RemoveOneJunkCue, RemoveAllJunkCues };
    PendingWriteKind m_pendingWriteKind = PendingWriteKind::Repair;
    // Valid only when m_pendingWriteKind is RemoveOneJunkCue: the row
    // removeJunkCue() was called with, applied to m_junkCueModel once
    // the write actually succeeds rather than optimistically before.
    int m_pendingJunkCueRemovalIndex = -1;
    // Valid only when m_pendingWriteKind is Repair: the m_model row
    // indices repairAll()/repairOne()/deleteOrphan() captured at call
    // time, removed locally via removeIssueAt() once every pending
    // format's task in the batch succeeds -- unless
    // m_pendingRepairTouchedRekordbox, in which case a real scan()
    // runs instead (see onWriteFinished()'s own comment: a rekordbox
    // repair's best-effort OneLibrary mirror write can silently stale
    // an already-listed OneLibrary issue, something only a fresh
    // LibraryConsistencyChecker::check() run could catch).
    std::vector<int> m_pendingRepairedIndices;
    bool m_pendingRepairTouchedRekordbox = false;
    bool m_busy = false;
    bool m_writing = false;
    int m_scanCurrent = 0;
    int m_scanTotal = 0;
    QString m_scanningFormat;
    QString m_errorMessage;
    QString m_statusMessage;
};

}  // namespace seabass::gui
