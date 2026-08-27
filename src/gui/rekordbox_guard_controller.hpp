#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>

namespace djconvert::gui
{

// Periodically polls (on a background thread -- a /proc scan is cheap but
// still real syscall I/O, so it never runs on the GUI thread) whether
// Rekordbox appears to be running on this machine, exposing it to QML as
// a global, persistent warning banner shown above every page. This is
// advisory only: the actual refusal happens independently inside every
// write background task (see write_guard.hpp), which checks the same
// underlying detector right before writing. Detection can never be a
// guarantee either way -- see
// infrastructure::system::isRekordboxRunning()'s doc comment.
class RekordboxGuardController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool rekordboxRunning READ rekordboxRunning NOTIFY rekordboxRunningChanged)

public:
    explicit RekordboxGuardController(QObject *parent = nullptr);

    bool rekordboxRunning() const { return m_running; }

signals:
    void rekordboxRunningChanged();

private:
    void poll();
    void onPollFinished();

    QTimer m_timer;
    QFutureWatcher<bool> m_watcher;
    bool m_running = false;
};

}  // namespace djconvert::gui
