#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>

#include <memory>
#include <string>
#include <vector>

#include "domain/duplicate_cleanup.hpp"
#include "gui/qt_progress_reporter.hpp"
#include "gui/undo_tracking.hpp"

namespace djconvert::gui
{

// Read-only Qt list model over the cleanup plans CleanupController last
// computed. Only groups DuplicateCleanupPlanner found something
// actually removable for (toRemove non-empty) are exposed -- a
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

private:
    std::vector<domain::DuplicateCleanupPlan> m_plans;
    // Parallel to m_plans -- default true unless the plan differs (see
    // DuplicateCleanupPlan::differs' own doc comment for why that
    // defaults to excluded).
    std::vector<bool> m_included;
};

// Result of a background scan+plan task -- see CleanupController::
// rescan(). Built entirely on a worker thread, no access to the
// controller.
struct CleanupTaskResult
{
    std::vector<domain::DuplicateCleanupPlan> plans;
    QString errorMessage;
};

// Result of a background apply/undo task -- see CleanupController::
// apply()/undoLastOperation().
struct CleanupWriteResult
{
    QString errorMessage;
    QString statusMessage;
    std::vector<UndoableBackup> backups;
};

// Wraps DuplicateCleanupPlanner + both formats' LibraryCleanupWriter
// implementations for QML: scans a library, finds duplicate groups,
// plans a survivor for each, and (for included groups) removes every
// other copy -- merging their cues onto the survivor first and fixing
// up playlist membership on both formats -- backing up before every
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
    Q_PROPERTY(djconvert::gui::CleanupPlanListModel *plans READ plansModel CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool writing READ writing NOTIFY writingChanged)
    Q_PROPERTY(int scanCurrent READ scanCurrent NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanProgressChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)
    Q_PROPERTY(QString totalWastedBytesHuman READ totalWastedBytesHuman NOTIFY plansChanged)
    Q_PROPERTY(int includedCount READ includedCount NOTIFY includedChanged)

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

    // format is "rekordbox" or "engine"; path is the corresponding
    // DetectedStick.rekordboxPath / .enginePath.
    Q_INVOKABLE void scan(const QString &format, const QString &path);

    Q_INVOKABLE void setIncluded(int index, bool included);

    // Removes every doomed track in every currently-included group:
    // merges cues onto the survivor, fixes up playlist membership on
    // both formats, backs up first. Does NOT delete any audio file --
    // see the class comment.
    Q_INVOKABLE void apply();

    // Reverts every file the last apply() touched back to what it was
    // immediately before. Available only right after a write (canUndo).
    Q_INVOKABLE void undoLastOperation();

signals:
    void busyChanged();
    void writingChanged();
    void scanProgressChanged();
    void errorMessageChanged();
    void statusMessageChanged();
    void canUndoChanged();
    void plansChanged();
    void includedChanged();

private:
    void rescan();
    void onRescanFinished();
    void onWriteFinished();
    void setBusy(bool busy);
    void setWriting(bool writing);
    void setScanProgress(int current, int total);
    void setErrorMessage(const QString &message);
    void setStatusMessage(const QString &message);
    std::shared_ptr<QtProgressReporter> makeReporter();

    CleanupPlanListModel m_model;
    QFutureWatcher<CleanupTaskResult> m_watcher;
    QFutureWatcher<CleanupWriteResult> m_writeWatcher;
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

}  // namespace djconvert::gui
