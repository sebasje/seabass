#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

#include "application/ports/removable_media_monitor.hpp"

namespace seabass::infrastructure::media
{

// Watches for removable-drive hotplug changes on Windows by polling
// GetLogicalDrives() on a background thread every couple of seconds and
// firing onChange() when the set of removable drive letters differs from
// the last check.
//
// This is deliberately the simple, honest first cut rather than the
// "real" event-driven approach (RegisterDeviceNotification against a
// hidden message-only window, reacting to WM_DEVICECHANGE/
// DBT_DEVICEARRIVAL) -- that needs a Win32 message loop running somewhere,
// which is a bigger structural addition than this class's Linux
// counterpart (LinuxUdevMediaMonitor) needed, since udev hands you a
// pollable file descriptor directly. Polling means up to ~2s of latency
// before a hotplug is noticed instead of being instant, but is correct
// and simple; swap this out for the message-window approach if that
// latency ever actually matters in practice.
class WindowsRemovableMediaMonitor : public application::RemovableMediaMonitor
{
public:
    WindowsRemovableMediaMonitor() = default;
    ~WindowsRemovableMediaMonitor() override;

    void start(std::function<void()> onChange) override;
    void stop() override;

private:
    void run();

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::function<void()> m_onChange;
    std::uint32_t m_lastDriveMask = 0;
};

}  // namespace seabass::infrastructure::media
