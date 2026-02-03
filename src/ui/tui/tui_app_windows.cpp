#include "tui_app.hpp"
#include <algorithm>

namespace pex {

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

    // If any critical window failed to allocate, treat as terminal too small
    if (!process_win_ || !details_win_ || !status_win_) {
        cleanup_windows();
        terminal_too_small_ = true;
        return;
    }

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

} // namespace pex
