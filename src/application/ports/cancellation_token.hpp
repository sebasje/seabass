#pragma once

#include <atomic>
#include <memory>

namespace seabass::application
{

// Cooperative cancellation for long-running use cases (the stick backup
// reads tens of GB; the user must be able to stop it). Deliberately not a
// method on ProgressReporter: the reporter is an output channel with
// several implementations already, whereas cancellation is an input that
// the GUI flips from a thread that never touches the reporter. Copies
// share the flag, so a use case receives one by value and the caller keeps
// another copy to cancel() from.
//
// Checked between chunks, never mid-syscall -- "stops after the current
// file" is the contract callers may promise their users.
class CancellationToken
{
public:
    CancellationToken() : m_flag(std::make_shared<std::atomic_bool>(false)) {}

    // A token nobody holds a cancelling copy of -- for callers that have
    // no cancel affordance (CLI one-shots, tests).
    static CancellationToken none() { return CancellationToken(); }

    void cancel() { m_flag->store(true, std::memory_order_relaxed); }
    bool cancelled() const { return m_flag->load(std::memory_order_relaxed); }

private:
    std::shared_ptr<std::atomic_bool> m_flag;
};

}  // namespace seabass::application
