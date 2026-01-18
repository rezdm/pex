#include "app.hpp"
#include <algorithm>
#include <functional>
#include <set>

namespace pex {

void App::refresh_processes() {
    auto current_cpu_times = SystemInfo::instance().get_cpu_times();
    uint64_t total_cpu_delta = current_cpu_times.total() - previous_system_cpu_times_.total();

    auto processes = reader_.get_all_processes();

    // Calculate CPU percentages
    int proc_count = SystemInfo::instance().get_processor_count();
    for (auto& proc : processes) {
        auto it = previous_cpu_times_.find(proc.pid);
        if (it != previous_cpu_times_.end() && total_cpu_delta > 0) {
            uint64_t user_delta = proc.user_time - it->second.first;
            uint64_t kernel_delta = proc.kernel_time - it->second.second;
            uint64_t process_delta = user_delta + kernel_delta;
            proc.cpu_percent = static_cast<double>(process_delta) / total_cpu_delta * 100.0 * proc_count;
            proc.total_cpu_percent = static_cast<double>(process_delta) / total_cpu_delta * 100.0;
        }
        previous_cpu_times_[proc.pid] = {proc.user_time, proc.kernel_time};
    }

    // Build tree
    process_map_.clear();
    process_tree_.clear();

    std::map<int, std::unique_ptr<ProcessNode>> nodes;
    for (auto& proc : processes) {
        auto node = std::make_unique<ProcessNode>();
        node->info = std::move(proc);
        node->is_expanded = true; // Default expanded
        nodes[node->info.pid] = std::move(node);
    }

    // Build parent-child relationships
    for (auto& [pid, node] : nodes) {
        int ppid = node->info.parent_pid;
        if (ppid != pid && nodes.contains(ppid)) {
            // Will be added to parent later
        } else {
            // Root node
        }
    }

    // Move nodes to their parents or root
    std::set<int> root_pids;
    for (auto& [pid, node] : nodes) {
        int ppid = node->info.parent_pid;
        if (ppid == pid || !nodes.contains(ppid)) {
            root_pids.insert(pid);
        }
    }

    // First pass: identify all parent relationships
    std::map<int, std::vector<int>> children_map;
    for (auto& [pid, node] : nodes) {
        int ppid = node->info.parent_pid;
        if (ppid != pid && nodes.contains(ppid)) {
            children_map[ppid].push_back(pid);
        }
    }

    // Recursive function to move children
    std::function<void(ProcessNode*, std::map<int, std::unique_ptr<ProcessNode>>&)> attach_children;
    attach_children = [&](ProcessNode* parent, std::map<int, std::unique_ptr<ProcessNode>>& all_nodes) {
        auto it = children_map.find(parent->info.pid);
        if (it != children_map.end()) {
            for (int child_pid : it->second) {
                auto child_it = all_nodes.find(child_pid);
                if (child_it != all_nodes.end()) {
                    auto child = std::move(child_it->second);
                    attach_children(child.get(), all_nodes);
                    parent->children.push_back(std::move(child));
                }
            }
        }
    };

    // Build root nodes with their children
    for (int root_pid : root_pids) {
        auto it = nodes.find(root_pid);
        if (it != nodes.end()) {
            auto root = std::move(it->second);
            attach_children(root.get(), nodes);
            process_tree_.push_back(std::move(root));
        }
    }

    // Sort tree by PID
    std::sort(process_tree_.begin(), process_tree_.end(),
        [](const auto& a, const auto& b) { return a->info.pid < b->info.pid; });

    // Build process map for quick lookup and calculate tree totals
    std::function<void(ProcessNode*)> build_map;
    build_map = [&](ProcessNode* node) {
        process_map_[node->info.pid] = node;
        for (auto& child : node->children) {
            build_map(child.get());
        }
    };
    for (auto& root : process_tree_) {
        build_map(root.get());
        calculate_tree_totals(*root);
    }

    // Restore selection
    if (auto it = process_map_.find(selected_pid_); it != process_map_.end()) {
        selected_process_ = it->second;
    } else {
        selected_process_ = nullptr;
    }

    // Update system stats
    auto mem_info = SystemInfo::instance().get_memory_info();
    memory_used_ = mem_info.used;
    memory_total_ = mem_info.total;

    if (total_cpu_delta > 0) {
        uint64_t active_delta = current_cpu_times.active() - previous_system_cpu_times_.active();
        cpu_usage_ = static_cast<double>(active_delta) / total_cpu_delta * 100.0;
    }

    previous_system_cpu_times_ = current_cpu_times;
    process_count_ = static_cast<int>(processes.size());

    // Refresh details if process is selected
    if (selected_process_) {
        refresh_selected_details();
    }
}

void App::calculate_tree_totals(ProcessNode& node) {
    node.tree_working_set = node.info.resident_memory;
    node.tree_memory_percent = node.info.memory_percent;
    node.tree_cpu_percent = node.info.cpu_percent;
    node.tree_total_cpu_percent = node.info.total_cpu_percent;

    for (auto& child : node.children) {
        calculate_tree_totals(*child);
        node.tree_working_set += child->tree_working_set;
        node.tree_memory_percent += child->tree_memory_percent;
        node.tree_cpu_percent += child->tree_cpu_percent;
        node.tree_total_cpu_percent += child->tree_total_cpu_percent;
    }
}

void App::refresh_selected_details() {
    if (!selected_process_) return;

    int pid = selected_process_->info.pid;
    file_handles_ = reader_.get_file_handles(pid);
    network_connections_ = reader_.get_network_connections(pid);
    threads_ = reader_.get_threads(pid);
    memory_maps_ = reader_.get_memory_maps(pid);
    environment_vars_ = reader_.get_environment_variables(pid);
    selected_thread_idx_ = -1;
}

} // namespace pex
