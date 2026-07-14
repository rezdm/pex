#pragma once

#include "../interfaces/i_process_event_source.hpp"

#include <atomic>
#include <mutex>
#include <thread>

namespace pex {

// Process lifecycle events via the Linux proc connector
// (NETLINK_CONNECTOR + CN_PROC). Subscribing to the multicast group needs
// CAP_NET_ADMIN (see PRIVILEGES.md); without it start() returns false and
// the app runs poll-only.
class LinuxProcessEventSource : public IProcessEventSource {
public:
    LinuxProcessEventSource() = default;
    ~LinuxProcessEventSource() override;

    LinuxProcessEventSource(const LinuxProcessEventSource&) = delete;
    LinuxProcessEventSource& operator=(const LinuxProcessEventSource&) = delete;

    bool start() override;
    void stop() override;
    [[nodiscard]] bool is_active() const override;
    std::vector<ProcessEvent> drain() override;

private:
    void event_thread_func();
    void set_mcast_listen(bool enable) const;

    int netlink_fd_ = -1;
    int wake_pipe_[2] = {-1, -1};  // Self-pipe to unblock poll() on stop
    std::thread event_thread_;
    std::atomic<bool> running_{false};

    std::mutex events_mutex_;
    std::vector<ProcessEvent> events_;
    // Bound the buffer: at 1s ticks even a heavy fork storm stays far below
    // this; beyond it the oldest events are discarded.
    static constexpr size_t kMaxBufferedEvents = 100000;
};

} // namespace pex
