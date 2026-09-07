#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "application/ports/cancellation_token.hpp"
#include "domain/duplicate_cue_consolidation.hpp"
#include "gui/qt_progress_reporter.hpp"
#include "gui/edit/changes/copy_cues_change.hpp"

namespace seabass::gui
{

class LibraryEditSession;

// Read-only Qt list model over the consolidation plans DuplicatesController
// last computed. Only Unambiguous (fixable) and Conflict (informational)
// plans are exposed -- NoCues/AlreadyConsistent groups need no attention,
// same filtering cli/main.cpp's handleDuplicates applies.
class ConsolidationPlanListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Populated by DuplicatesController; not constructible from QML")

public:
    enum Roles {
        KindRole = Qt::UserRole + 1,
        FilenameRole,
        DescriptionRole,
        ActionableRole,
        TracksRole,
        WastedBytesRole,
        // A copy is staged for this group (see DuplicatesController): what
        // will happen on Save, and from which copy.
        StagedRole,
        StagedDescriptionRole,
    };

    explicit ConsolidationPlanListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Waveforms are NOT precomputed here -- QML fetches a track's
    // waveform on demand via PlaybackController::waveformFor() instead
    // of this controller decoding one eagerly for every displayed track
    // up front (see sync_controller.hpp's own comment on the same
    // change, made for the same reason: it's what made a real "scanning
    // takes forever" report possible in the first place).
    void setPlans(std::vector<domain::ConsolidationPlan> plans);
    const std::vector<domain::ConsolidationPlan> &plans() const { return m_plans; }
    // Removes one plan without a full rescan -- once its cues are on the
    // stick that group is AlreadyConsistent, which this model never shows
    // at all (see runRescanTask's own filtering), and DuplicateTrackFinder
    // groups by filename/title+artist+duration only, never cues, so
    // removing it can't change any other group's own classification.
    void removePlanAt(int index);
    void setStaged(int index, bool staged, const QString &description);
    void clearStaged();

private:
    std::vector<domain::ConsolidationPlan> m_plans;
    std::vector<QString> m_stagedDescriptions;  // empty = not staged; parallel to m_plans
};

// Result of a background rescan task -- see DuplicatesController::rescan().
// Built entirely on a worker thread, with no access to the controller.
struct DuplicatesTaskResult
{
    std::vector<domain::ConsolidationPlan> plans;
    QString errorMessage;  // empty on success
    bool cancelled = false;  // stopped via cancelScan(); nothing else is set
};

