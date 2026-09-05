#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QTimer>

namespace seabass::gui
{

// Periodically polls (on a background thread -- a /proc scan is cheap but
// still real syscall I/O, so it never runs on the GUI thread) whether
// Rekordbox or Engine DJ appears to be running on this machine, exposing
// it to QML as a global, persistent warning banner shown above every
// page. This is advisory only: the actual refusal happens independently
// inside every write background task (see write_guard.hpp), which checks
// the same underlying detector right before writing. Detection can never
// be a guarantee either way -- see
// infrastructure::system::isRekordboxRunning()'s doc comment.
//
// `rekordboxRunning` keeps its original meaning (every stick write in the
// app refuses on it); `conflictingSoftware` additionally covers Engine
// DJ, which only the full-stick backup and restore refuse on.
class RekordboxGuardController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool rekordboxRunning READ rekordboxRunning NOTIFY rekordboxRunningChanged)
    Q_PROPERTY(QString conflictingSoftware READ conflictingSoftware NOTIFY conflictingSoftwareChanged)

public:
    explicit RekordboxGuardController(QObject *parent = nullptr);

    bool rekordboxRunning() const { return m_conflictingSoftware == QStringLiteral("rekordbox"); }
    // "" when nothing was detected, else "rekordbox" or "Engine DJ".
    QString conflictingSoftware() const { return m_conflictingSoftware; }

signals:
    void rekordboxRunningChanged();
    void conflictingSoftwareChanged();

private:
    void poll();
    void onPollFinished();

    QTimer m_timer;
    QFutureWatcher<QString> m_watcher;
    QString m_conflictingSoftware;
};

}  // namespace seabass::gui
