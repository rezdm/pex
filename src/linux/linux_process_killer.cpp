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

namespace fs = std::filesystem;

namespace pex {

int LinuxProcessKiller::get_ppid(int pid) {
    std::ifstream file("/proc/" + std::to_string(pid) + "/stat");
    if (!file) return -1;

    std::string content;
    std::getline(file, content);

    // Format: pid (comm) state ppid ...
    // comm can contain spaces/parens, so find last ')'
    size_t comm_end = content.rfind(')');
    if (comm_end == std::string::npos) return -1;

    std::istringstream iss(content.substr(comm_end + 2));
    std::string state;
    int ppid;
    iss >> state >> ppid;
    return ppid;
}

void LinuxProcessKiller::collect_descendants_from_proc(const int root_pid, std::vector<int>& result) {
    // Build parent -> children map by scanning /proc
    std::map<int, std::vector<int>> children_map;

    try {
        std::set<int> all_pids;
        for (const auto& entry : fs::directory_iterator("/proc")) {
            try {
                if (!entry.is_directory()) continue;

                const auto& name = entry.path().filename().string();
                int pid = 0;
                auto [ptr, ec] = std::from_chars(name.data(), name.data() + name.size(), pid);
                if (ec != std::errc{} || ptr != name.data() + name.size()) continue;

                all_pids.insert(pid);
                int ppid = get_ppid(pid);
                if (ppid > 0) {
                    children_map[ppid].push_back(pid);
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
    int ret = kill(pid, signal);

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

    // Build fresh parent -> children map from /proc
    std::map<int, std::vector<int>> children_map;
    std::set<int> descendant_set;

    // First pass: collect all descendants
    std::vector<int> descendants;
    collect_descendants_from_proc(pid, descendants);

    for (int p : descendants) {
        descendant_set.insert(p);
    }

    // Second pass: build children map only for descendants
    for (int p : descendants) {
        if (int ppid = get_ppid(p); ppid > 0 && descendant_set.contains(ppid)) {
            children_map[ppid].push_back(p);
        }
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
    for (const int p : kill_order) {
        kill(p, signal);
    }

    // Check if root process was killed successfully
    if (kill(pid, 0) == 0) {
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
    } else if (errno == ESRCH) {
        // Process gone - success
        result.success = true;
        result.process_still_running = false;
        return result;
    }

    result.success = true;
    result.process_still_running = false;
    return result;
}

} // namespace pex
