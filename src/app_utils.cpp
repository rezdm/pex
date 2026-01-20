#include "app.hpp"
#include <format>
#include <ctime>
#include <csignal>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace pex {

std::string App::format_bytes(int64_t bytes) {
    if (bytes < 1024) return std::format("{} B", bytes);
    if (bytes < 1024 * 1024) return std::format("{:.1f} KB", bytes / 1024.0);
    if (bytes < 1024LL * 1024 * 1024) return std::format("{:.1f} MB", bytes / (1024.0 * 1024));
    return std::format("{:.2f} GB", bytes / (1024.0 * 1024 * 1024));
}

std::string App::format_time(std::chrono::system_clock::time_point tp) {
    const auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_val;
    localtime_r(&time_t_val, &tm_val);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_val);
    return buf;
}

void App::collect_subtree_pids(const ProcessNode* node, std::vector<int>& pids) {
    if (!node) return;
    pids.push_back(node->info.pid);
    for (const auto& child : node->children) {
        collect_subtree_pids(child.get(), pids);
    }
}

// Helper: get parent PID from /proc/<pid>/stat
static int get_ppid(int pid) {
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

// Build current process tree from /proc and collect all descendants
static void collect_descendants_from_proc(int root_pid, std::vector<int>& result) {
    // Build parent -> children map by scanning /proc
    std::map<int, std::vector<int>> children_map;
    std::set<int> all_pids;

    try {
        for (const auto& entry : fs::directory_iterator("/proc")) {
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
        }
    } catch (...) {
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

// Post-order traversal to get kill order (children before parents)
static void postorder_kill_order(int pid, const std::map<int, std::vector<int>>& children_map,
                                  std::set<int>& visited, std::vector<int>& order) {
    if (visited.count(pid)) return;
    visited.insert(pid);

    if (auto it = children_map.find(pid); it != children_map.end()) {
        for (int child : it->second) {
            postorder_kill_order(child, children_map, visited, order);
        }
    }
    order.push_back(pid);
}

void App::kill_process_tree(const ProcessNode* node) {
    if (!node) return;

    int root_pid = node->info.pid;

    // Build fresh parent -> children map from /proc
    std::map<int, std::vector<int>> children_map;
    std::set<int> descendant_set;

    // First pass: collect all descendants
    std::vector<int> descendants;
    collect_descendants_from_proc(root_pid, descendants);

    for (int pid : descendants) {
        descendant_set.insert(pid);
    }

    // Second pass: build children map only for descendants
    for (int pid : descendants) {
        int ppid = get_ppid(pid);
        if (ppid > 0 && descendant_set.count(ppid)) {
            children_map[ppid].push_back(pid);
        }
    }

    // Get post-order traversal (children before parents)
    std::vector<int> kill_order;
    std::set<int> visited;
    postorder_kill_order(root_pid, children_map, visited, kill_order);

    // Kill in post-order (leaves first, root last)
    for (int pid : kill_order) {
        kill(pid, SIGKILL);
    }
}

} // namespace pex
