#include "macos_process_killer.hpp"

#include <sys/types.h>
#include <libproc.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

namespace pex {

namespace {

// libproc process status values (from <sys/proc.h>, inlined to avoid the
// header clashing with other includes): zombie is SZOMB == 5.
constexpr uint32_t kProcZombie = 5;

bool read_bsdinfo(int pid, proc_bsdinfo& bi) {
    return proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &bi, sizeof(bi)) == static_cast<int>(sizeof(bi));
}

uint64_t pack_start_time(const proc_bsdinfo& bi) {
    return static_cast<uint64_t>(bi.pbi_start_tvsec) * 1000000ULL +
           static_cast<uint64_t>(bi.pbi_start_tvusec);
}

bool is_same_process(int pid, uint64_t token) {
    proc_bsdinfo bi;
    if (!read_bsdinfo(pid, bi)) return false;
    return pack_start_time(bi) == token;
}

bool is_zombie(int pid) {
    proc_bsdinfo bi;
    if (!read_bsdinfo(pid, bi)) return false;
    return bi.pbi_status == kProcZombie;
}

} // namespace

std::optional<uint64_t> MacosProcessKiller::process_start_token(int pid) {
    if (pid <= 0) return std::nullopt;
    proc_bsdinfo bi;
    if (!read_bsdinfo(pid, bi)) return std::nullopt;
    return pack_start_time(bi);
}

KillResult MacosProcessKiller::kill_process(int pid, bool force,
                                            std::optional<uint64_t> expected_token) {
    KillResult result;
    if (pid <= 0) {
        result.error_message = "Invalid PID";
        return result;
    }
    if (auto refusal = check_kill_token(*this, pid, expected_token)) {
        return *refusal;
    }

    const int sig = force ? SIGKILL : SIGTERM;
    if (::kill(pid, sig) == 0) {
        if (!force) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (::kill(pid, 0) == 0 && !is_zombie(pid)) {
                result.success = true;
                result.process_still_running = true;
                result.error_message = "SIGTERM sent. Process may still be running. Use Force Kill (SIGKILL) if it doesn't terminate.";
                return result;
            }
        }
        result.success = true;
        return result;
    }

    switch (errno) {
        case ESRCH:
            result.error_message = "Process not found. It may have already terminated.";
            break;
        case EPERM:
            result.error_message = "Permission denied. You may need root to signal this process.";
            break;
        default:
            result.error_message = "Failed to send signal.";
            break;
    }
    return result;
}

KillResult MacosProcessKiller::kill_process_tree(int pid, bool force,
                                                 std::optional<uint64_t> expected_token) {
    KillResult result;
    if (pid <= 0) {
        result.error_message = "Invalid PID";
        return result;
    }
    if (auto refusal = check_kill_token(*this, pid, expected_token)) {
        return *refusal;
    }

    // Snapshot every process's ppid + start time to build the tree and to
    // PID-reuse-guard each victim before signaling.
    std::unordered_map<int, int> ppid_of;
    std::unordered_map<int, uint64_t> start_of;
    int needed = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (needed > 0) {
        std::vector<pid_t> pids(needed / sizeof(pid_t) + 16);
        const int got = proc_listpids(PROC_ALL_PIDS, 0, pids.data(),
                                      static_cast<int>(pids.size() * sizeof(pid_t)));
        const int count = got > 0 ? got / static_cast<int>(sizeof(pid_t)) : 0;
        for (int i = 0; i < count; ++i) {
            const int p = pids[i];
            if (p <= 0) continue;
            proc_bsdinfo bi;
            if (!read_bsdinfo(p, bi)) continue;
            ppid_of[p] = static_cast<int>(bi.pbi_ppid);
            start_of[p] = pack_start_time(bi);
        }
    }
    if (!start_of.contains(pid)) {
        result.error_message = "Process not found. It may have already terminated.";
        return result;
    }

    // Re-validate the root against the caller's token AFTER enumeration: if the
    // root exited and its PID was reused between the initial check and this
    // snapshot, the fresh start time won't match and we must not signal the
    // whole tree of an unrelated process (issue #81).
    if (expected_token && start_of[pid] != *expected_token) {
        result.error_message = "Root process changed identity (PID reused since scan); tree kill aborted.";
        return result;
    }

    // Collect descendants (children first).
    std::set<int> tree{pid};
    bool grew = true;
    while (grew) {
        grew = false;
        for (const auto& [p, pp] : ppid_of) {
            if (tree.contains(pp) && !tree.contains(p)) {
                tree.insert(p);
                grew = true;
            }
        }
    }

    const int sig = force ? SIGKILL : SIGTERM;
    bool any_success = false;
    bool any_denied = false;
    std::vector<int> victims(tree.begin(), tree.end());
    for (auto it = victims.rbegin(); it != victims.rend(); ++it) {  // Leaves first
        if (const auto s = start_of.find(*it); s != start_of.end() && !is_same_process(*it, s->second)) {
            continue;  // Recycled since the snapshot
        }
        if (::kill(*it, sig) == 0) any_success = true;
        else if (errno == EPERM) any_denied = true;
    }

    if (any_success) {
        if (!force && ::kill(pid, 0) == 0 && !is_zombie(pid)) {
            result.success = true;
            result.process_still_running = true;
            result.error_message = "SIGTERM sent. Process tree may still be running. Use Force Kill (SIGKILL) if it doesn't terminate.";
            return result;
        }
        // Root gone. Surface a genuine partial failure (some victims were denied)
        // instead of reporting a clean success the UI silently dismisses (#81).
        if (any_denied) {
            result.error_message = "Tree kill partially failed: some processes could not be signaled (permission denied).";
            return result;
        }
        result.success = true;
        return result;
    }

    result.error_message = any_denied
        ? "Permission denied. You may need root to signal this process."
        : "Process not found. It may have already terminated.";
    return result;
}

} // namespace pex
