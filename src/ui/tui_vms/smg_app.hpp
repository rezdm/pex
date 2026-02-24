#pragma once

// OpenVMS TUI application using direct ANSI escape sequences via SYS$QIOW.
//
// SMG$ display output doesn't work on VMS x86_64 SSH pseudo-terminals
// (text content invisible despite success return codes), so we bypass SMG$
// for rendering and write ANSI escape sequences directly to TT: via SYS$QIOW.
// SMG$ is still used for keyboard input (smg$read_keystroke works fine).
//
// C++17 only (VSI C++ on OpenVMS x86-64 is Clang 10.0.1, no C++20).

#ifdef __VMS
#define __NEW_STARLET 1
#include <descrip.h>
#include <smgdef.h>
#include <smg$routines.h>
#endif

#include "../../platform/interfaces/i_process_data_provider.hpp"
#include "../../platform/interfaces/i_system_data_provider.hpp"
#include "../../platform/interfaces/i_process_killer.hpp"
#include "../../core/services/data_store.hpp"
#include "../common/viewmodels/app_view_model.hpp"
#include <memory>
#include <atomic>
#include <string>
#include <vector>
#include <set>

namespace pex {

// Focus states for keyboard navigation between panels
enum class SmgPanelFocus {
    ProcessList,
    DetailsPanel
};

// A rectangular region on the screen. Coordinates refer to the INNER area
// (content area), excluding any border. Borders, if present, are drawn
// one row/col outside the inner area.
struct DisplayRegion {
    int row = 0;          // 1-based screen row of inner area top-left
    int col = 0;          // 1-based screen col of inner area top-left
    int inner_rows = 0;   // number of usable content rows
    int inner_cols = 0;   // number of usable content columns
    bool has_border = false;
    bool active = false;
};

// Display region indices (used as display_id in rendering functions)
constexpr unsigned int DISP_NONE    = 0;
constexpr unsigned int DISP_SYSTEM  = 1;
constexpr unsigned int DISP_PROCESS = 2;
constexpr unsigned int DISP_DETAILS = 3;
constexpr unsigned int DISP_STATUS  = 4;
constexpr unsigned int DISP_DIALOG  = 5;
constexpr unsigned int DISP_COUNT   = 6;

class SmgApp {
public:
    SmgApp(DataStore* data_store,
           ISystemDataProvider* system_provider,
           IProcessDataProvider* details_provider,
           IProcessKiller* killer);
    ~SmgApp();

    void run();

private:
    // Rendering
    void render();
    void render_system_panel();
    void render_process_tree();
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
    void handle_input(unsigned int key_code);
    void handle_process_list_input(unsigned int key_code);
    void handle_details_panel_input(unsigned int key_code);
    void handle_search_input(unsigned int key_code);
    void handle_kill_dialog_input(unsigned int key_code);
    void handle_help_input(unsigned int key_code);

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

    // Display management (region-based layout)
    void create_displays();
    void resize_displays();
    void cleanup_displays();
    [[nodiscard]] int calc_system_panel_height() const;

    // ANSI rendering helpers
    void smg_put_chars(unsigned int display_id, int row, int col,
                       const std::string& text, unsigned int rendition = 0);
    void smg_erase_display(unsigned int display_id);
    void smg_draw_border(unsigned int display_id);
    void smg_draw_box_title(unsigned int display_id, const std::string& title);
    void draw_border(const DisplayRegion& region);

    // Frame buffer operations
    void buf_move_to(int row, int col);
    void buf_set_rendition(unsigned int rendition);
    void buf_reset_rendition();
    void buf_write(const std::string& text);
    void buf_clear_screen();
    void buf_hide_cursor();
    void buf_show_cursor();
    void flush_frame_buffer();

    // Cell-buffer differential rendering
    void init_cell_buffers();
    void cell_put(int screen_row, int screen_col, char ch, unsigned int rendition);
    void flush_cell_diff();

    // Utility
    static std::string format_bytes(int64_t bytes);
    static std::string format_uptime(int64_t seconds);
    void draw_progress_bar(unsigned int display_id, int row, int col, int width,
                           double percent, unsigned int rendition, const std::string& label = "");
    void draw_cpu_bar(unsigned int display_id, int row, int col, int width,
                      double user_pct, double system_pct, const std::string& label);

    // Non-owned references to data layer
    DataStore* data_store_ = nullptr;
    ISystemDataProvider* system_provider_ = nullptr;
    IProcessDataProvider* details_provider_ = nullptr;
    IProcessKiller* killer_ = nullptr;

    // Current snapshot from data store
    std::shared_ptr<DataSnapshot> current_data_;

    // ViewModel (holds all UI state)
    AppViewModel view_model_;

    // Display regions (indexed by DISP_* constants)
    DisplayRegion regions_[DISP_COUNT];

    // Display IDs for compatibility with render functions
    // These hold DISP_* index values (0 = inactive)
    unsigned int system_display_ = 0;
    unsigned int process_display_ = 0;
    unsigned int details_display_ = 0;
    unsigned int status_display_ = 0;
    unsigned int dialog_display_ = 0;

    // SMG$ keyboard (used for input only)
    unsigned int keyboard_id_ = 0;

    // Terminal I/O channel (SYS$ASSIGN to TT:)
    unsigned short tt_chan_ = 0;

    // ANSI frame buffer — used by flush_cell_diff to build output
    std::string frame_buf_;

    // Cell-based double buffer for differential rendering.
    // Only cells that changed between frames are output, reducing
    // ANSI output from ~40KB to ~500 bytes per typical refresh.
    struct ScreenCell {
        char ch = ' ';
        unsigned int rendition = 0;
    };
    std::vector<ScreenCell> front_buf_;  // what's currently on screen
    std::vector<ScreenCell> back_buf_;   // being built this frame
    bool force_full_redraw_ = true;

    // Terminal dimensions
    int term_rows_ = 24;
    int term_cols_ = 80;

    // UI state
    SmgPanelFocus current_focus_ = SmgPanelFocus::ProcessList;
    bool show_help_ = false;
    bool search_mode_ = false;
    bool system_panel_expanded_ = false;
    bool terminal_too_small_ = false;
    std::string search_input_;
    std::atomic<bool> running_{false};
    int dialog_debounce_ = 0;

    // Scroll positions
    int process_scroll_offset_ = 0;
    int process_h_scroll_ = 0;
    int details_scroll_offset_ = 0;
    int visible_process_rows_ = 0;
    int visible_details_rows_ = 0;

    // Details panel refresh tracking
    int details_last_pid_ = -1;
    DetailsTab details_last_tab_ = DetailsTab::FileHandles;
    bool details_needs_refresh_ = true;

    // Layout constants
    static constexpr int kSystemPanelCollapsedHeight = 3;
    static constexpr int kStatusBarHeight = 1;
    static constexpr int kMinDetailsHeight = 8;
    static constexpr double kProcessPanelRatio = 0.5;
};

} // namespace pex
