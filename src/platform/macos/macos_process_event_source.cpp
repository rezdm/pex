#include "macos_process_event_source.hpp"

#include <sys/event.h>
#include <sys/types.h>
#include <libproc.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <vector>

namespace pex {

MacosProcessEventSource::~MacosProcessEventSource() {
    stop();
}

bool MacosProcessEventSource::start() {
    if (running_) return true;

    kq_ = kqueue();
    if (kq_ < 0) return false;

    // Wake-up event for stop()
    struct kevent wake;
    EV_SET(&wake, kWakeIdent, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    if (kevent(kq_, &wake, 1, nullptr, 0, nullptr) < 0) {
        close(kq_);
        kq_ = -1;
        return false;
    }

    // Attach to every existing process; NOTE_TRACK follows their descendants.
    // Per-PID failures (EPERM for foreign processes without root, ESRCH for
    // races) are expected — attach whatever we may observe.
    int attached = 0;
    int needed = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (needed > 0) {
        std::vector<pid_t> pids(needed / sizeof(pid_t) + 16);
        const int got = proc_listpids(PROC_ALL_PIDS, 0, pids.data(),
                                      static_cast<int>(pids.size() * sizeof(pid_t)));
        const int count = got > 0 ? got / static_cast<int>(sizeof(pid_t)) : 0;
        for (int i = 0; i < count; ++i) {
            if (pids[i] <= 0) continue;
            struct kevent kev;
            EV_SET(&kev, static_cast<uintptr_t>(pids[i]), EVFILT_PROC, EV_ADD,
                   NOTE_FORK | NOTE_EXEC | NOTE_EXIT | NOTE_TRACK, 0, nullptr);
            if (kevent(kq_, &kev, 1, nullptr, 0, nullptr) == 0) {
                attached++;
            }
        }
    }

    if (attached == 0) {
        close(kq_);
        kq_ = -1;
        return false;
    }

    running_ = true;
    active_ = true;
    event_thread_ = std::thread(&MacosProcessEventSource::event_thread_func, this);
    return true;
}

void MacosProcessEventSource::stop() {
    if (!running_.exchange(false)) return;

    struct kevent trigger;
    EV_SET(&trigger, kWakeIdent, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
    (void)kevent(kq_, &trigger, 1, nullptr, 0, nullptr);

    if (event_thread_.joinable()) {
        event_thread_.join();
    }

    close(kq_);
    kq_ = -1;
}

bool MacosProcessEventSource::is_active() const {
    // Health, not lifecycle: false once the thread has exited (including an
    // abnormal kevent() failure), so DataStore stops advertising the feed.
    return active_;
}

std::vector<ProcessEvent> MacosProcessEventSource::drain() {
    std::vector<ProcessEvent> result;
    std::lock_guard lock(events_mutex_);
    result.swap(events_);
    return result;
}

void MacosProcessEventSource::event_thread_func() {
    struct ClearActiveOnExit {
        std::atomic<bool>& flag;
        ~ClearActiveOnExit() { flag = false; }
    } clear_active{active_};

    struct kevent kevs[64];

    while (running_) {
        const int n = kevent(kq_, nullptr, 0, kevs, 64, nullptr);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        const auto now = std::chrono::steady_clock::now();

        // Accumulate this wakeup's edges locally and publish under one lock.
        std::vector<ProcessEvent> batch;
        auto emit = [&](const ProcessEventType type, const int pid,
                        const int parent, const int code) {
            ProcessEvent out;
            out.type = type;
            out.pid = pid;
            out.parent_pid = parent;
            out.exit_code = code;
            out.timestamp = now;
            batch.push_back(out);
        };

        for (int i = 0; i < n; ++i) {
            const struct kevent& kev = kevs[i];
            if (kev.filter == EVFILT_USER) {
                if (!running_) return;
                continue;
            }
            if (kev.filter != EVFILT_PROC) continue;

            const int pid = static_cast<int>(kev.ident);
            if (kev.fflags & NOTE_CHILD) {
                emit(ProcessEventType::Fork, pid, static_cast<int>(kev.data), 0);
            }
            if (kev.fflags & NOTE_EXEC) {
                emit(ProcessEventType::Exec, pid, 0, 0);
            }
            if (kev.fflags & NOTE_EXIT) {
                emit(ProcessEventType::Exit, pid, 0, static_cast<int>(kev.data));
            }
        }

        if (!batch.empty()) {
            std::lock_guard lock(events_mutex_);
            if (events_.size() + batch.size() > kMaxBufferedEvents) {
                const size_t overflow = events_.size() + batch.size() - kMaxBufferedEvents;
                events_.erase(events_.begin(),
                              events_.begin() + static_cast<long>(std::min(overflow, events_.size())));
            }
            events_.insert(events_.end(), batch.begin(), batch.end());
        }
    }
}

} // namespace pex
