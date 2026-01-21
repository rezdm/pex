#pragma once

#include "data_store.hpp"
#include "procfs_reader.hpp"
#include <vector>
#include <set>
#include <string>
#include <memory>
#include <chrono>
#include <atomic>

struct GLFWwindow;

namespace pex {

class App {
public:
    App();
    void run();
    void request_focus();

private:
    void render();
    void render_menu_bar();
    void render_toolbar();
    void render_system_panel();
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

    void handle_keyboard_navigation();
    [[nodiscard]] std::vector<ProcessNode*> get_visible_items() const;

    static void collect_visible_items(ProcessNode* node, std::vector<ProcessNode*>& items);

    void search_select_first();
    void search_next();
    void search_previous();
    [[nodiscard]] bool current_selection_matches() const;
    [[nodiscard]] std::vector<ProcessNode*> find_matching_processes() const;

    static std::string format_bytes(int64_t bytes);
    static std::string format_time(std::chrono::system_clock::time_point tp);

    static void kill_process_tree(const ProcessNode* node);

    // Data store (runs in background thread)
    DataStore data_store_;
    std::shared_ptr<DataSnapshot> current_data_;

    // UI state - preserved across data updates
    int selected_pid_ = -1;
    std::set<int> collapsed_pids_;  // Track which nodes are collapsed

    bool is_tree_view_ = true;

    // Details for selected process (fetched on-demand, not from DataStore)
    ProcfsReader details_reader_;
    std::vector<FileHandleInfo> file_handles_;
    std::vector<NetworkConnectionInfo> network_connections_;
    std::vector<ThreadInfo> threads_;
    std::vector<MemoryMapInfo> memory_maps_;
    std::vector<EnvironmentVariable> environment_vars_;
    int selected_thread_idx_ = -1;
    int details_pid_ = -1;  // PID for which details were fetched

    // Search
    char search_buffer_[256] = {};
    bool scroll_to_selected_ = false;

    // System panel
    bool show_system_panel_ = true;

    // Window pointer for focus handling
    GLFWwindow* window_ = nullptr;
    std::atomic<bool> focus_requested_{false};
};

} // namespace pex
