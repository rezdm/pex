#include "smg_app.hpp"
#include "smg_colors.hpp"
#include <cassert>
#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdio>

#ifdef __VMS
#define __NEW_STARLET 1
#include <descrip.h>
#include <smgdef.h>
#include <smg$routines.h>
#include <ssdef.h>
#include <lib$routines.h>

// System services for direct terminal I/O
// Use <starlet.h> for correct prototypes and symbol names on x86_64.
// Manual extern "C" declarations don't work because the Clang-based compiler
// lowercases symbols, yet SYS$SSISHR alone doesn't resolve them on x86_64 —
// we also need SYS$STUBS.OLB (added in DESCRIP.MMS link options).
#include <starlet.h>
#include <iodef.h>
#include <iosbdef.h>
#endif

namespace pex {

SmgApp::SmgApp(DataStore* data_store,
               ISystemDataProvider* system_provider,
               IProcessDataProvider* details_provider,
               IProcessKiller* killer)
    : data_store_(data_store)
    , system_provider_(system_provider)
    , details_provider_(details_provider)
    , killer_(killer)
{
    assert(data_store_ != nullptr);
    assert(system_provider_ != nullptr);
    assert(details_provider_ != nullptr);
    assert(killer_ != nullptr);
}

SmgApp::~SmgApp() {
    cleanup_displays();
}

// ---- Frame buffer operations ----

void SmgApp::buf_move_to(int row, int col) {
    char seq[32];
    std::snprintf(seq, sizeof(seq), "\033[%d;%dH", row, col);
    frame_buf_ += seq;
}

void SmgApp::buf_set_rendition(unsigned int rend) {
    if (rend == 0) {
        frame_buf_ += "\033[0m";
        return;
    }

    frame_buf_ += "\033[0";  // Always reset first

    if (rend & ANSI_BOLD)      frame_buf_ += ";1";
    if (rend & ANSI_REVERSE)   frame_buf_ += ";7";
    if (rend & ANSI_UNDERLINE) frame_buf_ += ";4";
    if (rend & ANSI_BLINK)     frame_buf_ += ";5";

    unsigned int fg = (rend & ANSI_FG_MASK) >> ANSI_FG_SHIFT;
    if (fg > 0 && fg <= 7) {
        char code[8];
        std::snprintf(code, sizeof(code), ";%d", 30 + fg);
        frame_buf_ += code;
    }

    unsigned int bg = (rend & ANSI_BG_MASK) >> ANSI_BG_SHIFT;
    if (bg > 0 && bg <= 7) {
        char code[8];
        std::snprintf(code, sizeof(code), ";%d", 40 + bg);
        frame_buf_ += code;
    }

    frame_buf_ += 'm';
}

void SmgApp::buf_reset_rendition() {
    frame_buf_ += "\033[0m";
}

void SmgApp::buf_write(const std::string& text) {
    frame_buf_ += text;
}

void SmgApp::buf_clear_screen() {
    frame_buf_ += "\033[2J\033[H";
}

void SmgApp::buf_hide_cursor() {
    frame_buf_ += "\033[?25l";
}

void SmgApp::buf_show_cursor() {
    frame_buf_ += "\033[?25h";
}

void SmgApp::flush_frame_buffer() {
#ifdef __VMS
    if (frame_buf_.empty()) return;

    // Write ANSI frame buffer to stdout using C library I/O.
    // SYS$QIOW with IO$_WRITEVBLK returns SS$_NOPRIV on SSH pseudo-terminals,
    // and IO$_WRITELBLK returns SS$_BADPARAM.  The C RTL's write() handles
    // the terminal driver correctly without requiring special privilege.
    std::fwrite(frame_buf_.c_str(), 1, frame_buf_.size(), stdout);
    std::fflush(stdout);
    frame_buf_.clear();
#endif
}

// ---- ANSI rendering helpers (called by all render functions) ----

void SmgApp::smg_put_chars(unsigned int display_id, int row, int col,
                           const std::string& text, unsigned int rendition) {
    if (display_id == 0 || display_id >= DISP_COUNT) return;
    const auto& rgn = regions_[display_id];
    if (!rgn.active) return;

    // Clamp to region bounds
    if (row < 1 || row > rgn.inner_rows) return;
    if (col < 1 || col > rgn.inner_cols) return;

    // Map display-relative coords to screen coords
    int screen_row = rgn.row + row - 1;
    int screen_col = rgn.col + col - 1;

    // Truncate text to fit within region
    int max_chars = rgn.inner_cols - col + 1;
    if (max_chars <= 0) return;

    int len = std::min(static_cast<int>(text.size()), max_chars);
    for (int i = 0; i < len; i++) {
        cell_put(screen_row, screen_col + i, text[i], rendition);
    }
}

void SmgApp::smg_erase_display(unsigned int display_id) {
    if (display_id == 0 || display_id >= DISP_COUNT) return;
    const auto& rgn = regions_[display_id];
    if (!rgn.active) return;

    for (int r = 0; r < rgn.inner_rows; r++) {
        for (int c = 0; c < rgn.inner_cols; c++) {
            cell_put(rgn.row + r, rgn.col + c, ' ', 0);
        }
    }
}

void SmgApp::draw_border(const DisplayRegion& rgn) {
    if (!rgn.has_border || !rgn.active) return;

    int top = rgn.row - 1;
    int left = rgn.col - 1;
    int bottom = rgn.row + rgn.inner_rows;
    int right = rgn.col + rgn.inner_cols;

    unsigned int rend = SMG_REND_BLUE;

    // Top border: +---...---+
    cell_put(top, left, '+', rend);
    for (int c = left + 1; c < right; c++) cell_put(top, c, '-', rend);
    cell_put(top, right, '+', rend);

    // Bottom border
    cell_put(bottom, left, '+', rend);
    for (int c = left + 1; c < right; c++) cell_put(bottom, c, '-', rend);
    cell_put(bottom, right, '+', rend);

    // Side borders
    for (int r = rgn.row; r < rgn.row + rgn.inner_rows; r++) {
        cell_put(r, left, '|', rend);
        cell_put(r, right, '|', rend);
    }
}

void SmgApp::smg_draw_border(unsigned int display_id) {
    if (display_id == 0 || display_id >= DISP_COUNT) return;
    draw_border(regions_[display_id]);
}

void SmgApp::smg_draw_box_title(unsigned int display_id, const std::string& title) {
    if (display_id == 0 || display_id >= DISP_COUNT) return;
    const auto& rgn = regions_[display_id];
    if (!rgn.active || !rgn.has_border) return;

    if (!title.empty()) {
        std::string padded = " " + title + " ";
        int top = rgn.row - 1;
        int title_col = rgn.col + 1;  // 2 chars into the top border
        for (size_t i = 0; i < padded.size(); i++) {
            int c = title_col + static_cast<int>(i);
            if (c > term_cols_) break;
            cell_put(top, c, padded[i], SMG_REND_TITLE);
        }
    }
}

// ---- Cell-buffer differential rendering ----

void SmgApp::init_cell_buffers() {
    size_t total = static_cast<size_t>(term_rows_) * term_cols_;
    front_buf_.assign(total, ScreenCell{' ', 0});
    back_buf_.assign(total, ScreenCell{' ', 0});
    force_full_redraw_ = true;
}

void SmgApp::cell_put(int row, int col, char ch, unsigned int rendition) {
    if (row < 1 || row > term_rows_ || col < 1 || col > term_cols_) return;
    size_t idx = static_cast<size_t>(row - 1) * term_cols_ + (col - 1);
    back_buf_[idx].ch = ch;
    back_buf_[idx].rendition = rendition;
}

void SmgApp::flush_cell_diff() {
#ifdef __VMS
    frame_buf_.clear();

    unsigned int cur_rend = UINT_MAX;  // force first SGR emission
    int cur_row = -1, cur_col = -1;    // cursor position tracker (1-based)

    const size_t total = static_cast<size_t>(term_rows_) * term_cols_;
    for (size_t idx = 0; idx < total; idx++) {
        const auto& back = back_buf_[idx];
        const auto& front = front_buf_[idx];

        // Skip unchanged cells (unless forcing full redraw)
        if (!force_full_redraw_ &&
            back.ch == front.ch && back.rendition == front.rendition) {
            continue;
        }

        int r = static_cast<int>(idx / term_cols_);
        int c = static_cast<int>(idx % term_cols_);
        int sr = r + 1;  // 1-based screen row
        int sc = c + 1;  // 1-based screen col

        // Emit cursor positioning only if cursor isn't already there
        if (sr != cur_row || sc != cur_col) {
            char seq[32];
            std::snprintf(seq, sizeof(seq), "\033[%d;%dH", sr, sc);
            frame_buf_ += seq;
        }

        // Emit SGR only when rendition changes
        if (back.rendition != cur_rend) {
            buf_set_rendition(back.rendition);
            cur_rend = back.rendition;
        }

        frame_buf_ += back.ch;
        cur_row = sr;
        cur_col = sc + 1;  // cursor auto-advances after character
    }

    // Reset rendition at end of frame
    if (cur_rend != 0 && cur_rend != UINT_MAX) {
        frame_buf_ += "\033[0m";
    }

    if (!frame_buf_.empty()) {
        std::fwrite(frame_buf_.c_str(), 1, frame_buf_.size(), stdout);
        std::fflush(stdout);
    }

    // Swap: front now reflects what's on screen
    front_buf_ = back_buf_;
    force_full_redraw_ = false;
#endif
}

// ---- Main application loop ----

void SmgApp::run() {
#ifdef __VMS
    unsigned int status;

    // File-based diagnostic log (helps debug issues on VMS)
    FILE* diag = std::fopen("SYS$SYSROOT:[REZDM.PEX]PEXC_DIAG.LOG", "w");
    auto diag_log = [&diag](const char* msg) {
        if (diag) { std::fprintf(diag, "%s\n", msg); std::fflush(diag); }
    };
    auto diag_logf = [&diag](const char* fmt, auto... args) {
        if (diag) { std::fprintf(diag, fmt, args...); std::fprintf(diag, "\n"); std::fflush(diag); }
    };

    diag_log("=== PEXC diagnostic start ===");

    // Configure terminal for ANSI escape sequence support.
    {
        char set_term_cmd[] = "SET TERMINAL/ANSI_CRT/DEC_CRT";
        struct dsc$descriptor_s cmd_dsc = {
            static_cast<unsigned short>(sizeof(set_term_cmd) - 1),
            DSC$K_DTYPE_T, DSC$K_CLASS_S, set_term_cmd
        };
        lib$spawn(&cmd_dsc, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }

    // Assign a channel to TT: for direct ANSI output via sys$qiow
    {
        char tt_name[] = "TT:";
        struct dsc$descriptor_s tt_dsc = {3, DSC$K_DTYPE_T, DSC$K_CLASS_S, tt_name};
        status = sys$assign(&tt_dsc, &tt_chan_, 0, 0, 0);
        diag_logf("sys$assign TT: status=%%X%08X chan=%u", status, (unsigned)tt_chan_);
        if (!(status & 1)) {
            if (diag) std::fclose(diag);
            return;
        }
    }

    // Create SMG$ virtual keyboard for input (SMG$ keyboard works fine on SSH)
    {
        char kb_dev[] = "TT:";
        struct dsc$descriptor_s kb_dsc = {
            static_cast<unsigned short>(sizeof(kb_dev) - 1),
            DSC$K_DTYPE_T, DSC$K_CLASS_S, kb_dev
        };
        status = smg$create_virtual_keyboard(&keyboard_id_, &kb_dsc);
        diag_logf("smg$create_virtual_keyboard: status=%%X%08X id=%u", status, keyboard_id_);
        if (!(status & 1)) {
            sys$dassgn(tt_chan_);
            tt_chan_ = 0;
            if (diag) std::fclose(diag);
            return;
        }
    }

    // Query terminal size via IO$_SENSEMODE
    {
        unsigned char tt_chars[12] = {0};
        _iosb iosb = {};
        status = sys$qiow(0, tt_chan_, IO$_SENSEMODE, &iosb, 0, 0,
                           tt_chars, 12, 0, 0, 0, 0);
        if ((status & 1) && (iosb.iosb$w_status & 1)) {
            unsigned short width  = static_cast<unsigned short>(tt_chars[2] | (tt_chars[3] << 8));
            unsigned short length = static_cast<unsigned short>(tt_chars[8] | (tt_chars[9] << 8));
            if (width > 0)  term_cols_ = width;
            if (length > 0) term_rows_ = length;
        }
        diag_logf("IO$_SENSEMODE: %dx%d (raw)", term_cols_, term_rows_);
    }

    // VMS SSH pseudo-terminals report "page length" as the scrollback buffer
    // size (e.g., 4096) rather than the actual visible window height.
    // Cap to a reasonable maximum.  The SSH client window is typically 24-80 rows.
    if (term_rows_ > 100) {
        diag_logf("Capping page length from %d to 50", term_rows_);
        term_rows_ = 50;
    }
    diag_logf("Terminal size: %dx%d", term_cols_, term_rows_);

    // Calculate panel layout and assign display regions
    create_displays();
    diag_logf("create_displays: terminal_too_small_=%d", (int)terminal_too_small_);
    for (unsigned int i = 1; i < DISP_COUNT; i++) {
        diag_logf("  region[%u]: active=%d row=%d col=%d rows=%d cols=%d border=%d",
                  i, (int)regions_[i].active, regions_[i].row, regions_[i].col,
                  regions_[i].inner_rows, regions_[i].inner_cols,
                  (int)regions_[i].has_border);
    }

    // Start background data collection
    diag_log("Starting data_store...");
    data_store_->start();

    // Wait for initial data
    {
        int retries = 50;
        current_data_ = data_store_->get_snapshot();
        while (retries-- > 0 && (!current_data_ || current_data_->process_count == 0)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            current_data_ = data_store_->get_snapshot();
        }
        diag_logf("Data wait done: data=%s process_count=%d",
                  current_data_ ? "yes" : "null",
                  current_data_ ? (int)current_data_->process_count : -1);

        if (!current_data_ || current_data_->process_count == 0) {
            diag_log("FATAL: No process data after 5 seconds");
            // Show error on screen before exiting
            {
                const char msg[] = "\033[2J\033[HError: No process data available. Exiting.\r\n";
                std::fwrite(msg, 1, sizeof(msg) - 1, stdout);
                std::fflush(stdout);
            }
            smg$delete_virtual_keyboard(&keyboard_id_);
            sys$dassgn(tt_chan_);
            tt_chan_ = 0;
            if (diag) std::fclose(diag);
            return;
        }
    }

    view_model_.update_from_snapshot(current_data_);
    diag_logf("ViewModel updated: %zu tree roots, selected_pid=%d",
              current_data_->process_tree.size(),
              view_model_.process_list.selected_pid);

    running_ = true;

    // Initialize cell buffers for differential rendering
    frame_buf_.reserve(32768);
    init_cell_buffers();

    // Hide cursor and clear screen
    {
        const char seq[] = "\033[?25l\033[2J\033[H";
        std::fwrite(seq, 1, sizeof(seq) - 1, stdout);
        std::fflush(stdout);
    }

    // Render initial frame to cell buffer
    std::fill(back_buf_.begin(), back_buf_.end(), ScreenCell{' ', 0});
    for (unsigned int i = 1; i < DISP_COUNT; i++) {
        if (regions_[i].active && regions_[i].has_border) draw_border(regions_[i]);
    }
    render();
    flush_cell_diff();

    // Close diagnostic log before main loop (file stays available for inspection)
    if (diag) { std::fclose(diag); diag = nullptr; }

    // Main loop
    while (running_) {
        // Read keystroke with timeout (100 centiseconds = 1 second).
        // SMG$READ_KEYSTROKE expects unsigned short* for key_code with __NEW_STARLET.
        unsigned short key_code = 0;
        int timeout_cs = 100;
        status = smg$read_keystroke(&keyboard_id_, &key_code, 0, &timeout_cs, 0);

        if (status & 1) {
            // Key received — widen to unsigned int for handle_input
            handle_input(static_cast<unsigned int>(key_code));
        }
        // On timeout (SS$_TIMEOUT) or key, refresh display

        // Update data
        current_data_ = data_store_->get_snapshot();
        if (current_data_) {
            view_model_.update_from_snapshot(current_data_);
        }

        // Render frame to cell buffer (only changed cells get output)
        std::fill(back_buf_.begin(), back_buf_.end(), ScreenCell{' ', 0});
        for (unsigned int i = 1; i < DISP_COUNT; i++) {
            if (regions_[i].active && regions_[i].has_border) {
                draw_border(regions_[i]);
            }
        }

        render();
        flush_cell_diff();
    }

    // Cleanup: restore terminal
    {
        const char seq[] = "\033[?25h\033[0m\033[2J\033[1;1H";
        std::fwrite(seq, 1, sizeof(seq) - 1, stdout);
        std::fflush(stdout);
    }

    data_store_->stop();
    smg$delete_virtual_keyboard(&keyboard_id_);
    sys$dassgn(tt_chan_);
    tt_chan_ = 0;

#else
    // Non-VMS stub
    (void)this;
#endif
}

void SmgApp::render() {
    if (terminal_too_small_) return;

    // Clear all panel regions before rendering to prevent stale text artifacts.
    // With direct ANSI output (unlike SMG$ virtual displays), any text from the
    // previous frame that isn't overwritten will persist on screen.
    if (system_display_) smg_erase_display(system_display_);
    if (process_display_) smg_erase_display(process_display_);
    if (details_display_) smg_erase_display(details_display_);
    // Status bar and dialog are erased by their own render functions.

    // Render panels (each writes to frame_buf_ via smg_put_chars)
    if (view_model_.system_panel.is_visible && system_display_) {
        render_system_panel();
    }

    render_process_tree();
    render_details_panel();
    render_status_bar();

    // Render overlays
    if (view_model_.kill_dialog.is_visible) {
        render_kill_dialog();
    }

    if (show_help_) {
        render_help_overlay();
    }

    if (search_mode_) {
        render_search_bar();
    }
}

// --- Navigation helpers (unchanged from SMG$ version) ---

std::vector<ProcessNode*> SmgApp::get_visible_items() const {
    std::vector<ProcessNode*> items;
    if (!current_data_) return items;

    for (const auto& root : current_data_->process_tree) {
        collect_visible_items(root.get(), items, view_model_.process_list.collapsed_pids);
    }
    return items;
}

void SmgApp::collect_visible_items(ProcessNode* node, std::vector<ProcessNode*>& items,
                                   const std::set<int>& collapsed) {
    if (!node) return;
    items.push_back(node);

    if (collapsed.count(node->info.pid) == 0) {
        for (const auto& child : node->children) {
            collect_visible_items(child.get(), items, collapsed);
        }
    }
}

void SmgApp::move_selection(int delta) {
    const auto visible = get_visible_items();
    if (visible.empty()) return;

    int current_pos = 0;
    for (size_t i = 0; i < visible.size(); ++i) {
        if (visible[i]->info.pid == view_model_.process_list.selected_pid) {
            current_pos = static_cast<int>(i);
            break;
        }
    }

    const int new_pos = std::clamp(current_pos + delta, 0, static_cast<int>(visible.size()) - 1);
    view_model_.process_list.selected_pid = visible[new_pos]->info.pid;
    scroll_to_selection();
}

void SmgApp::page_up() {
    move_selection(-visible_process_rows_);
}

void SmgApp::page_down() {
    move_selection(visible_process_rows_);
}

void SmgApp::scroll_to_selection() {
    const auto visible = get_visible_items();
    int selected_idx = 0;

    for (size_t i = 0; i < visible.size(); ++i) {
        if (visible[i]->info.pid == view_model_.process_list.selected_pid) {
            selected_idx = static_cast<int>(i);
            break;
        }
    }

    if (selected_idx < process_scroll_offset_) {
        process_scroll_offset_ = selected_idx;
    } else if (selected_idx >= process_scroll_offset_ + visible_process_rows_) {
        process_scroll_offset_ = selected_idx - visible_process_rows_ + 1;
    }
}

bool SmgApp::matches_search(const ProcessInfo& info) const {
    if (view_model_.process_list.search_text.empty()) return false;

    const auto& search = view_model_.process_list.search_text;

    auto to_lower = [](const std::string& s) {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return result;
    };

    const std::string search_lower = to_lower(search);

    if (to_lower(info.name).find(search_lower) != std::string::npos) return true;
    if (to_lower(info.command_line).find(search_lower) != std::string::npos) return true;
    if (std::to_string(info.pid) == search) return true;

    return false;
}

std::vector<ProcessNode*> SmgApp::find_matching_processes() const {
    std::vector<ProcessNode*> matches;
    const auto visible = get_visible_items();
    for (auto* node : visible) {
        if (matches_search(node->info)) {
            matches.push_back(node);
        }
    }
    return matches;
}

void SmgApp::search_select_first() {
    const auto matches = find_matching_processes();
    if (!matches.empty()) {
        view_model_.process_list.selected_pid = matches[0]->info.pid;
        scroll_to_selection();
    }
}

void SmgApp::search_next() {
    const auto matches = find_matching_processes();
    if (matches.empty()) return;

    for (size_t i = 0; i < matches.size(); ++i) {
        if (matches[i]->info.pid == view_model_.process_list.selected_pid) {
            const size_t next = (i + 1) % matches.size();
            view_model_.process_list.selected_pid = matches[next]->info.pid;
            scroll_to_selection();
            return;
        }
    }

    view_model_.process_list.selected_pid = matches[0]->info.pid;
    scroll_to_selection();
}

void SmgApp::search_previous() {
    const auto matches = find_matching_processes();
    if (matches.empty()) return;

    for (size_t i = 0; i < matches.size(); ++i) {
        if (matches[i]->info.pid == view_model_.process_list.selected_pid) {
            const size_t prev = (i == 0) ? matches.size() - 1 : i - 1;
            view_model_.process_list.selected_pid = matches[prev]->info.pid;
            scroll_to_selection();
            return;
        }
    }

    view_model_.process_list.selected_pid = matches[0]->info.pid;
    scroll_to_selection();
}

void SmgApp::next_tab() {
    int tab = static_cast<int>(view_model_.details_panel.active_tab);
    tab = (tab + 1) % kTabCount;
    view_model_.details_panel.active_tab = static_cast<DetailsTab>(tab);
    details_scroll_offset_ = 0;
}

void SmgApp::prev_tab() {
    int tab = static_cast<int>(view_model_.details_panel.active_tab);
    tab = (tab + kTabCount - 1) % kTabCount;
    view_model_.details_panel.active_tab = static_cast<DetailsTab>(tab);
    details_scroll_offset_ = 0;
}

void SmgApp::request_kill_process(int pid, const std::string& name, bool is_tree) {
    view_model_.kill_dialog.is_visible = true;
    view_model_.kill_dialog.target_pid = pid;
    view_model_.kill_dialog.target_name = name;
    view_model_.kill_dialog.is_tree_kill = is_tree;
    view_model_.kill_dialog.error_message.clear();
    view_model_.kill_dialog.show_force_option = false;
    dialog_debounce_ = 5;
}

void SmgApp::execute_kill(bool force) {
    auto& kd = view_model_.kill_dialog;

    KillResult result;
    if (kd.is_tree_kill) {
        result = killer_->kill_process_tree(kd.target_pid, force);
    } else {
        result = killer_->kill_process(kd.target_pid, force);
    }

    if (result.success) {
        kd.is_visible = false;
        kd.target_pid = -1;
    } else if (result.process_still_running && !force) {
        kd.show_force_option = true;
        kd.error_message = "Process still running after $FORCEX";
    } else {
        kd.error_message = result.error_message;
    }
}

void SmgApp::collect_tree_pids(const ProcessNode* node, std::vector<int>& pids) {
    if (!node) return;
    pids.push_back(node->info.pid);
    for (const auto& child : node->children) {
        collect_tree_pids(child.get(), pids);
    }
}

std::string SmgApp::format_bytes(int64_t bytes) {
    static const char* units[] = {"B", "K", "M", "G", "T", "P"};
    constexpr int max_unit = 5;

    int unit_idx = 0;
    auto size = static_cast<double>(bytes);

    while (size >= 1024.0 && unit_idx < max_unit) {
        size /= 1024.0;
        unit_idx++;
    }

    char buf[32];
    if (unit_idx == 0) {
        std::snprintf(buf, sizeof(buf), "%lld%s", static_cast<long long>(bytes), units[unit_idx]);
    } else if (size >= 100.0) {
        std::snprintf(buf, sizeof(buf), "%.0f%s", size, units[unit_idx]);
    } else if (size >= 10.0) {
        std::snprintf(buf, sizeof(buf), "%.1f%s", size, units[unit_idx]);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f%s", size, units[unit_idx]);
    }
    return std::string(buf);
}

std::string SmgApp::format_uptime(int64_t seconds) {
    const int days = seconds / 86400;
    const int hours = (seconds % 86400) / 3600;
    const int minutes = (seconds % 3600) / 60;
    const int secs = seconds % 60;

    std::ostringstream oss;
    if (days > 0) {
        oss << days << "d " << std::setfill('0') << std::setw(2) << hours
            << ":" << std::setw(2) << minutes << ":" << std::setw(2) << secs;
    } else {
        oss << std::setfill('0') << std::setw(2) << hours
            << ":" << std::setw(2) << minutes << ":" << std::setw(2) << secs;
    }
    return oss.str();
}

void SmgApp::draw_progress_bar(unsigned int display_id, int row, int col, int width,
                               double percent, unsigned int rendition, const std::string& label) {
    if (width < 3) return;

    const int bar_width = width - 2;
    const int filled = static_cast<int>(bar_width * std::clamp(percent, 0.0, 100.0) / 100.0);

    smg_put_chars(display_id, row, col, "[", SMG_REND_NORMAL);

    if (filled > 0) {
        std::string filled_str(filled, '#');
        smg_put_chars(display_id, row, col + 1, filled_str, rendition);
    }

    if (filled < bar_width) {
        std::string empty_str(bar_width - filled, ' ');
        smg_put_chars(display_id, row, col + 1 + filled, empty_str, SMG_REND_NORMAL);
    }

    smg_put_chars(display_id, row, col + bar_width + 1, "]", SMG_REND_NORMAL);

    if (!label.empty()) {
        smg_put_chars(display_id, row, col + width + 1, label, SMG_REND_NORMAL);
    }
}

void SmgApp::draw_cpu_bar(unsigned int display_id, int row, int col, int width,
                          double user_pct, double system_pct, const std::string& label) {
    if (width < 3) return;

    const int bar_width = width - 2;
    const int user_chars = static_cast<int>(bar_width * std::clamp(user_pct, 0.0, 100.0) / 100.0);
    int system_chars = static_cast<int>(bar_width * std::clamp(system_pct, 0.0, 100.0) / 100.0);

    if (user_chars + system_chars > bar_width) {
        system_chars = bar_width - user_chars;
    }

    smg_put_chars(display_id, row, col, "[", SMG_REND_NORMAL);

    if (user_chars > 0) {
        std::string user_str(user_chars, '#');
        smg_put_chars(display_id, row, col + 1, user_str, SMG_REND_CPU_USER);
    }

    if (system_chars > 0) {
        std::string sys_str(system_chars, '#');
        smg_put_chars(display_id, row, col + 1 + user_chars, sys_str, SMG_REND_CPU_SYSTEM);
    }

    const int empty = bar_width - user_chars - system_chars;
    if (empty > 0) {
        std::string empty_str(empty, ' ');
        smg_put_chars(display_id, row, col + 1 + user_chars + system_chars, empty_str, SMG_REND_NORMAL);
    }

    smg_put_chars(display_id, row, col + bar_width + 1, "]", SMG_REND_NORMAL);

    if (!label.empty()) {
        smg_put_chars(display_id, row, col + width + 1, label, SMG_REND_NORMAL);
    }
}

} // namespace pex
