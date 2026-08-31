#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "domain/duplicate_cleanup.hpp"
#include "gui/qt_progress_reporter.hpp"
#include "gui/undo_tracking.hpp"
#include "infrastructure/cleanup/pending_deletion_manifest.hpp"

namespace seabass::gui
{

// Read-only Qt list model over the cleanup plans CleanupController last
// computed. Only groups DuplicateCleanupPlanner found something
// actually removable for (toRemove non-empty) are exposed. A
// degenerate single-track "group" has nothing to clean up.
class CleanupPlanListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Populated by CleanupController; not constructible from QML")

public:
    enum Roles {
        SurvivorRole = Qt::UserRole + 1,
        ToRemoveRole,
        DiffersRole,
        WastedBytesHumanRole,
        NewCueCountRole,
        IncludedRole,
        HasUnpreservableDataAtRiskRole,
    };

    explicit CleanupPlanListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QHash<int, QByteArray> roleNames() const override;

    void setPlans(std::vector<domain::DuplicateCleanupPlan> plans);
    const std::vector<domain::DuplicateCleanupPlan> &plans() const { return m_plans; }
    bool included(size_t index) const;
    int includedCount() const;
    // Sets every row's included flag at once (select all / deselect all) -
    // only the currently *visible* rows (see setFilter()), so selecting all
    // while a search is active doesn't silently touch groups scrolled out
    // of view by the filter.
    void setAllIncluded(bool included);

    // Case-insensitive substring match against every track's title/artist
    // in each group (the survivor and every copy to remove). A group is
    // visible if any of them match. Filtering never touches m_included:
    // it's purely a view over the same underlying selection state, so
    // clearing/changing the search text never loses what was checked.
    // Empty query shows everything.
    void setFilter(const QString &query);

private:
    std::vector<domain::DuplicateCleanupPlan> m_plans;
    // Parallel to m_plans, default true unless the plan differs (see
    // DuplicateCleanupPlan::differs' own doc comment for why that
    // defaults to excluded).
    std::vector<bool> m_included;
    // Indices into m_plans/m_included that pass the current filter, in
    // order, what QML's row-based data()/setData() actually iterate.
    // included(size_t)/plans() stay index-into-m_plans-based (unfiltered)
    // since apply() needs to act on every included group regardless of
    // what the search box currently shows.
    std::vector<size_t> m_visibleIndices;
};

// Read-only Qt list model over the audio files a past cleanup's DB edit
// orphaned but hasn't deleted from disk yet (see PendingDeletionManifest's
// own class comment). Mirrors CleanupPlanListModel's included/excluded
// per-row selection pattern exactly, for the same reason: "act on
// everything found" is rarely what you want to click through blind.
class PendingDeletionListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Populated by CleanupController; not constructible from QML")

public:
    enum Roles {
        FormatRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        FilePathRole,
        BackupIdRole,
        TimestampRole,
        IncludedRole,
        SizeHumanRole,
    };

    explicit PendingDeletionListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QHash<int, QByteArray> roleNames() const override;

    void setEntries(std::vector<infrastructure::cleanup::PendingDeletion> entries);
    // Only the rows currently checked, what deleteSelectedPendingFiles() acts on.
    std::vector<infrastructure::cleanup::PendingDeletion> includedEntries() const;
    int includedCount() const;
    // Sets every row's included flag at once (select all / deselect all).
    void setAllIncluded(bool included);
    // Sum of every listed entry's fileSizeBytes, and just the checked
    // ones, "how much space is here" vs "how much would this delete
    // free up right now."
    std::uint64_t totalBytes() const;
    std::uint64_t includedBytes() const;

private:
    std::vector<infrastructure::cleanup::PendingDeletion> m_entries;
    // Parallel to m_entries, default true (everything selected), same
    // default-selected convention as CleanupPlanListModel except when a
    // plan `differs` (there's no equivalent ambiguity here to default
    // away from).
    std::vector<bool> m_included;
};

