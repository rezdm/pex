#include "solaris_process_killer.hpp"

#include <sys/types.h>
#include <procfs.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <vector>
#include <set>
#include <filesystem>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

namespace pex {

KillResult SolarisProcessKiller::kill_process(int pid, bool force) {
    KillResult result;
    if (pid <= 0) {
        result.success = false;
        result.error_message = "Invalid PID";
        return result;
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
            result.error_message = "Permission denied. You may need root privileges or privileges to signal this process.";
            break;
        default:
            result.success = false;
            result.error_message = "Failed to send signal.";
            break;
    }
    return result;
}

KillResult SolarisProcessKiller::kill_process_tree(int pid, bool force) {
    KillResult result;
    if (pid <= 0) {
        result.success = false;
        result.error_message = "Invalid PID";
        return result;
    }
    // Collect all descendant PIDs by reading /proc
    std::set<int> pids_to_kill;
    pids_to_kill.insert(pid);

    // Build map of pid -> ppid
    std::vector<std::pair<int, int>> all_procs;  // (pid, ppid)

    try {
        for (const auto& entry : fs::directory_iterator("/proc")) {
            if (!entry.is_directory()) continue;

            std::string name = entry.path().filename().string();
            int proc_pid = 0;
            try {
                proc_pid = std::stoi(name);
            } catch (...) {
                continue;
            }

            // Read psinfo to get parent PID
            std::string psinfo_path = entry.path().string() + "/psinfo";
            int fd = open(psinfo_path.c_str(), O_RDONLY);
            if (fd < 0) continue;

            psinfo_t psinfo;
            ssize_t n = read(fd, &psinfo, sizeof(psinfo));
            close(fd);

            if (n == sizeof(psinfo)) {
                all_procs.emplace_back(proc_pid, psinfo.pr_ppid);
            }
        }
    } catch (...) {
        // Directory iteration failed
    }

    // Find all children iteratively
    bool found_new = true;
    while (found_new) {
        found_new = false;
        for (const auto& [proc_pid, ppid] : all_procs) {
            if (pids_to_kill.count(ppid) && !pids_to_kill.count(proc_pid)) {
                pids_to_kill.insert(proc_pid);
                found_new = true;
            }
        }
    }

    // Kill all processes in the tree (children first, then parent)
    int sig = force ? SIGKILL : SIGTERM;
    bool any_success = false;
    bool any_permission_denied = false;

    // Kill children first (reverse order)
    std::vector<int> sorted_pids(pids_to_kill.begin(), pids_to_kill.end());
    for (auto it = sorted_pids.rbegin(); it != sorted_pids.rend(); ++it) {
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
        result.error_message = "Permission denied. You may need root privileges or privileges to signal this process.";
    } else {
        result.success = false;
        result.error_message = "Process not found. It may have already terminated.";
    }
    return result;
}

} // namespace pex
