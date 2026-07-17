#include "freebsd_process_event_source.hpp"

#include <sys/types.h>
#include <sys/event.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <unistd.h>

#include <cerrno>
#include <algorithm>
#include <vector>

namespace pex {

FreeBSDProcessEventSource::~FreeBSDProcessEventSource() {
    stop();
}

bool FreeBSDProcessEventSource::start() {
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
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
    size_t len = 0;
    if (sysctl(mib, 3, nullptr, &len, nullptr, 0) == 0 && len > 0) {
        len = len * 5 / 4;  // Headroom for processes spawned in between
        std::vector<char> buf(len);
        if (sysctl(mib, 3, buf.data(), &len, nullptr, 0) == 0) {
            const size_t count = len / sizeof(struct kinfo_proc);
            const auto* kp = reinterpret_cast<const struct kinfo_proc*>(buf.data());
            for (size_t i = 0; i < count; ++i) {
                if (kp[i].ki_pid <= 0) continue;
                struct kevent kev;
                EV_SET(&kev, static_cast<uintptr_t>(kp[i].ki_pid), EVFILT_PROC, EV_ADD,
                       NOTE_FORK | NOTE_EXEC | NOTE_EXIT | NOTE_TRACK, 0, nullptr);
                if (kevent(kq_, &kev, 1, nullptr, 0, nullptr) == 0) {
                    attached++;
                }
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
    event_thread_ = std::thread(&FreeBSDProcessEventSource::event_thread_func, this);
    return true;
}

void FreeBSDProcessEventSource::stop() {
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

bool FreeBSDProcessEventSource::is_active() const {
    // Health, not lifecycle: false once the event thread has exited (including
    // an abnormal kevent() failure), so DataStore stops advertising the feed
    // as live and the churn line disappears instead of freezing at zero.
    return active_;
}

std::vector<ProcessEvent> FreeBSDProcessEventSource::drain() {
    std::vector<ProcessEvent> result;
    std::lock_guard lock(events_mutex_);
    result.swap(events_);
    return result;
}

void FreeBSDProcessEventSource::event_thread_func() {
    // Clear the health flag on every exit path (normal stop or a fatal
    // kevent() error), so is_active() reflects a dead feed. running_ stays as
    // the lifecycle flag so stop() still joins this thread exactly once.
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

        // Accumulate this kevent() wakeup's edges locally and publish them
        // under one lock, rather than locking events_mutex_ per edge (a
        // 64-event batch can emit up to ~192 edges, all contending drain()).
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

            // One knote event can carry several fflags; emit each edge.
            // NOTE_FORK on the parent is ignored — the child's NOTE_CHILD
            // carries both PIDs of the fork edge.
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
            // NOTE_TRACKERR: a child could not be auto-attached; nothing to
            // report — that subtree degrades to poll-only coverage.
        }

        if (!batch.empty()) {
            std::lock_guard lock(events_mutex_);
            // Bound the buffer: drop the oldest to make room. drain() empties
            // this every tick, so the cap only trips under an extreme storm.
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