// Result of a background scan+plan task, see CleanupController::
// rescan(). Built entirely on a worker thread, no access to the
// controller.
struct CleanupTaskResult
{
    std::vector<domain::DuplicateCleanupPlan> plans;
    QString errorMessage;
};

// Result of a background apply/undo task, see CleanupController::
// apply()/undoLastOperation().
struct CleanupWriteResult
{
    QString errorMessage;
    QString statusMessage;
    std::vector<UndoableBackup> backups;
};

// Result of a background pending-deletion apply task, see
// CleanupController::deleteSelectedPendingFiles(). Unlike
// CleanupWriteResult there's nothing here to undo (a deleted audio file
// is gone, the DB edit that orphaned it was already backed up
// separately, back when it was first removed from the library), so
// there's no UndoableBackup list.
struct PendingDeletionApplyResult
{
    QString errorMessage;
    QString statusMessage;
};

// Wraps DuplicateCleanupPlanner + both formats' LibraryCleanupWriter
// implementations for QML: scans a library, finds duplicate groups,
// plans a survivor for each, and (for included groups) removes every
// other copy, merging their cues onto the survivor first and fixing
// up playlist membership on both formats, backing up before every
// write, mirroring every other controller's safety pattern exactly.
//
// Deliberately does NOT delete the doomed tracks' audio files: apply()
// stops at removing their library entries and appending them to
// PendingDeletionManifest, so the actual (irreversible, cross-format)
// file deletion stays a distinct, separately-reviewed step.
class CleanupController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(seabass::gui::CleanupPlanListModel *plans READ plansModel CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool writing READ writing NOTIFY writingChanged)
    Q_PROPERTY(int scanCurrent READ scanCurrent NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanProgressChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)
    Q_PROPERTY(QString totalWastedBytesHuman READ totalWastedBytesHuman NOTIFY plansChanged)
    Q_PROPERTY(int includedCount READ includedCount NOTIFY includedChanged)
    Q_PROPERTY(seabass::gui::PendingDeletionListModel *pendingDeletions READ pendingDeletionsModel CONSTANT)
    Q_PROPERTY(int pendingDeletionsIncludedCount READ pendingDeletionsIncludedCount NOTIFY pendingDeletionsChanged)
    Q_PROPERTY(QString totalPendingBytesHuman READ totalPendingBytesHuman NOTIFY pendingDeletionsChanged)
    Q_PROPERTY(QString includedPendingBytesHuman READ includedPendingBytesHuman NOTIFY pendingDeletionsChanged)

