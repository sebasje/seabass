#include "infrastructure/media/windows_removable_media_monitor.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>

namespace seabass::infrastructure::media
{

WindowsRemovableMediaMonitor::~WindowsRemovableMediaMonitor()
{
    stop();
}

void WindowsRemovableMediaMonitor::start(std::function<void()> onChange)
{
    if (m_running) {
        return;
    }
    m_onChange = std::move(onChange);
    m_lastDriveMask = ::GetLogicalDrives();
    m_running = true;
    m_thread = std::thread(&WindowsRemovableMediaMonitor::run, this);
}

void WindowsRemovableMediaMonitor::stop()
{
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void WindowsRemovableMediaMonitor::run()
{
    while (m_running) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (!m_running) {
            break;
        }
        std::uint32_t driveMask = ::GetLogicalDrives();
        if (driveMask != m_lastDriveMask) {
            m_lastDriveMask = driveMask;
            if (m_onChange) {
                m_onChange();
            }
        }
    }
}

}  // namespace seabass::infrastructure::media
