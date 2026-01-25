#pragma once

#include "../interfaces/i_process_data_provider.hpp"
#include "../interfaces/i_system_data_provider.hpp"
#include "../interfaces/i_process_killer.hpp"
#include "../data_store.hpp"
#include "../viewmodels/app_view_model.hpp"
#include "../name_resolver.hpp"
#include <memory>
#include <atomic>
#include <mutex>
#include <chrono>

struct GLFWwindow;

namespace pex {

class ImGuiApp {
public:
    // Non-owning constructor: ImGuiApp uses but does not own the data layer.
    // All pointers must be non-null and must outlive the ImGuiApp instance.
    // - data_store: background data collection (managed externally)
    // - system_provider: for system config queries (ticks/sec, cpu count)
    // - details_provider: for on-demand detail fetches (threads, files, etc.)
    // - killer: for process termination
    // Precondition: all pointers != nullptr (asserted in constructor)
    ImGuiApp(DataStore* data_store,
             ISystemDataProvider* system_provider,
             IProcessDataProvider* details_provider,
             IProcessKiller* killer);
    ~ImGuiApp();

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
    void render_kill_confirmation_dialog();
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

    // Kill functionality
    void request_kill_process(int pid, const std::string& name, bool is_tree);
    void execute_kill(bool force);
    static void collect_tree_pids(const ProcessNode* node, std::vector<int>& pids);

    // Non-owned references to data layer (managed externally)
    DataStore* data_store_ = nullptr;
    ISystemDataProvider* system_provider_ = nullptr;  // For config queries (ticks/sec, cpu count)
    IProcessDataProvider* details_provider_ = nullptr;
    IProcessKiller* killer_ = nullptr;

    // Current snapshot from data store
    std::shared_ptr<DataSnapshot> current_data_;

    // ViewModel (holds all UI state - single source of truth)
    AppViewModel view_model_;

    // Window pointer for focus handling
    GLFWwindow* window_ = nullptr;
    std::atomic<bool> focus_requested_{false};

    // Name resolver for DNS and service lookups
    NameResolver name_resolver_;

    // Event debouncing
    void post_empty_event_debounced();
    std::mutex event_debounce_mutex_;
    std::chrono::steady_clock::time_point last_event_post_time_;
    static constexpr auto kEventDebounceInterval = std::chrono::milliseconds(16);
};

} // namespace pex
