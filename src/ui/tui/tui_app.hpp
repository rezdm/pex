#pragma once

#include "../../platform/interfaces/i_process_data_provider.hpp"
#include "../../platform/interfaces/i_system_data_provider.hpp"
#include "../../platform/interfaces/i_process_killer.hpp"
#include "../../core/services/data_store.hpp"
#include "../../core/services/name_resolver.hpp"
#include "../common/viewmodels/app_view_model.hpp"
#include <chrono>
#include <memory>
#include <mutex>
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <ncurses.h>

namespace pex {

class HistoryStore;

// Focus states for keyboard navigation between panels
enum class PanelFocus {
    ProcessList,
    DetailsPanel
};

class TuiApp {
public:
    // Non-owning constructor: TuiApp uses but does not own the data layer.
    // All pointers must be non-null and must outlive the TuiApp instance
    // (history may be nullptr to disable history-backed features).
    TuiApp(DataStore* data_store,
           ISystemDataProvider* system_provider,
           IProcessDataProvider* details_provider,
           IProcessKiller* killer,
           HistoryStore* history = nullptr);
    ~TuiApp();

    void run();

private:
    // Rendering
    void render();
    void render_system_panel() const;
    void render_process_tree();
    void render_details_panel();
    void render_status_bar() const;
    void render_kill_dialog() const;

    static void render_help_overlay();
    void render_search_bar() const;

    // Find open file/handle (issue #7) - defined in tui_find_file.cpp
    void render_find_file_bar() const;
    void render_find_results_overlay();
    void handle_find_file_input(int ch);
    void handle_find_results_input(int ch);
    void start_find_scan();
    void stop_find_scan();

    // History export (issue #9)
    void export_history();

    // Tab rendering
    void render_file_handles_tab() const;
    void render_network_tab() const;
    void render_threads_tab() const;
    void render_memory_tab() const;
    void render_environment_tab() const;
    void render_libraries_tab() const;

    // Input handling
    void handle_input(int ch);
    void handle_process_list_input(int ch);
    void handle_details_panel_input(int ch);
    void handle_search_input(int ch);
    void handle_kill_dialog_input(int ch);
    void handle_help_input(int ch);
    void handle_mouse_event();

    // Navigation helpers
    [[nodiscard]] std::vector<ProcessNode*> get_visible_items() const;
    static void collect_visible_items(ProcessNode* node, std::vector<ProcessNode*>& items,
                                      const std::set<int>& collapsed);
    void move_selection(int delta);
    void page_up();
    void page_down();
    void scroll_to_selection();

    // Search
    void search_select_first();
    void search_next();
    void search_previous();
    void expand_ancestors(int pid);  // Defined in tui_app_navigation.cpp
    [[nodiscard]] bool matches_search(const ProcessInfo& info) const;
    [[nodiscard]] std::vector<ProcessNode*> find_matching_processes() const;

    // Details
    void refresh_selected_details();
    void next_tab();
    void prev_tab();

    // Kill functionality
    void request_kill_process(int pid, const std::string& name, bool is_tree);
    void execute_kill(bool force);
    static void collect_tree_pids(const ProcessNode* node, std::vector<int>& pids);

    // Window management
    void create_windows();
    void resize_windows();
    void cleanup_windows();
    [[nodiscard]] int calc_system_panel_height() const;

    // Utility
    static std::string format_bytes(int64_t bytes);
    static std::string format_uptime(int64_t seconds);
    static void draw_progress_bar(WINDOW* win, int y, int x, int width,
                          double percent, int color_pair, const std::string& label = "");
    static void draw_cpu_bar(WINDOW* win, int y, int x, int width,
                      double user_pct, double system_pct, const std::string& label);
    static void draw_box_title(WINDOW* win, const std::string& title);

    // Non-owned references to data layer
    DataStore* data_store_ = nullptr;
    ISystemDataProvider* system_provider_ = nullptr;
    IProcessDataProvider* details_provider_ = nullptr;
    IProcessKiller* killer_ = nullptr;
    HistoryStore* history_ = nullptr;  // Optional (nullptr = history features disabled)

    // Current snapshot from data store
    std::shared_ptr<DataSnapshot> current_data_;

    // ViewModel (holds all UI state)
    AppViewModel view_model_;

    // Name resolver for DNS lookups in network tab
    mutable NameResolver name_resolver_;

    // ncurses windows
    WINDOW* main_win_ = nullptr;
    WINDOW* system_win_ = nullptr;
    WINDOW* process_win_ = nullptr;
    WINDOW* details_win_ = nullptr;
    WINDOW* status_win_ = nullptr;

    // UI state
    PanelFocus current_focus_ = PanelFocus::ProcessList;
    bool show_help_ = false;
    bool search_mode_ = false;
    bool system_panel_expanded_ = false;  // When false, show compact CPU summary
    bool terminal_too_small_ = false;
    std::string search_input_;
    std::atomic<bool> running_{false};
    int dialog_debounce_ = 0;  // Frames to ignore input after showing dialog

    // Scroll positions
    int process_scroll_offset_ = 0;
    int process_h_scroll_ = 0;  // Horizontal scroll for process list
    int details_scroll_offset_ = 0;
    int visible_process_rows_ = 0;
    int visible_details_rows_ = 0;

    // Details panel refresh tracking (avoid expensive syscalls every frame)
    int details_last_pid_ = -1;
    DetailsTab details_last_tab_ = DetailsTab::FileHandles;
    bool details_needs_refresh_ = true;

    // Window positions (for mouse click detection)
    int process_win_y_ = 0;
    int process_win_height_ = 0;
    int details_win_y_ = 0;
    int details_win_height_ = 0;

    // Transient status message (e.g. history export result)
    std::string status_message_;
    std::chrono::steady_clock::time_point status_message_time_{};
    static constexpr auto kStatusMessageDuration = std::chrono::seconds(8);

    // ---- Find open file/handle (issue #7) ----
    struct FindFileResult {
        int pid = 0;
        std::string process_name;
        std::string type;   // "file", "socket", "library", ...
        std::string path;
    };
    bool find_file_mode_ = false;       // Entering the query
    bool find_results_visible_ = false; // Results overlay shown
    std::string find_file_input_;
    int find_selected_idx_ = 0;
    int find_scroll_ = 0;
    std::atomic<bool> find_running_{false};
    std::atomic<bool> find_cancel_{false};
    std::atomic<bool> find_dirty_{false};  // Worker progressed; main loop re-renders
    std::thread find_thread_;              // Joined before restart/destruction
    mutable std::mutex find_mutex_;        // Guards results + status
    std::vector<FindFileResult> find_results_;
    std::string find_status_;
    // Dedicated provider: worker must not share details_provider_ with UI thread
    std::unique_ptr<IProcessDataProvider> find_provider_;
    static constexpr size_t kMaxFindResults = 5000;

    // Layout constants
    static constexpr int kSystemPanelCollapsedHeight = 3;  // Compact: avg CPU, mem, tasks
    static constexpr int kStatusBarHeight = 1;
    static constexpr int kMinDetailsHeight = 8;
    static constexpr double kProcessPanelRatio = 0.5;  // Percentage of remaining space
};

} // namespace pex
