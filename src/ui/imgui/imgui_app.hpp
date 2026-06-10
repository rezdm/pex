#pragma once

#include "../../platform/interfaces/i_process_data_provider.hpp"
#include "../../platform/interfaces/i_system_data_provider.hpp"
#include "../../platform/interfaces/i_process_killer.hpp"
#include "../../core/services/data_store.hpp"
#include "../common/viewmodels/app_view_model.hpp"
#include "../../core/services/name_resolver.hpp"
#include <memory>
#include <atomic>
#include <mutex>
#include <chrono>
#include <thread>
#include <vector>

struct GLFWwindow;

namespace pex {

class HistoryStore;

class ImGuiApp {
public:
    // Non-owning constructor: ImGuiApp uses but does not own the data layer.
    // All pointers must be non-null and must outlive the ImGuiApp instance
    // (history may be nullptr to disable history-backed features).
    // - data_store: background data collection (managed externally)
    // - system_provider: for system config queries (ticks/sec, cpu count)
    // - details_provider: for on-demand detail fetches (threads, files, etc.)
    // - killer: for process termination
    // - history: recorded per-tick samples for chart backfill and CSV export
    // Precondition: pointers != nullptr (asserted in constructor)
    ImGuiApp(DataStore* data_store,
             ISystemDataProvider* system_provider,
             IProcessDataProvider* details_provider,
             IProcessKiller* killer,
             HistoryStore* history = nullptr);
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
    void expand_ancestors(int pid);
    [[nodiscard]] bool current_selection_matches() const;
    [[nodiscard]] std::vector<ProcessNode*> find_matching_processes() const;

    static std::string format_bytes(int64_t bytes);
    static std::string format_time(std::chrono::system_clock::time_point tp);

    // Kill functionality
    void request_kill_process(int pid, const std::string& name, bool is_tree);
    void execute_kill(bool force);
    static void collect_tree_pids(const ProcessNode* node, std::vector<int>& pids);

    // Find open file/handle (issue #7)
    void render_find_handle_dialog();
    void start_handle_search();
    void stop_handle_search();

    // History export (File menu)
    void export_history();

    // Non-owned references to data layer (managed externally)
    DataStore* data_store_ = nullptr;
    ISystemDataProvider* system_provider_ = nullptr;  // For config queries (ticks/sec, cpu count)
    IProcessDataProvider* details_provider_ = nullptr;
    IProcessKiller* killer_ = nullptr;
    HistoryStore* history_ = nullptr;  // Optional (nullptr = history features disabled)

    // Transient status message shown in the status bar (e.g. export result)
    std::string status_message_;
    std::chrono::steady_clock::time_point status_message_time_{};
    static constexpr auto kStatusMessageDuration = std::chrono::seconds(10);

    // Current snapshot from data store
    std::shared_ptr<DataSnapshot> current_data_;

    // ViewModel (holds all UI state - single source of truth)
    AppViewModel view_model_;

    // Details refresh tracking (avoid heavy syscalls every tick)
    int details_last_pid_ = -1;
    DetailsTab details_last_tab_ = DetailsTab::FileHandles;
    std::chrono::steady_clock::time_point details_last_refresh_{};
    bool details_force_refresh_ = false;
    static constexpr auto kDetailsRefreshInterval = std::chrono::milliseconds(500);

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

    // ---- Find open file/handle (issue #7) ----
    struct HandleSearchResult {
        int pid = 0;
        std::string process_name;
        std::string type;   // "file", "socket", "library", ...
        std::string path;
    };
    bool find_dialog_visible_ = false;
    bool find_focus_input_ = false;
    char find_query_[256] = {};
    int find_selected_row_ = -1;
    std::atomic<bool> find_running_{false};
    std::atomic<bool> find_cancel_{false};
    std::thread find_thread_;                       // Worker; joined before restart/destruction
    mutable std::mutex find_mutex_;                 // Guards results + status
    std::vector<HandleSearchResult> find_results_;
    std::string find_status_;
    // Dedicated provider instance: the worker thread must not share
    // details_provider_ with the UI thread.
    std::unique_ptr<IProcessDataProvider> find_provider_;
    static constexpr size_t kMaxFindResults = 5000;
};

} // namespace pex
