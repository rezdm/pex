#include "linux_process_event_source.hpp"

#include <linux/cn_proc.h>
#include <linux/connector.h>
#include <linux/netlink.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <algorithm>

namespace pex {

// cn_msg ends in a flexible array member, so the subscription request
// (nlmsghdr + cn_msg + op) is assembled in a raw buffer.
static constexpr size_t kMcastRequestSize =
    sizeof(nlmsghdr) + sizeof(cn_msg) + sizeof(proc_cn_mcast_op);

// The parts are memcpy'd back to back; that only matches the kernel's
// expected layout (payload at NLMSG_DATA, i.e. NLMSG_ALIGN'ed header) as
// long as the header size is already netlink-aligned.
static_assert(sizeof(nlmsghdr) % NLMSG_ALIGNTO == 0,
              "cn_msg offset would need NLMSG_ALIGN padding");

LinuxProcessEventSource::~LinuxProcessEventSource() {
    stop();
}

bool LinuxProcessEventSource::start() {
    if (running_) return true;

    netlink_fd_ = socket(PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_CONNECTOR);
    if (netlink_fd_ < 0) {
        return false;
    }

    sockaddr_nl addr{};
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = CN_IDX_PROC;
    addr.nl_pid = 0;  // Kernel assigns a unique address

    // EPERM here is the normal unprivileged case: joining the proc-connector
    // multicast group requires CAP_NET_ADMIN.
    if (bind(netlink_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(netlink_fd_);
        netlink_fd_ = -1;
        return false;
    }

    if (pipe(wake_pipe_) < 0) {
        close(netlink_fd_);
        netlink_fd_ = -1;
        return false;
    }

    running_ = true;
    active_ = true;
    set_mcast_listen(true);
    event_thread_ = std::thread(&LinuxProcessEventSource::event_thread_func, this);
    return true;
}

void LinuxProcessEventSource::stop() {
    if (!running_.exchange(false)) return;

    set_mcast_listen(false);  // Best-effort unsubscribe

    // Wake the poll() in the event thread, join, then close fds (closing
    // before the join would race fd reuse).
    const char byte = 0;
    (void)!write(wake_pipe_[1], &byte, 1);
    if (event_thread_.joinable()) {
        event_thread_.join();
    }

    close(netlink_fd_);
    netlink_fd_ = -1;
    close(wake_pipe_[0]);
    close(wake_pipe_[1]);
    wake_pipe_[0] = wake_pipe_[1] = -1;
}

bool LinuxProcessEventSource::is_active() const {
    // Health, not lifecycle: false once the event thread has exited (including
    // an abnormal recv()/poll() failure), so DataStore stops advertising the
    // feed as live and the churn line disappears instead of freezing at zero.
    return active_;
}

std::vector<ProcessEvent> LinuxProcessEventSource::drain() {
    std::vector<ProcessEvent> result;
    std::lock_guard lock(events_mutex_);
    result.swap(events_);
    return result;
}

void LinuxProcessEventSource::set_mcast_listen(const bool enable) const {
    // Built as ordinary structs and memcpy'd into the wire buffer: writing
    // through pointers cast into a char array would violate strict aliasing.
    nlmsghdr hdr{};
    hdr.nlmsg_len = kMcastRequestSize;
    hdr.nlmsg_type = NLMSG_DONE;
    hdr.nlmsg_pid = static_cast<uint32_t>(getpid());

    cn_msg msg;
    std::memset(&msg, 0, sizeof(msg));  // {}-init trips on the flexible array member
    msg.id.idx = CN_IDX_PROC;
    msg.id.val = CN_VAL_PROC;
    msg.len = sizeof(proc_cn_mcast_op);

    const proc_cn_mcast_op op = enable ? PROC_CN_MCAST_LISTEN : PROC_CN_MCAST_IGNORE;

    char buf[kMcastRequestSize];
    size_t offset = 0;
    std::memcpy(buf + offset, &hdr, sizeof(hdr));
    offset += sizeof(hdr);
    std::memcpy(buf + offset, &msg, sizeof(msg));
    offset += sizeof(msg);
    std::memcpy(buf + offset, &op, sizeof(op));

    (void)!send(netlink_fd_, buf, sizeof(buf), 0);
}

void LinuxProcessEventSource::event_thread_func() {
    // Clear the health flag on every exit path (normal stop or a fatal
    // socket error), so is_active() reflects a dead feed. running_ stays as
    // the lifecycle flag so stop() still joins this thread exactly once.
    struct ClearActiveOnExit {
        std::atomic<bool>& flag;
        ~ClearActiveOnExit() { flag = false; }
    } clear_active{active_};

    // Large enough for a batch of netlink messages
    alignas(NLMSG_ALIGNTO) char buffer[16 * 1024];

    pollfd fds[2];
    fds[0] = {netlink_fd_, POLLIN, 0};
    fds[1] = {wake_pipe_[0], POLLIN, 0};

    while (running_) {
        const int ret = poll(fds, 2, -1);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (fds[1].revents & POLLIN) break;  // stop() woke us
        if (!(fds[0].revents & POLLIN)) continue;

        const ssize_t received = recv(netlink_fd_, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            if (received < 0 && (errno == EINTR || errno == ENOBUFS)) continue;
            break;
        }
        const auto now = std::chrono::steady_clock::now();

        // Accumulate this recv() batch locally and publish it under one lock,
        // rather than locking events_mutex_ once per event (a fork storm can
        // deliver thousands per wakeup, all contending with drain()).
        std::vector<ProcessEvent> batch;

        // NLMSG_OK/NLMSG_NEXT consume 'len' as they walk the batch
        auto len = static_cast<unsigned int>(received);
        for (nlmsghdr* nlh = reinterpret_cast<nlmsghdr*>(buffer);
             NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
            if (nlh->nlmsg_type != NLMSG_DONE) continue;

            const auto* cn = static_cast<const cn_msg*>(NLMSG_DATA(nlh));
            if (cn->id.idx != CN_IDX_PROC || cn->id.val != CN_VAL_PROC) continue;

            // cn->data sits at an offset that is not 8-byte aligned within the
            // recv buffer, but proc_event contains a __u64 — dereferencing a
            // proc_event* there is misaligned UB (UBSan aborts; strict-align
            // targets fault). Copy it into a naturally-aligned local first.
            proc_event ev{};
            std::memcpy(&ev, cn->data, std::min(sizeof(ev), static_cast<size_t>(cn->len)));

            ProcessEvent out;
            out.timestamp = now;

            switch (ev.what) {
                case PROC_EVENT_FORK:
                    // Thread creation also reports FORK; a new *process* has
                    // child pid == child tgid.
                    if (ev.event_data.fork.child_pid != ev.event_data.fork.child_tgid) continue;
                    out.type = ProcessEventType::Fork;
                    out.pid = ev.event_data.fork.child_tgid;
                    out.parent_pid = ev.event_data.fork.parent_tgid;
                    break;
                case PROC_EVENT_EXEC:
                    if (ev.event_data.exec.process_pid != ev.event_data.exec.process_tgid) continue;
                    out.type = ProcessEventType::Exec;
                    out.pid = ev.event_data.exec.process_tgid;
                    break;
                case PROC_EVENT_EXIT:
                    // Thread exits report EXIT too; the process is gone only
                    // when the exiting task is the group leader.
                    if (ev.event_data.exit.process_pid != ev.event_data.exit.process_tgid) continue;
                    out.type = ProcessEventType::Exit;
                    out.pid = ev.event_data.exit.process_tgid;
                    out.exit_code = static_cast<int>(ev.event_data.exit.exit_code);
                    break;
                default:
                    continue;  // ACK/UID/GID/SID/COMM/COREDUMP not needed
            }

            batch.push_back(out);
        }

        if (!batch.empty()) {
            std::lock_guard lock(events_mutex_);
            // Bound the buffer: drop the oldest events to make room for the
            // batch. drain() empties this every tick, so the cap only trips
            // under an extreme storm.
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
