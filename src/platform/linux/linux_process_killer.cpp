#include "linux_process_killer.hpp"
#include <format>
#include <csignal>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <thread>
#include <charconv>
#include <sstream>

namespace fs = std::filesystem;

namespace pex {

bool LinuxProcessKiller::read_proc_meta(const int pid, ProcMeta& out) {
    std::ifstream file("/proc/" + std::to_string(pid) + "/stat");
    if (!file) return false;

    std::string content;
    std::getline(file, content);

    // Format: pid (comm) state ppid ...
    // comm can contain spaces/parens, so find last ')'
    size_t comm_end = content.rfind(')');
    if (comm_end == std::string::npos) return false;

    std::istringstream iss(content.substr(comm_end + 2));
    std::string state;
    int ppid = 0, pgrp = 0, session = 0, tty_nr = 0, tpgid = 0;
    unsigned int flags = 0;
    uint64_t minflt = 0, cminflt = 0, majflt = 0, cmajflt = 0, utime = 0, stime = 0;
    int64_t cutime = 0, cstime = 0, priority = 0, nice = 0;
    int64_t num_threads = 0, itrealvalue = 0;
    uint64_t starttime = 0;

    iss >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags
        >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime
        >> cutime >> cstime >> priority >> nice >> num_threads >> itrealvalue >> starttime;

    if (iss.fail()) {
        return false;
    }

    out.ppid = ppid;
    out.starttime = starttime;
    return true;
}

int LinuxProcessKiller::get_ppid(int pid) {
    ProcMeta meta;
    if (!read_proc_meta(pid, meta)) return -1;
    return meta.ppid;
}

bool LinuxProcessKiller::is_same_process(const int pid, const uint64_t starttime) {
    ProcMeta meta;
    if (!read_proc_meta(pid, meta)) return false;
    return meta.starttime == starttime;
}

void LinuxProcessKiller::collect_descendants_from_proc(const int root_pid,
                                                       std::vector<int>& result,
                                                       std::map<int, std::vector<int>>& children_map,
                                                       std::map<int, uint64_t>& start_times) {
    // Build parent -> children map by scanning /proc
    try {
        for (const auto& entry : fs::directory_iterator("/proc")) {
            try {
                if (!entry.is_directory()) continue;

                const auto& name = entry.path().filename().string();
                int pid = 0;
                auto [ptr, ec] = std::from_chars(name.data(), name.data() + name.size(), pid);
                if (ec != std::errc{} || ptr != name.data() + name.size()) continue;

                ProcMeta meta;
                if (!read_proc_meta(pid, meta)) continue;

                start_times[pid] = meta.starttime;
                if (meta.ppid > 0) {
                    children_map[meta.ppid].push_back(pid);
                }
            } catch (...) {
                // Process disappeared, skip it
                continue;
            }
        }
    } catch (...) {
        // Directory iteration failed
        return;
    }

    // BFS/DFS to collect all descendants of root_pid
    std::vector<int> stack;
    stack.push_back(root_pid);

    while (!stack.empty()) {
        int pid = stack.back();
        stack.pop_back();

        result.push_back(pid);

        if (auto it = children_map.find(pid); it != children_map.end()) {
            for (int child : it->second) {
                stack.push_back(child);
            }
        }
    }
}

std::string LinuxProcessKiller::get_kill_error_message(int err) {
    switch (err) {
        case EPERM:
            return "Permission denied. You may need root privileges or CAP_KILL capability to signal this process.";
        case ESRCH:
            return "Process not found. It may have already terminated.";
        case EINVAL:
            return "Invalid signal.";
        default:
            return std::format("Failed to send signal: {} (errno {})", strerror(err), err);
    }
}

KillResult LinuxProcessKiller::kill_process(int pid, bool force) {
    KillResult result;

    if (pid <= 0) {
        result.success = false;
        result.error_message = "Invalid PID";
        return result;
    }

    const int signal = force ? SIGKILL : SIGTERM;
    const int ret = kill(pid, signal);

    if (ret == -1) {
        result.success = false;
        result.error_message = get_kill_error_message(errno);
        if (errno != ESRCH) {
            result.process_still_running = true;
        }
        return result;
    }

    // Give process a moment to terminate, then check if still alive
    if (!force) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (kill(pid, 0) == 0) {
            // Still running
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

KillResult LinuxProcessKiller::kill_process_tree(int pid, bool force) {
    KillResult result;

    if (pid <= 0) {
        result.success = false;
        result.error_message = "Invalid PID";
        return result;
    }

    // Build fresh parent -> children map and capture start times from /proc
    std::map<int, std::vector<int>> children_map;
    std::map<int, uint64_t> start_times;

    // First pass: collect all descendants
    std::vector<int> descendants;
    collect_descendants_from_proc(pid, descendants, children_map, start_times);
    if (descendants.empty() || !start_times.contains(pid)) {
        result.success = false;
        result.error_message = "Process not found. It may have already terminated.";
        return result;
    }

    // Post-order traversal to get kill order (children before parents)
    std::vector<int> kill_order;
    std::set<int> visited;

    std::function<void(int)> postorder = [&](int p) {
        if (visited.contains(p)) return;
        visited.insert(p);

        if (const auto it = children_map.find(p); it != children_map.end()) {
            for (const int child : it->second) {
                postorder(child);
            }
        }
        kill_order.push_back(p);
    };

    postorder(pid);

    // Kill in post-order (leaves first, root last)
    const int signal = force ? SIGKILL : SIGTERM;
    int skipped = 0;
    int failed = 0;
    for (const int p : kill_order) {
        auto it = start_times.find(p);
        if (it == start_times.end()) {
            skipped++;
            continue;
        }
        if (!is_same_process(p, it->second)) {
            skipped++;
            continue;
        }
        if (kill(p, signal) == -1 && errno != ESRCH) {
            failed++;
        }
    }

    // Check if root process was killed successfully
    bool root_same = false;
    if (auto it = start_times.find(pid); it != start_times.end()) {
        root_same = is_same_process(pid, it->second);
    }

    if (root_same) {
        const int check = kill(pid, 0);
        if (check == -1 && errno == EPERM) {
            result.success = false;
            result.process_still_running = true;
            result.error_message = get_kill_error_message(errno);
            return result;
        }
        if (check == 0) {
            // Process still exists - if we used SIGTERM, offer force kill
            if (!force) {
                result.success = true;
                result.process_still_running = true;
                result.error_message = "SIGTERM sent. Process may still be running. Use Force Kill (SIGKILL) if it doesn't terminate.";
                return result;
            }
            result.success = false;
            result.process_still_running = true;
            result.error_message = "Process tree kill failed - some processes may still be running";
            return result;
        }
    }

    result.success = true;
    result.process_still_running = false;

    if ((skipped > 0 || failed > 0) && result.error_message.empty()) {
        result.error_message = std::format(
            "Kill tree completed with warnings: skipped {} process(es), failed to signal {} process(es).",
            skipped, failed);
    }

    return result;
}

} // namespace pex