// One "copy source's cues onto these targets" operation -- applyOne() and
// copyFromTrack() each produce exactly one, applyAllUnambiguous() produces
// one per unambiguous group. Tracks are copied by value so the save loop
// never touches the GUI-thread model.
// Wraps ConsolidateDuplicateCues for QML: scans a library, finds duplicate
// tracks, and (for Unambiguous groups) can copy cues from the one copy that
// has them onto the others -- mirroring cli/main.cpp's handleDuplicates
// wiring exactly so GUI and CLI behave identically.
//
// Edits are staged, not written: applyOne()/copyFromTrack()/
// applyAllUnambiguous() each stage one CopyCuesChange per group in the
// library's LibraryEditSession (the first one takes the edit lock), the
// row shows what will happen, and the page's Save writes them all. A
// group whose change reached the stick disappears from the list.
//
// The scan itself runs on a background thread (see ScanController for
// the same reasoning): it can take several seconds for a large library.
class DuplicatesController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(seabass::gui::ConsolidationPlanListModel *plans READ plansModel CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // True while the read-only rescan runs (never during a write): it can
    // be stopped via cancelScan(), after which scanCancelled() fires.
    Q_PROPERTY(bool scanCancellable READ scanCancellable NOTIFY busyChanged)
    Q_PROPERTY(int scanCurrent READ scanCurrent NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanProgressChanged)
    // What's actually happening right now -- e.g. "Scanning rekordbox
    // tracks" while the reader runs (scanCurrent/scanTotal move), then
    // "Finding duplicates..." for the grouping pass afterward (no
    // per-item progress for that one, scanTotal resets to 0 =
    // indeterminate). Without this, the progress bar used to sit frozen
    // at 100% for several seconds once the raw scan finished, with no
    // indication anything was still happening.
    Q_PROPERTY(QString scanLabel READ scanLabel NOTIFY scanProgressChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)
    // Mirrors the session: true while a save is writing to the stick.
    Q_PROPERTY(bool writing READ writing NOTIFY writingChanged)
    Q_PROPERTY(int stagedCount READ stagedCount NOTIFY plansChanged)
    // Sum, across every duplicate group currently listed (Unambiguous and
    // Conflict alike -- being a duplicate doesn't depend on cue-consolidation
    // status), of every group's file sizes minus its single largest copy:
    // the disk space that would be freed if each group kept only one file.
    // Purely informational -- Seabass has no feature that deletes audio
    // files, this is a number, not an action.
    Q_PROPERTY(QString totalWastedBytesHuman READ totalWastedBytesHuman NOTIFY plansChanged)

public:
    explicit DuplicatesController(QObject *parent = nullptr);

    ConsolidationPlanListModel *plansModel() { return &m_model; }
    bool busy() const { return m_busy; }
    QString totalWastedBytesHuman() const;
    bool writing() const;
    int scanCurrent() const { return m_scanCurrent; }
    int scanTotal() const { return m_scanTotal; }
    QString scanLabel() const { return m_scanLabel; }
    QString errorMessage() const { return m_errorMessage; }
    QString statusMessage() const { return m_statusMessage; }
    bool canUndo() const;
    int stagedCount() const { return static_cast<int>(m_stagedByGroup.size()); }

    // format is "rekordbox", "engine", or "onelibrary"; path is the
    // corresponding DetectedStick.rekordboxPath / .enginePath (OneLibrary
    // shares rekordboxPath -- exportLibrary.db lives alongside
    // export.pdb under the same PIONEER root).
    Q_INVOKABLE void scan(const QString &format, const QString &path);

    // Same convention as CleanupController::hasOneLibrary()/
    // ScanController::hasOneLibrary().
    Q_INVOKABLE bool hasOneLibrary(const QString &pioneerRoot) const;

    // Stage copying the one cued copy's cues onto the rest of the group.
    Q_INVOKABLE void applyOne(int index);
    Q_INVOKABLE void applyAllUnambiguous();

    // Manual override for a Conflict group: stages copying sourceTrackId's
    // cues onto every other track in that same group. Unlike applyOne,
    // this works regardless of plan kind -- it's the human decision the
    // domain model defers to when copies disagree (see
    // ConsolidationPlan::Kind::Conflict's doc comment). Staging a second
    // choice for the same group replaces the first.
    Q_INVOKABLE void copyFromTrack(int index, const QString &sourceTrackId);
    Q_INVOKABLE void unstage(int index);

    // Reverts every file the last save touched (the session's undo).
    Q_INVOKABLE void undoLastOperation();

    bool scanCancellable() const { return m_busy && !writing(); }
    Q_INVOKABLE void cancelScan();

signals:
    void scanCancelled();
    void busyChanged();
    void scanProgressChanged();
    void errorMessageChanged();
    void statusMessageChanged();
    void canUndoChanged();
    void writingChanged();
    void plansChanged();

private:
    void rescan();
    void onRescanFinished();
    void attachSession();
    void stageCopy(int index, const DuplicatesCopyOp &op);
    void setBusy(bool busy);
    void setScanProgress(int current, int total);
    void setScanLabel(const QString &label);
    void setErrorMessage(const QString &message);
    void setStatusMessage(const QString &message);
    std::shared_ptr<QtProgressReporter> makeReporter();
    static QString groupKeyFor(const domain::ConsolidationPlan &plan);
    int indexOfGroupKey(const QString &groupKey) const;

    ConsolidationPlanListModel m_model;
    QFutureWatcher<DuplicatesTaskResult> m_watcher;
    application::CancellationToken m_scanCancel;  // fresh per rescan()
    QPointer<LibraryEditSession> m_session;
    struct StagedInfo
    {
        QString changeId;
        QString description;
    };
    std::map<QString, StagedInfo> m_stagedByGroup;  // group key -> what is staged for it
    QString m_format;
    QString m_path;
    bool m_busy = false;
    int m_scanCurrent = 0;
    int m_scanTotal = 0;
    QString m_scanLabel;
    QString m_errorMessage;
    QString m_statusMessage;
};

}  // namespace seabass::gui
