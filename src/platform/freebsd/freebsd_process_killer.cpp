#include "freebsd_process_killer.hpp"

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <vector>
#include <set>
#include <unordered_map>
#include <thread>
#include <chrono>

namespace pex {

namespace {

// Pack a start time into the opaque token used by IProcessKiller
uint64_t pack_start_time(const struct timeval& start) {
    return static_cast<uint64_t>(start.tv_sec) * 1000000ULL +
           static_cast<uint64_t>(start.tv_usec);
}

// Verify a PID still refers to the same process by comparing start times
bool is_same_process(int pid, const struct timeval& expected_start) {
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, pid };
    struct kinfo_proc kp;
    size_t len = sizeof(kp);
    if (sysctl(mib, 4, &kp, &len, nullptr, 0) < 0 || len != sizeof(kp)) {
        return false;
    }
    return kp.ki_start.tv_sec == expected_start.tv_sec &&
           kp.ki_start.tv_usec == expected_start.tv_usec;
}

// Returns a failure result if the PID no longer refers to the process
// instance identified by 'token' (recycled or gone); nullopt = OK to kill.
std::optional<KillResult> check_token(FreeBSDProcessKiller& killer, int pid,
                                      const std::optional<uint64_t>& token) {
    if (!token) return std::nullopt;
    KillResult result;
    const auto current = killer.process_start_token(pid);
    if (!current) {
        result.error_message = "Process not found. It may have already terminated.";
        return result;
    }
    if (*current != *token) {
        result.error_message =
            "PID was reused by a different process since the dialog opened. Kill aborted.";
        return result;
    }
    return std::nullopt;
}

} // anonymous namespace

std::optional<uint64_t> FreeBSDProcessKiller::process_start_token(int pid) {
    if (pid <= 0) return std::nullopt;
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, pid };
    struct kinfo_proc kp;
    size_t len = sizeof(kp);
    if (sysctl(mib, 4, &kp, &len, nullptr, 0) < 0 || len != sizeof(kp)) {
        return std::nullopt;
    }
    return pack_start_time(kp.ki_start);
}

KillResult FreeBSDProcessKiller::kill_process(int pid, bool force,
                                              std::optional<uint64_t> expected_token) {
    KillResult result;
    if (pid <= 0) {
        result.success = false;
        result.error_message = "Invalid PID";
        return result;
    }

    if (auto refusal = check_token(*this, pid, expected_token)) {
        return *refusal;
    }

    int sig = force ? SIGKILL : SIGTERM;

    if (::kill(pid, sig) == 0) {
        if (!force) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (::kill(pid, 0) == 0) {
                result.success = true;
                result.process_still_running = true;
                result.error_message = "SIGTERM sent. Process may still be running. Use Force Kill (SIGKILL) if it doesn't terminate.";
                return result;
            }
        }
        result.success = true;
        result.process_still_running = false;
        return result;
    }

    switch (errno) {
        case ESRCH:
            result.success = false;
            result.error_message = "Process not found. It may have already terminated.";
            break;
        case EPERM:
            result.success = false;
            result.error_message = "Permission denied. You may need root privileges or CAP_KILL capability to signal this process.";
            break;
        default:
            result.success = false;
            result.error_message = "Failed to send signal.";
            break;
    }
    return result;
}

KillResult FreeBSDProcessKiller::kill_process_tree(int pid, bool force,
                                                   std::optional<uint64_t> expected_token) {
    KillResult result;
    if (pid <= 0) {
        result.success = false;
        result.error_message = "Invalid PID";
        return result;
    }
    // The tree is rebuilt from *current* kernel state, so a recycled root PID
    // would otherwise target an unrelated process's whole tree.
    if (auto refusal = check_token(*this, pid, expected_token)) {
        return *refusal;
    }
    // Collect all descendant PIDs
    std::set<int> pids_to_kill;
    std::vector<int> queue;
    queue.push_back(pid);
    pids_to_kill.insert(pid);

    // Get all processes and build parent->children map
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
    size_t len = 0;

    if (sysctl(mib, 3, nullptr, &len, nullptr, 0) < 0) {
        result.success = false;
        result.error_message = "Failed to enumerate processes.";
        return result;
    }

    len = len * 5 / 4;  // Extra buffer
    std::vector<char> buf(len);

    if (sysctl(mib, 3, buf.data(), &len, nullptr, 0) < 0) {
        result.success = false;
        result.error_message = "Failed to enumerate processes.";
        return result;
    }

    size_t count = len / sizeof(struct kinfo_proc);
    struct kinfo_proc* kp = reinterpret_cast<struct kinfo_proc*>(buf.data());

    // Capture start times for PID-reuse verification
    std::unordered_map<int, struct timeval> start_times;
    for (size_t i = 0; i < count; ++i) {
        start_times[kp[i].ki_pid] = kp[i].ki_start;
    }

    // Find all children iteratively
    bool found_new = true;
    while (found_new) {
        found_new = false;
        for (size_t i = 0; i < count; ++i) {
            if (pids_to_kill.count(kp[i].ki_ppid) && !pids_to_kill.count(kp[i].ki_pid)) {
                pids_to_kill.insert(kp[i].ki_pid);
                found_new = true;
            }
        }
    }

    // Kill all processes in the tree (children first, then parent)
    int sig = force ? SIGKILL : SIGTERM;
    bool any_success = false;
    bool any_permission_denied = false;
    int skipped = 0;

    // Kill children first (reverse order to kill deepest first)
    std::vector<int> sorted_pids(pids_to_kill.begin(), pids_to_kill.end());
    for (auto it = sorted_pids.rbegin(); it != sorted_pids.rend(); ++it) {
        // Verify PID hasn't been reused before killing
        if (auto st = start_times.find(*it); st != start_times.end()) {
            if (!is_same_process(*it, st->second)) {
                skipped++;
                continue;
            }
        }
        if (::kill(*it, sig) == 0) {
            any_success = true;
        } else if (errno == EPERM) {
            any_permission_denied = true;
        }
    }

    if (any_success) {
        if (!force) {
            if (::kill(pid, 0) == 0) {
                result.success = true;
                result.process_still_running = true;
                result.error_message = "SIGTERM sent. Process tree may still be running. Use Force Kill (SIGKILL) if it doesn't terminate.";
                return result;
            }
        }
        result.success = true;
        result.process_still_running = false;
        return result;
    }

    if (any_permission_denied) {
        result.success = false;
        result.error_message = "Permission denied. You may need root privileges or CAP_KILL capability to signal this process.";
    } else {
        result.success = false;
        result.error_message = "Process not found. It may have already terminated.";
    }
    return result;
}

} // namespace pex
