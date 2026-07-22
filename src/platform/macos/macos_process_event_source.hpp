#pragma once

#include "../interfaces/i_process_event_source.hpp"

#include <atomic>
#include <mutex>
#include <thread>

namespace pex {

// Process lifecycle events via kqueue EVFILT_PROC (Darwin has the same kqueue
// API as the BSDs). Every existing PID is attached with
// NOTE_FORK|NOTE_EXEC|NOTE_EXIT|NOTE_TRACK; NOTE_TRACK follows descendants and
// NOTE_CHILD carries the fork edge. Best-effort: without root only processes
// the user may observe can be attached, so poll-based collection stays the
// source of truth.
class MacosProcessEventSource : public IProcessEventSource {
public:
    MacosProcessEventSource() = default;
    ~MacosProcessEventSource() override;

    MacosProcessEventSource(const MacosProcessEventSource&) = delete;
    MacosProcessEventSource& operator=(const MacosProcessEventSource&) = delete;

    bool start() override;
    void stop() override;
    [[nodiscard]] bool is_active() const override;
    std::vector<ProcessEvent> drain() override;

private:
    void event_thread_func();

    int kq_ = -1;
    std::thread event_thread_;
    std::atomic<bool> running_{false};  // Lifecycle: gates the loop and stop()'s join
    std::atomic<bool> active_{false};   // Health: false once the thread leaves its loop

    std::mutex events_mutex_;
    std::vector<ProcessEvent> events_;
    static constexpr size_t kMaxBufferedEvents = 100000;
    static constexpr uintptr_t kWakeIdent = 1;  // EVFILT_USER ident for stop()
};

} // namespace pex
