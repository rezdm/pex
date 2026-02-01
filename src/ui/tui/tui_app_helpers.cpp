#include "tui_app.hpp"
#include "tui_colors.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace pex {

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
    auto size = static_cast<double>(bytes);

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

} // namespace pex
