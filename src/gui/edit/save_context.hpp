#pragma once

#include <QString>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "application/ports/backup_store.hpp"
#include "application/ports/cancellation_token.hpp"
#include "application/ports/operation_log.hpp"
#include "application/ports/progress_reporter.hpp"
#include "gui/undo_tracking.hpp"

namespace seabass::gui
{

// Everything one Save shares across the changes it applies. Lives on the
// worker thread for exactly one save loop; never touched by the GUI.
//
// - backupOnce(): the pre-write backup of a file, made at most once per
//   save however many changes touch it, and remembered as the undo unit.
// - shared<T>(): per-save, per-key resources -- a format's writer plus
//   its scratch copy (FormatWriteSession), a OneLibrary mirror writer --
//   created by the first change that asks and reused by the rest, so
//   per-item changes keep the batch efficiency the old all-in-one tasks
//   had.
// - onFinish(): hooks run after the loop in creation order, even after a
//   cancel or a failure (a scratch copy commits what completed).
class SaveContext
{
public:
    using StatusSink = std::function<void(const QString &)>;

    SaveContext(application::CancellationToken cancel, application::ProgressReporter &progress, StatusSink status,
                QString rekordboxPath, QString enginePath);
    ~SaveContext();
    SaveContext(const SaveContext &) = delete;
    SaveContext &operator=(const SaveContext &) = delete;

    const application::CancellationToken &cancel() const { return m_cancel; }
    application::ProgressReporter &progress() { return m_progress; }
    // "What is going on right now", shown next to the progress bar.
    void status(const QString &text);

    const QString &rekordboxPath() const { return m_rekordboxPath; }
    const QString &enginePath() const { return m_enginePath; }
    // rekordbox and OneLibrary share the PIONEER root.
    QString pathFor(const QString &format) const;
    std::string stickRoot() const;

    application::OperationLog &log();
    application::BackupStore &backupStore();

    // Backs `file` up under `label` unless this save already did; records
    // the backup for undo. Returns true when a backup was made now.
    bool backupOnce(const std::string &file, const std::string &label);
    // The id of the backup this save made for `file` (empty if none yet),
    // for records that want to name it (the pending-deletion manifest).
    std::string backupIdOf(const std::string &file) const;

    template <class T>
    T &shared(const std::string &key, const std::function<std::unique_ptr<T>()> &make)
    {
        auto it = m_shared.find(key);
        if (it == m_shared.end()) {
            std::shared_ptr<T> made(make());
            it = m_shared.emplace(key, std::shared_ptr<void>(made)).first;
        }
        return *static_cast<T *>(it->second.get());
    }

    void onFinish(std::function<void(bool ok)> hook);
    // Runs every hook once, creation order; a throwing hook does not stop
    // the rest. Returns the first hook error, if any.
    std::optional<QString> runFinishHooks(bool ok);

    std::vector<UndoableBackup> takeBackups();

private:
    application::CancellationToken m_cancel;
    application::ProgressReporter &m_progress;
    StatusSink m_status;
    QString m_rekordboxPath;
    QString m_enginePath;
    std::unique_ptr<application::OperationLog> m_log;
    std::unique_ptr<application::BackupStore> m_backupStore;
    std::map<std::string, std::string> m_backedUp;  // file -> backup id
    std::vector<UndoableBackup> m_backups;
    std::map<std::string, std::shared_ptr<void>> m_shared;
    std::vector<std::function<void(bool)>> m_finishHooks;
    bool m_hooksRan = false;
};

}  // namespace seabass::gui
