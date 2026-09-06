#pragma once

#include <QString>

#include <exception>

namespace seabass::gui
{

// Reads a finished QFutureWatcher's result without letting an exception
// from the worker thread reach the event loop.
//
// QtConcurrent stores an exception thrown inside a run() body and
// rethrows it from QFutureWatcher::result(). That call happens in a slot
// on the GUI thread, where nothing catches it: Qt wraps it as
// QUnhandledException and the process calls std::terminate. A stick
// pulled mid-walk, an unreadable archive -- any std::filesystem_error on
// the background side -- therefore killed the whole app instead of
// showing an error (confirmed from a real crash dump: BackupStick::
// preview() threw, StickBackupController::onPreviewFinished() rethrew,
// terminate).
//
// On a throw this hands back a default-constructed result -- every
// caller already tolerates the "no result" case -- and puts the message
// in `error` for the controller to surface however it normally does.
template <typename Watcher>
auto takeResult(Watcher &watcher, QString *error = nullptr) -> decltype(watcher.result())
{
    try {
        return watcher.result();
    } catch (const std::exception &e) {
        if (error != nullptr) {
            *error = QString::fromUtf8(e.what());
        }
    } catch (...) {
        if (error != nullptr) {
            *error = QStringLiteral("Unknown error");
        }
    }
    return {};
}

}  // namespace seabass::gui
