#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>

#include <string>
#include <unordered_map>
#include <vector>

#include <memory>

#include "domain/duplicate_cue_consolidation.hpp"
#include "gui/qt_progress_reporter.hpp"
#include "gui/undo_tracking.hpp"

namespace seabass::gui
{

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

private:
    std::vector<domain::ConsolidationPlan> m_plans;
};

// Result of a background rescan task -- see DuplicatesController::rescan().
// Built entirely on a worker thread, with no access to the controller.
struct DuplicatesTaskResult
{
    std::vector<domain::ConsolidationPlan> plans;
    QString errorMessage;  // empty on success
};

// Result of a background write task -- see DuplicatesController::
// applyOne()/copyFromTrack()/applyAllUnambiguous()/undoLastOperation().
// Built entirely on a worker thread, with no access to the controller.
// Shared by writes and undo: an undo task always returns an empty
// `backups`, which is exactly what should replace the controller's undo
// trail either way.
struct DuplicatesWriteResult
{
    QString errorMessage;  // empty on success
    QString statusMessage;
    std::vector<UndoableBackup> backups;
};

// One "copy source's cues onto these targets" operation -- applyOne() and
// copyFromTrack() each produce exactly one, applyAllUnambiguous() produces
// one per unambiguous group. Tracks are copied by value so the background
// write task never touches the GUI-thread model.
struct DuplicatesCopyOp
{
    domain::Track source;
    std::vector<domain::Track> targets;
};

// Wraps ConsolidateDuplicateCues for QML: scans a library, finds duplicate
// tracks, and (for Unambiguous groups) can copy cues from the one copy that
// has them onto the others -- backing up first, mirroring cli/main.cpp's
// handleDuplicates wiring exactly so GUI and CLI behave identically. The
// scan itself runs on a background thread (see ScanController for the same
// reasoning): it can take several seconds for a large library, and a
// scan that blocks the UI thread never actually gets to paint a progress
// indicator.
class DuplicatesController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(seabass::gui::ConsolidationPlanListModel *plans READ plansModel CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int scanCurrent READ scanCurrent NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanProgressChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)
    Q_PROPERTY(bool writing READ writing NOTIFY writingChanged)
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
    // True only while actually writing to the stick -- unlike busy(),
    // which is also true during the read-only scan().
    bool writing() const { return m_writing; }
    int scanCurrent() const { return m_scanCurrent; }
    int scanTotal() const { return m_scanTotal; }
    QString errorMessage() const { return m_errorMessage; }
    QString statusMessage() const { return m_statusMessage; }
    bool canUndo() const { return !m_lastBackups.empty(); }

    // format is "rekordbox", "engine", or "onelibrary"; path is the
    // corresponding DetectedStick.rekordboxPath / .enginePath (OneLibrary
    // shares rekordboxPath -- exportLibrary.db lives alongside
    // export.pdb under the same PIONEER root).
    Q_INVOKABLE void scan(const QString &format, const QString &path);

    // Same convention as CleanupController::hasOneLibrary()/
    // ScanController::hasOneLibrary().
    Q_INVOKABLE bool hasOneLibrary(const QString &pioneerRoot) const;

    Q_INVOKABLE void applyOne(int index);
    Q_INVOKABLE void applyAllUnambiguous();

    // Manual override for a Conflict group: copies sourceTrackId's cues
    // onto every other track in that same group. Unlike applyOne, this
    // works regardless of plan kind -- it's the human decision the domain
    // model defers to when copies disagree (see ConsolidationPlan::Kind::
    // Conflict's doc comment).
    Q_INVOKABLE void copyFromTrack(int index, const QString &sourceTrackId);

    // Reverts every file the last apply*()/copyFromTrack() write touched
    // back to what it was immediately before, using the backups that write
    // made. Available only right after a write (canUndo).
    Q_INVOKABLE void undoLastOperation();

signals:
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
    void onWriteFinished();
    void setBusy(bool busy);
    void setWriting(bool writing);
    void setScanProgress(int current, int total);
    void setErrorMessage(const QString &message);
    void setStatusMessage(const QString &message);
    void startApply(std::vector<DuplicatesCopyOp> ops, bool multiGroup);
    std::shared_ptr<QtProgressReporter> makeReporter();

    ConsolidationPlanListModel m_model;
    QFutureWatcher<DuplicatesTaskResult> m_watcher;
    QFutureWatcher<DuplicatesWriteResult> m_writeWatcher;
    QString m_format;
    QString m_path;
    bool m_busy = false;
    int m_scanCurrent = 0;
    int m_scanTotal = 0;
    QString m_errorMessage;
    QString m_statusMessage;
    std::vector<UndoableBackup> m_lastBackups;
    bool m_writing = false;
};

}  // namespace seabass::gui
