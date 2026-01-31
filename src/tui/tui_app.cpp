#include "tui_app.hpp"
#include "tui_colors.hpp"
#include <cassert>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>

namespace pex {

// Signal handler for terminal resize - use sig_atomic_t for signal safety
static volatile sig_atomic_t g_resize_requested = 0;

static void handle_resize([[maybe_unused]] int sig) {
    g_resize_requested = 1;
}

TuiApp::TuiApp(DataStore* data_store,
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

TuiApp::~TuiApp() {
    cleanup_windows();
}

void TuiApp::run() {
    // Initialize ncurses
    if (!initscr()) {
        fprintf(stderr, "pexc: initscr() failed\n");
        return;
    }

    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);  // Hide cursor
    nodelay(stdscr, TRUE);  // Non-blocking input
    mouseinterval(200);  // Double-click timeout in milliseconds

    // Enable mouse support (button presses and scroll wheel, not movement tracking)
    mousemask(ALL_MOUSE_EVENTS, nullptr);
    printf("\033[?1000h");
    fflush(stdout);

    // Initialize colors
    init_colors();

    // Set terminal title (like GUI version: "PEX: uname-info")
    const std::string title = "PEX: " + system_provider_->get_system_info_string();
    printf("\033]0;%s\007", title.c_str());
    fflush(stdout);

    // Set up resize handler
    signal(SIGWINCH, handle_resize);

    // Clear and refresh stdscr first to initialize the screen properly
    clear();
    refresh();

    // Create windows
    create_windows();

    // Start data collection
    data_store_->start();

    // Get initial data - wait for actual data (not just empty snapshot)
    int retries = 50;  // 5 seconds max
    current_data_ = data_store_->get_snapshot();
    while (retries-- > 0 && (!current_data_ || current_data_->process_count == 0)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        current_data_ = data_store_->get_snapshot();
    }

    if (!current_data_ || current_data_->process_count == 0) {
        // Restore terminal state before exiting
        printf("\033[?1000l");  // Disable mouse tracking
        printf("\033]0;\007");   // Reset terminal title
        fflush(stdout);
        endwin();
        fprintf(stderr, "pexc: Failed to get process data from system\n");
        return;
    }

    view_model_.update_from_snapshot(current_data_);

    running_ = true;
    auto last_update = std::chrono::steady_clock::now();
    constexpr auto update_interval = std::chrono::milliseconds(100);
    int loop_count = 0;

    while (running_) {
        loop_count++;

        // Handle terminal resize
        if (g_resize_requested) {
            g_resize_requested = 0;
            endwin();
            refresh();
            resize_windows();
        }

        // Handle input (non-blocking)
        if (const int ch = getch(); ch != ERR) {
            handle_input(ch);
        }

        // Update data periodically
        auto now = std::chrono::steady_clock::now();
        if (now - last_update >= update_interval) {
            const auto new_data = data_store_->get_snapshot();
            if (new_data && (!current_data_ || new_data->timestamp != current_data_->timestamp)) {
                current_data_ = new_data;
                view_model_.update_from_snapshot(current_data_);
            }
            last_update = now;
        }

        // Render
        render();

        // Small sleep to reduce CPU usage
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // Cleanup
    data_store_->stop();
    cleanup_windows();

    // Disable mouse tracking
    printf("\033[?1000l");
    fflush(stdout);

    endwin();

    // Reset terminal title
    printf("\033]0;\007");
    fflush(stdout);
}

int TuiApp::calc_system_panel_height() const {
    if (!view_model_.system_panel.is_visible) return 0;
    if (!system_panel_expanded_) return kSystemPanelCollapsedHeight;

    // Expanded: calculate rows needed for all CPUs
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    (void)max_y;

    size_t num_cpus = view_model_.system_panel.per_cpu_usage.size();
    if (num_cpus == 0) num_cpus = 1;

    constexpr int bar_width = 15;  // Shorter bars in expanded mode
    constexpr int cpu_section_width = bar_width + 12;
    const int cpus_per_row = std::max(1, (max_x - 2) / cpu_section_width);

    const int cpu_rows = static_cast<int>((num_cpus + cpus_per_row - 1) / cpus_per_row);
    return cpu_rows + 2;  // +2 for memory row and tasks row
}

void TuiApp::create_windows() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    terminal_too_small_ = false;

    // Minimum terminal size check
    constexpr int kMinWidth = 40;
    constexpr int kMinProcessHeight = 5;
    const int min_system_height = view_model_.system_panel.is_visible ? kSystemPanelCollapsedHeight : 0;

    if (const int min_total_height = kStatusBarHeight + kMinDetailsHeight + kMinProcessHeight + min_system_height; max_x < kMinWidth || max_y < min_total_height) {
        // Terminal too small - show minimal message and don't create windows
        clear();
        mvprintw(0, 0, "Terminal too small");
        mvprintw(1, 0, "Min: %dx%d", kMinWidth, min_total_height);
        mvprintw(2, 0, "Now: %dx%d", max_x, max_y);
        mvprintw(3, 0, "Resize or press q to quit");
        refresh();
        // Set windows to null so render functions can check
        system_win_ = nullptr;
        process_win_ = nullptr;
        details_win_ = nullptr;
        status_win_ = nullptr;
        process_win_y_ = 0;
        process_win_height_ = 0;
        details_win_y_ = 0;
        details_win_height_ = 0;
        visible_process_rows_ = 0;
        visible_details_rows_ = 0;
        terminal_too_small_ = true;
        return;
    }

    // Calculate panel heights
    int system_height = view_model_.system_panel.is_visible ? calc_system_panel_height() : 0;
    if (const int max_system_height = max_y - (kStatusBarHeight + kMinDetailsHeight + kMinProcessHeight); view_model_.system_panel.is_visible && system_height > max_system_height) {
        // Auto-collapse if expanded panel won't fit
        system_panel_expanded_ = false;
        system_height = kSystemPanelCollapsedHeight;
    }
    constexpr int status_height = kStatusBarHeight;
    const int remaining = std::max(10, max_y - system_height - status_height);
    int process_height = std::max(kMinProcessHeight, static_cast<int>(remaining * kProcessPanelRatio));
    int details_height = std::max(kMinDetailsHeight, remaining - process_height);
    if (process_height + details_height > remaining) {
        process_height = std::max(kMinProcessHeight, remaining - kMinDetailsHeight);
        details_height = remaining - process_height;
    }

    // Create windows
    int y = 0;

    if (view_model_.system_panel.is_visible) {
        system_win_ = newwin(system_height, max_x, y, 0);
        y += system_height;
    }

    // Track process window position for mouse clicks
    process_win_y_ = y;
    process_win_height_ = process_height;
    process_win_ = newwin(process_height, max_x, y, 0);
    y += process_height;
    visible_process_rows_ = std::max(1, process_height - 2);  // Account for border, min 1

    // Track details window position for mouse clicks
    details_win_y_ = y;
    details_win_height_ = details_height;
    details_win_ = newwin(details_height, max_x, y, 0);
    y += details_height;
    visible_details_rows_ = std::max(1, details_height - 3);  // Account for border and tabs, min 1

    status_win_ = newwin(status_height, max_x, y, 0);

    // Enable keypad for all windows
    if (system_win_) keypad(system_win_, TRUE);
    if (process_win_) keypad(process_win_, TRUE);
    if (details_win_) keypad(details_win_, TRUE);
    if (status_win_) keypad(status_win_, TRUE);
}

void TuiApp::resize_windows() {
    cleanup_windows();
    create_windows();
}

void TuiApp::cleanup_windows() {
    if (system_win_) {
        delwin(system_win_);
        system_win_ = nullptr;
    }
    if (process_win_) {
        delwin(process_win_);
        process_win_ = nullptr;
    }
    if (details_win_) {
        delwin(details_win_);
        details_win_ = nullptr;
    }
    if (status_win_) {
        delwin(status_win_);
        status_win_ = nullptr;
    }
}

void TuiApp::render() {
    // Safety check: verify we have valid windows
    if (!process_win_ || !details_win_ || !status_win_) {
        // Terminal too small - create_windows() already showed message
        // Just refresh so the "terminal too small" message stays visible
        refresh();
        return;
    }

    // Clear all windows
    if (system_win_) werase(system_win_);
    werase(process_win_);
    werase(details_win_);
    werase(status_win_);

    // Render panels
    if (view_model_.system_panel.is_visible && system_win_) {
        render_system_panel();
    }

    render_process_tree();

    render_details_panel();
    render_status_bar();

    // Refresh all windows
    if (system_win_) wrefresh(system_win_);
    wrefresh(process_win_);
    wrefresh(details_win_);
    wrefresh(status_win_);

    // Render overlays AFTER main window refresh
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

void TuiApp::draw_box_title(WINDOW* win, const std::string& title) {
    box(win, 0, 0);
    if (!title.empty()) {
        wattron(win, COLOR_PAIR(COLOR_PAIR_TITLE) | A_BOLD);
        mvwprintw(win, 0, 2, " %s ", title.c_str());
        wattroff(win, COLOR_PAIR(COLOR_PAIR_TITLE) | A_BOLD);
    }
}

void TuiApp::draw_progress_bar(WINDOW* win, int y, int x, int width,
                               double percent, int color_pair, const std::string& label) {
    if (width < 3) return;

    const int bar_width = width - 2;  // Account for brackets
    const int filled = static_cast<int>(bar_width * std::clamp(percent, 0.0, 100.0) / 100.0);

    mvwaddch(win, y, x, '[');

    wattron(win, COLOR_PAIR(color_pair));
    for (int i = 0; i < filled; ++i) {
        waddch(win, ACS_CKBOARD);
    }
    wattroff(win, COLOR_PAIR(color_pair));

    for (int i = filled; i < bar_width; ++i) {
        waddch(win, ' ');
    }
    waddch(win, ']');

    if (!label.empty()) {
        mvwprintw(win, y, x + width + 1, "%s", label.c_str());
    }
}

void TuiApp::draw_cpu_bar(WINDOW* win, int y, int x, int width,
                          double user_pct, double system_pct, const std::string& label) {
    if (width < 3) return;

    const int bar_width = width - 2;
    const int user_chars = static_cast<int>(bar_width * std::clamp(user_pct, 0.0, 100.0) / 100.0);
    int system_chars = static_cast<int>(bar_width * std::clamp(system_pct, 0.0, 100.0) / 100.0);

    // Don't exceed bar width
    if (user_chars + system_chars > bar_width) {
        system_chars = bar_width - user_chars;
    }

    mvwaddch(win, y, x, '[');

    // User (green)
    wattron(win, COLOR_PAIR(COLOR_PAIR_CPU_BAR_USER));
    for (int i = 0; i < user_chars; ++i) {
        waddch(win, ACS_CKBOARD);
    }
    wattroff(win, COLOR_PAIR(COLOR_PAIR_CPU_BAR_USER));

    // System (red)
    wattron(win, COLOR_PAIR(COLOR_PAIR_CPU_BAR_SYSTEM));
    for (int i = 0; i < system_chars; ++i) {
        waddch(win, ACS_CKBOARD);
    }
    wattroff(win, COLOR_PAIR(COLOR_PAIR_CPU_BAR_SYSTEM));

    // Empty space
    const int empty = bar_width - user_chars - system_chars;
    for (int i = 0; i < empty; ++i) {
        waddch(win, ' ');
    }
    waddch(win, ']');

    if (!label.empty()) {
        mvwprintw(win, y, x + width + 1, "%s", label.c_str());
    }
}

std::string TuiApp::format_bytes(int64_t bytes) {
    const char* units[] = {"B", "K", "M", "G", "T", "P"};
    int unit_idx = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unit_idx < 5) {
        size /= 1024.0;
        unit_idx++;
    }

    std::ostringstream oss;
    if (unit_idx == 0) {
        oss << bytes << units[unit_idx];
    } else if (size >= 100.0) {
        oss << std::fixed << std::setprecision(0) << size << units[unit_idx];
    } else if (size >= 10.0) {
        oss << std::fixed << std::setprecision(1) << size << units[unit_idx];
    } else {
        oss << std::fixed << std::setprecision(2) << size << units[unit_idx];
    }
    return oss.str();
}

std::string TuiApp::format_uptime(int64_t seconds) {
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

std::vector<ProcessNode*> TuiApp::get_visible_items() const {
    std::vector<ProcessNode*> items;
    if (!current_data_) return items;

    for (const auto& root : current_data_->process_tree) {
        collect_visible_items(root.get(), items, view_model_.process_list.collapsed_pids);
    }
    return items;
}

void TuiApp::collect_visible_items(ProcessNode* node, std::vector<ProcessNode*>& items,
                                   const std::set<int>& collapsed) {
    if (!node) return;

    items.push_back(node);

    if (const bool is_collapsed = collapsed.contains(node->info.pid); !is_collapsed) {
        for (const auto& child : node->children) {
            collect_visible_items(child.get(), items, collapsed);
        }
    }
}

void TuiApp::move_selection(int delta) {
    const auto visible = get_visible_items();
    if (visible.empty()) return;

    // Find current position
    int current_pos = 0;
    for (size_t i = 0; i < visible.size(); ++i) {
        if (visible[i]->info.pid == view_model_.process_list.selected_pid) {
            current_pos = static_cast<int>(i);
            break;
        }
    }

    // Move
    const int new_pos = std::clamp(current_pos + delta, 0, static_cast<int>(visible.size()) - 1);
    view_model_.process_list.selected_pid = visible[new_pos]->info.pid;

    scroll_to_selection();
}

void TuiApp::page_up() {
    move_selection(-visible_process_rows_);
}

void TuiApp::page_down() {
    move_selection(visible_process_rows_);
}

void TuiApp::scroll_to_selection() {
    const auto visible = get_visible_items();
    int selected_idx = 0;

    for (size_t i = 0; i < visible.size(); ++i) {
        if (visible[i]->info.pid == view_model_.process_list.selected_pid) {
            selected_idx = static_cast<int>(i);
            break;
        }
    }

    // Adjust scroll offset
    if (selected_idx < process_scroll_offset_) {
        process_scroll_offset_ = selected_idx;
    } else if (selected_idx >= process_scroll_offset_ + visible_process_rows_) {
        process_scroll_offset_ = selected_idx - visible_process_rows_ + 1;
    }
}

bool TuiApp::matches_search(const ProcessInfo& info) const {
    if (view_model_.process_list.search_text.empty()) return false;

    const auto& search = view_model_.process_list.search_text;

    // Case-insensitive search in name and command
    auto to_lower = [](const std::string& s) {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    };

    const std::string search_lower = to_lower(search);

    if (to_lower(info.name).find(search_lower) != std::string::npos) return true;
    if (to_lower(info.command_line).find(search_lower) != std::string::npos) return true;

    // Also try matching PID
    if (std::to_string(info.pid) == search) return true;

    return false;
}

std::vector<ProcessNode*> TuiApp::find_matching_processes() const {
    std::vector<ProcessNode*> matches;

    for (const auto visible = get_visible_items(); auto* node : visible) {
        if (matches_search(node->info)) {
            matches.push_back(node);
        }
    }
    return matches;
}

void TuiApp::search_select_first() {
    const auto matches = find_matching_processes();
    if (!matches.empty()) {
        view_model_.process_list.selected_pid = matches[0]->info.pid;
        scroll_to_selection();
    }
}

void TuiApp::search_next() {
    const auto matches = find_matching_processes();
    if (matches.empty()) return;

    // Find current position in matches
    for (size_t i = 0; i < matches.size(); ++i) {
        if (matches[i]->info.pid == view_model_.process_list.selected_pid) {
            // Select next match (wrap around)
            const size_t next = (i + 1) % matches.size();
            view_model_.process_list.selected_pid = matches[next]->info.pid;
            scroll_to_selection();
            return;
        }
    }

    // If current selection isn't a match, select first match
    view_model_.process_list.selected_pid = matches[0]->info.pid;
    scroll_to_selection();
}

void TuiApp::search_previous() {
    const auto matches = find_matching_processes();
    if (matches.empty()) return;

    for (size_t i = 0; i < matches.size(); ++i) {
        if (matches[i]->info.pid == view_model_.process_list.selected_pid) {
            // Select previous match (wrap around)
            const size_t prev = (i == 0) ? matches.size() - 1 : i - 1;
            view_model_.process_list.selected_pid = matches[prev]->info.pid;
            scroll_to_selection();
            return;
        }
    }

    view_model_.process_list.selected_pid = matches[0]->info.pid;
    scroll_to_selection();
}

void TuiApp::next_tab() {
    int tab = static_cast<int>(view_model_.details_panel.active_tab);
    tab = (tab + 1) % 6;
    view_model_.details_panel.active_tab = static_cast<DetailsTab>(tab);
    details_scroll_offset_ = 0;
}

void TuiApp::prev_tab() {
    int tab = static_cast<int>(view_model_.details_panel.active_tab);
    tab = (tab + 5) % 6;  // +5 is same as -1 mod 6
    view_model_.details_panel.active_tab = static_cast<DetailsTab>(tab);
    details_scroll_offset_ = 0;
}

void TuiApp::request_kill_process(int pid, const std::string& name, bool is_tree) {
    view_model_.kill_dialog.is_visible = true;
    view_model_.kill_dialog.target_pid = pid;
    view_model_.kill_dialog.target_name = name;
    view_model_.kill_dialog.is_tree_kill = is_tree;
    view_model_.kill_dialog.error_message.clear();
    view_model_.kill_dialog.show_force_option = false;
    flushinp();  // Clear any pending input
    dialog_debounce_ = 5;  // Ignore input for 5 frames
}

void TuiApp::execute_kill(bool force) {
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
        // Process didn't terminate, offer force kill
        kd.show_force_option = true;
        kd.error_message = "Process still running after SIGTERM";
    } else {
        kd.error_message = result.error_message;
    }
}

void TuiApp::collect_tree_pids(const ProcessNode* node, std::vector<int>& pids) {
    if (!node) return;
    pids.push_back(node->info.pid);
    for (const auto& child : node->children) {
        collect_tree_pids(child.get(), pids);
    }
}

} // namespace pex
