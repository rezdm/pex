#pragma once

#include "process_info.hpp"
#include "procfs_reader.hpp"
#include "system_info.hpp"
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <chrono>
#include <atomic>

struct GLFWwindow;

namespace pex {

struct ProcessNode {
    ProcessInfo info;
    std::vector<std::unique_ptr<ProcessNode>> children;
    bool is_expanded = true;

    // Tree aggregate values
    int64_t tree_working_set = 0;
    double tree_memory_percent = 0.0;
    double tree_cpu_percent = 0.0;
    double tree_total_cpu_percent = 0.0;
};

class App {
public:
    App();
    void run();
    void request_focus();

private:
    void refresh_processes();
    //void build_process_tree();

    static void calculate_tree_totals(ProcessNode& node);
    void render();
    void render_menu_bar();
    void render_toolbar();
    void render_process_tree();
    void render_process_tree_node(ProcessNode& node, int depth);
    void render_process_list();
    void render_details_panel();
    void render_file_handles_tab() const;
    void render_network_tab() const;
    void render_threads_tab();
    void render_memory_tab() const;
    void render_environment_tab() const;
    void refresh_selected_details();

    void handle_search_input();
    void handle_keyboard_navigation();
    [[nodiscard]] std::vector<ProcessNode*> get_visible_items() const;

    static void collect_visible_items(ProcessNode* node, std::vector<ProcessNode*>& items);
    ProcessNode* find_matching_process(const std::string& search, ProcessNode* start_node);

    static ProcessNode* search_subtree(std::vector<std::unique_ptr<ProcessNode>>& nodes, const std::string& search);

    static std::string format_bytes(int64_t bytes);
    static std::string format_time(std::chrono::system_clock::time_point tp);

    static void collect_subtree_pids(const ProcessNode* node, std::vector<int>& pids);
    static void kill_process_tree(const ProcessNode* node);

    ProcfsReader reader_;
    std::vector<std::unique_ptr<ProcessNode>> process_tree_;
    std::map<int, ProcessNode*> process_map_;
    std::map<int, std::pair<uint64_t, uint64_t>> previous_cpu_times_;
    CpuTimes previous_system_cpu_times_;

    ProcessNode* selected_process_ = nullptr;
    int selected_pid_ = 1;

    bool is_tree_view_ = true;
    int refresh_interval_ms_ = 1000;

    // Details for selected process
    std::vector<FileHandleInfo> file_handles_;
    std::vector<NetworkConnectionInfo> network_connections_;
    std::vector<ThreadInfo> threads_;
    std::vector<MemoryMapInfo> memory_maps_;
    std::vector<EnvironmentVariable> environment_vars_;
    int selected_thread_idx_ = -1;

    // Search
    std::string search_text_;
    std::chrono::steady_clock::time_point last_key_time_;

    // Stats
    int process_count_ = 0;
    double cpu_usage_ = 0.0;
    int64_t memory_used_ = 0;
    int64_t memory_total_ = 0;

    std::chrono::steady_clock::time_point last_refresh_;

    // Window pointer for focus handling
    GLFWwindow* window_ = nullptr;
    std::atomic<bool> focus_requested_{false};
};

} // namespace pex
