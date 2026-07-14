#pragma once

#include "../interfaces/i_process_event_source.hpp"

#include <atomic>
#include <mutex>
#include <thread>

namespace pex {

// Process lifecycle events via kqueue EVFILT_PROC. At start every existing
// PID is attached with NOTE_FORK|NOTE_EXEC|NOTE_EXIT|NOTE_TRACK; NOTE_TRACK
// makes the kernel attach descendants automatically, and NOTE_CHILD on the
// child carries the fork edge (child + parent PID).
//
// Best-effort by design: without root only processes the user may observe
// can be attached, and children forked by never-attached processes are
// missed. Poll-based collection remains the source of truth.
class FreeBSDProcessEventSource : public IProcessEventSource {
public:
    FreeBSDProcessEventSource() = default;
    ~FreeBSDProcessEventSource() override;

    FreeBSDProcessEventSource(const FreeBSDProcessEventSource&) = delete;
    FreeBSDProcessEventSource& operator=(const FreeBSDProcessEventSource&) = delete;

    bool start() override;
    void stop() override;
    [[nodiscard]] bool is_active() const override;
    std::vector<ProcessEvent> drain() override;

private:
    void event_thread_func();

    int kq_ = -1;
    std::thread event_thread_;
    std::atomic<bool> running_{false};

    std::mutex events_mutex_;
    std::vector<ProcessEvent> events_;
    static constexpr size_t kMaxBufferedEvents = 100000;
    static constexpr uintptr_t kWakeIdent = 1;  // EVFILT_USER ident for stop()
};

} // namespace pex
