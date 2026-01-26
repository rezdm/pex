#include "freebsd_process_killer.hpp"

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <vector>
#include <set>

namespace pex {

KillResult FreeBSDProcessKiller::kill_process(int pid, bool force) {
    int sig = force ? SIGKILL : SIGTERM;

    if (::kill(pid, sig) == 0) {
        return KillResult::Success;
    }

    switch (errno) {
        case ESRCH:
            return KillResult::ProcessNotFound;
        case EPERM:
            return KillResult::PermissionDenied;
        default:
            return KillResult::Failed;
    }
}

KillResult FreeBSDProcessKiller::kill_process_tree(int pid, bool force) {
    // Collect all descendant PIDs
    std::set<int> pids_to_kill;
    std::vector<int> queue;
    queue.push_back(pid);
    pids_to_kill.insert(pid);

    // Get all processes and build parent->children map
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
    size_t len = 0;

    if (sysctl(mib, 3, nullptr, &len, nullptr, 0) < 0) {
        return KillResult::Failed;
    }

    len = len * 5 / 4;  // Extra buffer
    std::vector<char> buf(len);

    if (sysctl(mib, 3, buf.data(), &len, nullptr, 0) < 0) {
        return KillResult::Failed;
    }

    size_t count = len / sizeof(struct kinfo_proc);
    struct kinfo_proc* kp = reinterpret_cast<struct kinfo_proc*>(buf.data());

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

    // Kill children first (reverse order to kill deepest first)
    std::vector<int> sorted_pids(pids_to_kill.begin(), pids_to_kill.end());
    for (auto it = sorted_pids.rbegin(); it != sorted_pids.rend(); ++it) {
        if (::kill(*it, sig) == 0) {
            any_success = true;
        } else if (errno == EPERM) {
            any_permission_denied = true;
        }
    }

    if (any_success) {
        return KillResult::Success;
    } else if (any_permission_denied) {
        return KillResult::PermissionDenied;
    } else {
        return KillResult::ProcessNotFound;
    }
}

} // namespace pex
