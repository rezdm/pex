#pragma once

#include "../interfaces/i_process_data_provider.hpp"
#include "../interfaces/i_system_data_provider.hpp"
#include "../interfaces/i_process_killer.hpp"
#include "../data_store.hpp"
#include "../viewmodels/app_view_model.hpp"
#include <memory>
#include <atomic>
#include <string>
#include <vector>
#include <ncurses.h>

namespace pex {

// Focus states for keyboard navigation between panels
enum class PanelFocus {
    ProcessList,
    DetailsPanel
};

class TuiApp {
public:
    // Non-owning constructor: TuiApp uses but does not own the data layer.
    // All pointers must be non-null and must outlive the TuiApp instance.
    TuiApp(DataStore* data_store,
           ISystemDataProvider* system_provider,
           IProcessDataProvider* details_provider,
           IProcessKiller* killer);
    ~TuiApp();

    void run();

private:
    // Rendering
    void render();
    void render_system_panel();
    void render_process_list();
    void render_process_tree();
    void render_process_tree_node(ProcessNode& node, int depth, int& row,
                                   const std::vector<bool>& connector_state);
    void render_details_panel();
    void render_status_bar();
    void render_kill_dialog();
    void render_help_overlay();
    void render_search_bar();

    // Tab rendering
    void render_file_handles_tab();
    void render_network_tab();
    void render_threads_tab();
    void render_memory_tab();
    void render_environment_tab();
    void render_libraries_tab();

    // Input handling
    void handle_input(int ch);
    void handle_process_list_input(int ch);
    void handle_details_panel_input(int ch);
    void handle_search_input(int ch);
    void handle_kill_dialog_input(int ch);
    void handle_help_input(int ch);

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

    // Utility
    static std::string format_bytes(int64_t bytes);
    static std::string format_uptime(int64_t seconds);
    void draw_progress_bar(WINDOW* win, int y, int x, int width,
                          double percent, int color_pair, const std::string& label = "");
    void draw_cpu_bar(WINDOW* win, int y, int x, int width,
                      double user_pct, double system_pct, const std::string& label);
    void draw_box_title(WINDOW* win, const std::string& title);

    // Non-owned references to data layer
    DataStore* data_store_ = nullptr;
    ISystemDataProvider* system_provider_ = nullptr;
    IProcessDataProvider* details_provider_ = nullptr;
    IProcessKiller* killer_ = nullptr;

    // Current snapshot from data store
    std::shared_ptr<DataSnapshot> current_data_;

    // ViewModel (holds all UI state)
    AppViewModel view_model_;

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
    std::string search_input_;
    std::atomic<bool> running_{false};

    // Scroll positions
    int process_scroll_offset_ = 0;
    int details_scroll_offset_ = 0;
    int visible_process_rows_ = 0;
    int visible_details_rows_ = 0;

    // Layout constants
    static constexpr int kSystemPanelHeight = 4;
    static constexpr int kStatusBarHeight = 1;
    static constexpr int kMinDetailsHeight = 8;
    static constexpr double kProcessPanelRatio = 0.5;  // Percentage of remaining space
};

} // namespace pex
