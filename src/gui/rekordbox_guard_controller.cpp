#include "rekordbox_guard_controller.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include "infrastructure/system/rekordbox_process_detector.hpp"

namespace seabass::gui
{

namespace
{
constexpr int PollIntervalMs = 3000;

QString detectConflictingSoftware()
{
    return QString::fromStdString(infrastructure::system::conflictingDjSoftwareName());
}
}  // namespace

RekordboxGuardController::RekordboxGuardController(QObject *parent) : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<QString>::finished, this, &RekordboxGuardController::onPollFinished);
    connect(&m_timer, &QTimer::timeout, this, &RekordboxGuardController::poll);
    m_timer.start(PollIntervalMs);
    poll();
}

void RekordboxGuardController::poll()
{
    if (m_watcher.isRunning()) {
        return;  // previous check hasn't returned yet -- skip this tick
    }
    m_watcher.setFuture(QtConcurrent::run(detectConflictingSoftware));
}

void RekordboxGuardController::onPollFinished()
{
    QString name = m_watcher.result();
    if (m_conflictingSoftware == name) {
        return;
    }
    bool rekordboxBefore = rekordboxRunning();
    m_conflictingSoftware = name;
    emit conflictingSoftwareChanged();
    if (rekordboxBefore != rekordboxRunning()) {
        emit rekordboxRunningChanged();
    }
}

}  // namespace seabass::gui
