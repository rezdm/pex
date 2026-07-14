#pragma once

#include <chrono>
#include <vector>

namespace pex {

enum class ProcessEventType {
    Fork,  // New process created (pid = child, parent_pid = parent)
    Exec,  // Process replaced its image (pid)
    Exit   // Process terminated (pid, exit_code)
};

struct ProcessEvent {
    ProcessEventType type = ProcessEventType::Fork;
    int pid = 0;
    int parent_pid = 0;  // Fork only; 0 otherwise
    int exit_code = 0;   // Exit only
    std::chrono::steady_clock::time_point timestamp;
};

// Kernel-push process lifecycle events (fork/exec/exit), the complement to
// the poll-based providers: polling misses any process that lives less than
// one refresh tick. Implementations own a background collection thread.
//
// Availability is platform- and privilege-dependent (e.g. the Linux proc
// connector needs CAP_NET_ADMIN); when start() returns false the app simply
// runs poll-only and every consumer must treat the feed as optional.
class IProcessEventSource {
public:
    virtual ~IProcessEventSource() = default;

    // Begin collecting. Returns false when the mechanism is unavailable
    // (missing kernel support or privileges). Safe to call once.
    virtual bool start() = 0;

    // Stop the collection thread. Safe to call multiple times / unstarted.
    virtual void stop() = 0;

    // True between a successful start() and stop().
    [[nodiscard]] virtual bool is_active() const = 0;

    // Take all events collected since the previous drain (oldest first).
    // Called from the DataStore collection thread; must be thread-safe
    // against the internal event thread. The internal buffer is bounded:
    // under extreme churn the oldest events are dropped.
    virtual std::vector<ProcessEvent> drain() = 0;
};

} // namespace pex
