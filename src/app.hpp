#pragma once

#include "data_store.hpp"
#include "procfs_reader.hpp"
#include "name_resolver.hpp"
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
    void render_system_panel() const;
    void render_process_tree();
    void render_process_tree_node(ProcessNode& node, int depth);
    void render_process_list();
    void render_details_panel();
    void render_file_handles_tab();
    void render_network_tab();
    void render_threads_tab();
    void render_memory_tab();
    void render_environment_tab();
    void render_libraries_tab();
    void render_process_popup();
    void refresh_selected_details();
    void update_popup_history();

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

    // Kill confirmation dialog
    void render_kill_confirmation_dialog();
    void request_kill_process(int pid, const std::string& name, bool is_tree);
    void execute_kill(bool force);
    static void kill_process_tree_impl(int root_pid, bool force);
    static std::string get_kill_error_message(int err);

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
    std::vector<LibraryInfo> libraries_;
    int selected_thread_idx_ = -1;
    int details_pid_ = -1;  // PID for which details were fetched
    int cached_stack_tid_ = -1;  // TID for which stack is cached
    std::string cached_stack_;    // Cached stack trace (fetched on-demand)

    // Details tab tracking for on-demand refresh
    enum class DetailsTab { FileHandles, Network, Threads, Memory, Environment, Libraries };
    DetailsTab active_details_tab_ = DetailsTab::FileHandles;

    // Search
    char search_buffer_[256] = {};
    bool scroll_to_selected_ = false;
    bool focus_search_box_ = false;

    // Process popup (double-click)
    bool show_process_popup_ = false;
    int popup_pid_ = -1;
    bool popup_show_tree_ = false;  // Toggle: false = process only, true = process + descendants
    static constexpr size_t kHistorySize = 60;  // 60 data points
    std::vector<float> popup_cpu_user_history_;
    std::vector<float> popup_cpu_kernel_history_;
    std::vector<float> popup_memory_history_;
    std::vector<std::vector<float>> popup_per_cpu_user_history_;
    std::vector<std::vector<float>> popup_per_cpu_kernel_history_;
    uint64_t popup_prev_utime_ = 0;
    uint64_t popup_prev_stime_ = 0;
    std::chrono::steady_clock::time_point popup_last_update_;
    static void collect_tree_pids(const ProcessNode* node, std::vector<int>& pids);

    // System panel
    bool show_system_panel_ = true;

    // Window pointer for focus handling
    GLFWwindow* window_ = nullptr;
    std::atomic<bool> focus_requested_{false};

    // Name resolver for DNS and service lookups
    NameResolver name_resolver_;

    // Kill confirmation dialog
    bool show_kill_dialog_ = false;
    int kill_target_pid_ = -1;
    std::string kill_target_name_;
    bool kill_is_tree_ = false;
    std::string kill_error_message_;
    bool kill_show_force_option_ = false;  // Show force kill after SIGTERM fails

    // List view sorting state (tree view doesn't support sorting)
    int list_sort_col_ = 1;  // Default: PID column
    bool list_sort_asc_ = true;

    // Details panel sorting state
    int file_handles_sort_col_ = 0;
    bool file_handles_sort_asc_ = true;
    int network_sort_col_ = 0;
    bool network_sort_asc_ = true;
    int threads_sort_col_ = 0;
    bool threads_sort_asc_ = true;
    int memory_sort_col_ = 0;
    bool memory_sort_asc_ = true;
    int environment_sort_col_ = 0;
    bool environment_sort_asc_ = true;
    int libraries_sort_col_ = 0;
    bool libraries_sort_asc_ = true;

    // Event debouncing to prevent glfwPostEmptyEvent floods
    void post_empty_event_debounced();
    std::mutex event_debounce_mutex_;
    std::chrono::steady_clock::time_point last_event_post_time_;
    static constexpr auto kEventDebounceInterval = std::chrono::milliseconds(16);  // ~60fps max
};

} // namespace pex
