#include "rekordbox_guard_controller.hpp"

#include <QtConcurrent/QtConcurrentRun>

#include "infrastructure/system/rekordbox_process_detector.hpp"

namespace djconvert::gui
{

namespace
{
constexpr int PollIntervalMs = 3000;
}

RekordboxGuardController::RekordboxGuardController(QObject *parent) : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<bool>::finished, this, &RekordboxGuardController::onPollFinished);
    connect(&m_timer, &QTimer::timeout, this, &RekordboxGuardController::poll);
    m_timer.start(PollIntervalMs);
    poll();
}

void RekordboxGuardController::poll()
{
    if (m_watcher.isRunning()) {
        return;  // previous check hasn't returned yet -- skip this tick
    }
    m_watcher.setFuture(QtConcurrent::run(infrastructure::system::isRekordboxRunning));
}

void RekordboxGuardController::onPollFinished()
{
    bool running = m_watcher.result();
    if (m_running == running) {
        return;
    }
    m_running = running;
    emit rekordboxRunningChanged();
}

}  // namespace djconvert::gui