public:
    explicit CleanupController(QObject *parent = nullptr);

    CleanupPlanListModel *plansModel() { return &m_model; }
    bool busy() const { return m_busy; }
    bool writing() const { return m_writing; }
    int scanCurrent() const { return m_scanCurrent; }
    int scanTotal() const { return m_scanTotal; }
    QString errorMessage() const { return m_errorMessage; }
    QString statusMessage() const { return m_statusMessage; }
    bool canUndo() const { return !m_lastBackups.empty(); }
    QString totalWastedBytesHuman() const;
    int includedCount() const { return m_model.includedCount(); }
    PendingDeletionListModel *pendingDeletionsModel() { return &m_pendingModel; }
    int pendingDeletionsIncludedCount() const { return m_pendingModel.includedCount(); }
    QString totalPendingBytesHuman() const;
    QString includedPendingBytesHuman() const;

    // format is "rekordbox" or "engine"; path is the corresponding
    // DetectedStick.rekordboxPath / .enginePath.
    Q_INVOKABLE void scan(const QString &format, const QString &path);

    // Same convention as ScanController::hasOneLibrary(), lets QML show
    // that a rekordbox-format cleanup also mirrors into OneLibrary when
    // it's present on this stick.
    Q_INVOKABLE bool hasOneLibrary(const QString &pioneerRoot) const;

    // Builds and populates `plans` with a single manually-declared merge
    // of exactly two tracks (by sourceId), reusing the same survivor/cue-
    // merge planning DuplicateCleanupPlanner already does for auto-
    // detected groups, for ScanPage.qml's "Merge with..." picker,
    // where the user (not DuplicateTrackFinder) has already decided these
    // two are the same track. apply() then works exactly as it does for
    // an auto-detected plan, no separate apply path needed.
    Q_INVOKABLE void planManualMerge(const QString &format, const QString &path, const QString &sourceIdA,
                                      const QString &sourceIdB);

    Q_INVOKABLE void setIncluded(int index, bool included);
    Q_INVOKABLE void setAllIncluded(bool included);

    // Filters the visible duplicate groups by title/artist (see
    // CleanupPlanListModel::setFilter()). Purely a view filter, never
    // changes which groups are included, and apply() still acts on every
    // included group regardless of the current search text.
    Q_INVOKABLE void search(const QString &query);

    // Removes every doomed track in every currently-included group:
    // merges cues onto the survivor, fixes up playlist membership on
    // both formats, backs up first. Does NOT delete any audio file,
    // see the class comment.
    Q_INVOKABLE void apply();

    // Reverts every file the last apply() touched back to what it was
    // immediately before. Available only right after a write (canUndo).
    Q_INVOKABLE void undoLastOperation();

    // Re-reads this format's pending-deletion entries from
    // .seabass-pending-deletions.jsonl on the stick and repopulates
    // pendingDeletions. Cheap (a small text file plus a stat() per entry
    // for its current size), so this runs synchronously rather than on a
    // background thread, called automatically after every scan()/
    // apply(), but QML can also call it directly.
    Q_INVOKABLE void refreshPendingDeletions();

    // Sets format/path and loads pendingDeletions only, unlike scan(),
    // does NOT kick off the (potentially slow, whole-library) duplicate-
    // group rescan. For PendingDeletionsPage, whose entire purpose is the
    // pending-deletions list: forcing a full rescan just to show a small
    // manifest file would be a pointless wait.
    Q_INVOKABLE void loadPendingDeletionsOnly(const QString &format, const QString &path);

    Q_INVOKABLE void setPendingDeletionIncluded(int index, bool included);
    Q_INVOKABLE void setAllPendingDeletionIncluded(bool included);

    // For every currently-checked pendingDeletions entry: re-scans the
    // library fresh and, ONLY for entries resolvePendingDeletions()
    // confirms are genuinely no longer referenced by any current track,
    // deletes the file from disk and clears it from the manifest.
    // Entries still referenced (by anything, whether checked or not) are
    // never deleted no matter what the manifest said, see
    // resolvePendingDeletions()'s own doc comment for why the manifest
    // alone is never trusted. Irreversible: unlike apply(), there is no
    // undo for an actual file deletion.
    Q_INVOKABLE void deleteSelectedPendingFiles();

signals:
    void busyChanged();
    void writingChanged();
    void scanProgressChanged();
    void errorMessageChanged();
    void statusMessageChanged();
    void canUndoChanged();
    void plansChanged();
    void includedChanged();
    void pendingDeletionsChanged();

private:
    void rescan();
    void onRescanFinished();
    void onWriteFinished();
    void onDeletePendingFinished();
    void setBusy(bool busy);
    void setWriting(bool writing);
    void setScanProgress(int current, int total);
    void setErrorMessage(const QString &message);
    void setStatusMessage(const QString &message);
    std::shared_ptr<QtProgressReporter> makeReporter();

    CleanupPlanListModel m_model;
    PendingDeletionListModel m_pendingModel;
    QFutureWatcher<CleanupTaskResult> m_watcher;
    QFutureWatcher<CleanupWriteResult> m_writeWatcher;
    QFutureWatcher<PendingDeletionApplyResult> m_pendingWriteWatcher;
    QString m_format;
    QString m_path;
    bool m_busy = false;
    bool m_writing = false;
    int m_scanCurrent = 0;
    int m_scanTotal = 0;
    QString m_errorMessage;
    QString m_statusMessage;
    std::vector<UndoableBackup> m_lastBackups;
};

}  // namespace seabass::gui
