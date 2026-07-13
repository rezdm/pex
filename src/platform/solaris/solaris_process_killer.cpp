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
#include <unordered_map>
#include <filesystem>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

namespace pex {

namespace {

// Pack a start time into the opaque token used by IProcessKiller
uint64_t pack_start_time(const timestruc_t& start) {
    return static_cast<uint64_t>(start.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(start.tv_nsec);
}

// Verify a PID still refers to the same process by comparing start times
bool is_same_process(int pid, const timestruc_t& expected_start) {
    std::string path = "/proc/" + std::to_string(pid) + "/psinfo";
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;

    psinfo_t psinfo;
    ssize_t n = read(fd, &psinfo, sizeof(psinfo));
    close(fd);

    if (n != static_cast<ssize_t>(sizeof(psinfo))) return false;
    return psinfo.pr_start.tv_sec == expected_start.tv_sec &&
           psinfo.pr_start.tv_nsec == expected_start.tv_nsec;
}

// Returns a failure result if the PID no longer refers to the process
// instance identified by 'token' (recycled or gone); nullopt = OK to kill.
std::optional<KillResult> check_token(SolarisProcessKiller& killer, int pid,
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

std::optional<uint64_t> SolarisProcessKiller::process_start_token(int pid) {
    if (pid <= 0) return std::nullopt;
    std::string path = "/proc/" + std::to_string(pid) + "/psinfo";
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return std::nullopt;

    psinfo_t psinfo;
    ssize_t n = read(fd, &psinfo, sizeof(psinfo));
    close(fd);

    if (n != static_cast<ssize_t>(sizeof(psinfo))) return std::nullopt;
    return pack_start_time(psinfo.pr_start);
}

KillResult SolarisProcessKiller::kill_process(int pid, bool force,
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

    if (const int sig = force ? SIGKILL : SIGTERM; ::kill(pid, sig) == 0) {
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

KillResult SolarisProcessKiller::kill_process_tree(int pid, bool force,
                                                   std::optional<uint64_t> expected_token) {
    KillResult result;
    if (pid <= 0) {
        result.success = false;
        result.error_message = "Invalid PID";
        return result;
    }
    // The tree is rebuilt from *current* /proc state, so a recycled root PID
    // would otherwise target an unrelated process's whole tree.
    if (auto refusal = check_token(*this, pid, expected_token)) {
        return *refusal;
    }
    // Collect all descendant PIDs by reading /proc
    std::set<int> pids_to_kill;
    pids_to_kill.insert(pid);

    // Build map of pid -> ppid, capturing start times for PID-reuse verification
    std::vector<std::pair<int, int>> all_procs;  // (pid, ppid)
    std::unordered_map<int, timestruc_t> start_times;

    try {
        for (const auto& entry : fs::directory_iterator("/proc")) {
            if (!entry.is_directory()) continue;

            std::string name = entry.path().filename().string();
            int proc_pid = 0;
            try {
                proc_pid = std::stoi(name);
            } catch (const std::exception&) {
                continue;
            }

            // Read psinfo to get parent PID and start time
            std::string psinfo_path = entry.path().string() + "/psinfo";
            int fd = open(psinfo_path.c_str(), O_RDONLY);
            if (fd < 0) continue;

            psinfo_t psinfo;
            ssize_t n = read(fd, &psinfo, sizeof(psinfo));
            close(fd);

            if (n == static_cast<ssize_t>(sizeof(psinfo))) {
                all_procs.emplace_back(proc_pid, psinfo.pr_ppid);
                start_times[proc_pid] = psinfo.pr_start;
            }
        }
    } catch (const std::exception&) {
        // Directory iteration failed
    }

    // Find all children iteratively
    bool found_new = true;
    while (found_new) {
        found_new = false;
        for (const auto& [proc_pid, ppid] : all_procs) {
            if (pids_to_kill.contains(ppid) && !pids_to_kill.contains(proc_pid)) {
                pids_to_kill.insert(proc_pid);
                found_new = true;
            }
        }
    }

    // Kill all processes in the tree (children first, then parent)
    int sig = force ? SIGKILL : SIGTERM;
    bool any_success = false;
    bool any_permission_denied = false;
    int skipped = 0;

    // Kill children first (reverse order)
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
        result.error_message = "Permission denied. You may need root privileges or privileges to signal this process.";
    } else {
        result.success = false;
        result.error_message = "Process not found. It may have already terminated.";
    }
    return result;
}

} // namespace pex
