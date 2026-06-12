#include "tui_app.hpp"
#include "tui_colors.hpp"
#include <algorithm>
#include <sstream>

namespace pex {

void TuiApp::render_kill_dialog() const {
    const auto& kd = view_model_.kill_dialog;
    if (!kd.is_visible) return;

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    // Dialog dimensions
    const int dialog_width = 50;
    int dialog_height = kd.show_force_option ? 10 : 8;
    if (!kd.error_message.empty()) dialog_height += 2;

    const int dialog_x = (max_x - dialog_width) / 2;
    const int dialog_y = (max_y - dialog_height) / 2;

    // Create temporary window for dialog
    WINDOW* dialog_win = newwin(dialog_height, dialog_width, dialog_y, dialog_x);
    if (!dialog_win) return;

    // Draw dialog background and border
    wbkgd(dialog_win, COLOR_PAIR(COLOR_PAIR_DIALOG));
    box(dialog_win, 0, 0);

    // Title
    const std::string title = kd.is_tree_kill ? " Kill Process Tree " : " Kill Process ";
    wattron(dialog_win, A_BOLD);
    mvwprintw(dialog_win, 0, (dialog_width - title.length()) / 2, "%s", title.c_str());
    wattroff(dialog_win, A_BOLD);

    // Process info
    std::ostringstream msg;
    if (kd.is_tree_kill) {
        msg << "Kill process tree starting at:";
    } else {
        msg << "Kill process:";
    }
    mvwprintw(dialog_win, 2, 2, "%s", msg.str().c_str());

    std::ostringstream proc_info;
    proc_info << kd.target_name << " (PID " << kd.target_pid << ")";
    wattron(dialog_win, A_BOLD);
    mvwprintw(dialog_win, 3, 4, "%s", proc_info.str().c_str());
    wattroff(dialog_win, A_BOLD);

    int row = 5;

    // Error message
    if (!kd.error_message.empty()) {
        wattron(dialog_win, COLOR_PAIR(COLOR_PAIR_ERROR) | A_BOLD);
        mvwprintw(dialog_win, row++, 2, "%s", kd.error_message.c_str());
        wattroff(dialog_win, COLOR_PAIR(COLOR_PAIR_ERROR) | A_BOLD);
        row++;
    }

    // Buttons
    if (kd.show_force_option) {
        // Show force kill option
        mvwprintw(dialog_win, row++, 2, "Process did not terminate. Force kill?");

        wattron(dialog_win, COLOR_PAIR(COLOR_PAIR_DIALOG_BUTTON));
        mvwprintw(dialog_win, row, 6, " [Y] Force Kill (SIGKILL) ");
        wattroff(dialog_win, COLOR_PAIR(COLOR_PAIR_DIALOG_BUTTON));

        mvwprintw(dialog_win, row, 35, " [N] Cancel ");
    } else {
        wattron(dialog_win, COLOR_PAIR(COLOR_PAIR_DIALOG_BUTTON));
        mvwprintw(dialog_win, row, 6, " [Y] Kill (SIGTERM) ");
        wattroff(dialog_win, COLOR_PAIR(COLOR_PAIR_DIALOG_BUTTON));

        mvwprintw(dialog_win, row, 30, " [N] Cancel ");
    }

    // Refresh dialog
    wrefresh(dialog_win);

    // Delete temporary window (but leave content on screen until next render)
    delwin(dialog_win);
}

void TuiApp::render_help_overlay() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    // Help dialog dimensions (shrink to fit small terminals; content clips)
    constexpr int help_width = 60;
    const int help_height = std::min(39, max_y - 2);
    const int help_x = (max_x - help_width) / 2;
    const int help_y = (max_y - help_height) / 2;

    WINDOW* help_win = newwin(help_height, help_width, help_y, help_x);
    if (!help_win) return;

    wbkgd(help_win, COLOR_PAIR(COLOR_PAIR_DIALOG));
    box(help_win, 0, 0);

    // Title
    wattron(help_win, A_BOLD);
    mvwprintw(help_win, 0, (help_width - 6) / 2, " Help ");
    wattroff(help_win, A_BOLD);

    // Help content
    const char* help_lines[] = {
        "Navigation:",
        "  Up/k, Down/j    Move selection up/down",
        "  PgUp, PgDn      Page up/down",
        "  Home/g, End/G   Jump to first/last",
        "  Tab             Switch panel focus",
        "",
        "Process Tree:",
        "  Enter/Right     Expand node",
        "  Left            Collapse node or go to parent",
        "  </> or ,/.      Horizontal scroll",
        "  0               Reset horizontal scroll",
        "  s               Toggle system panel",
        "  c               Expand/collapse CPUs",
        "",
        "Details Panel:",
        "  1-6             Switch tab by number",
        "  f/w/h/m/e/l     Switch tab by letter",
        "  Left/Right      Previous/next tab",
        "",
        "Mouse:",
        "  Click           Select row / switch panel",
        "  Double-click    Expand/collapse tree node",
        "  Scroll wheel    Scroll up/down",
        "  Click on tab    Switch details tab",
        "",
        "Actions:",
        "  /               Search mode",
        "  n/N             Next/previous search match",
        "  o               Find open file/handle",
        "  d               Dump history to CSV (~/pex-history-*)",
        "  u               Show/hide kernel threads",
        "  x               Kill process",
        "  K               Kill process tree",
        "  r/F5            Force refresh",
        "  q               Quit",
        "  ?/F1            This help"
    };

    int row = 2;
    for (const char* line : help_lines) {
        if (row >= help_height - 1) break;

        if (line[0] == ' ' && line[1] == ' ') {
            // Key binding line
            std::string key(line, 2, 16);
            std::string desc(line + 18);

            wattron(help_win, COLOR_PAIR(COLOR_PAIR_HELP_KEY));
            mvwprintw(help_win, row, 2, "%s", key.c_str());
            wattroff(help_win, COLOR_PAIR(COLOR_PAIR_HELP_KEY));
            mvwprintw(help_win, row, 18, "%s", desc.c_str());
        } else {
            wattron(help_win, A_BOLD);
            mvwprintw(help_win, row, 2, "%s", line);
            wattroff(help_win, A_BOLD);
        }
        row++;
    }

    // Close instruction
    wattron(help_win, COLOR_PAIR(COLOR_PAIR_DIALOG_BUTTON));
    mvwprintw(help_win, help_height - 2, (help_width - 20) / 2, " Press any key to close ");
    wattroff(help_win, COLOR_PAIR(COLOR_PAIR_DIALOG_BUTTON));

    wrefresh(help_win);
    delwin(help_win);
}

void TuiApp::render_status_bar() const {
    if (!status_win_) return;

    const int max_x = getmaxx(status_win_);

    wbkgd(status_win_, COLOR_PAIR(COLOR_PAIR_STATUS));
    werase(status_win_);

    // Transient status message (e.g. history export result) replaces hints
    if (!status_message_.empty() &&
        std::chrono::steady_clock::now() - status_message_time_ < kStatusMessageDuration) {
        wattron(status_win_, A_BOLD);
        mvwprintw(status_win_, 0, 1, "%.*s", max_x - 2, status_message_.c_str());
        wattroff(status_win_, A_BOLD);
        return;
    }

    // Left side: key hints
    const char* hints = "q:Quit  /:Search  o:FindFile  d:HistCSV  s:System  Tab:Panel  x:Kill  ?:Help";
    mvwprintw(status_win_, 0, 1, "%s", hints);

    // Right side: search text if active
    if (!view_model_.process_list.search_text.empty()) {
        std::string search_indicator = "Search: " + view_model_.process_list.search_text;
        if (search_indicator.length() > 30) {
            search_indicator = search_indicator.substr(0, 27) + "...";
        }
        const int search_x = max_x - search_indicator.length() - 2;
        if (search_x > 0) {
            mvwprintw(status_win_, 0, search_x, "%s", search_indicator.c_str());
        }
    }
}

void TuiApp::render_search_bar() const {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    // Search bar at bottom
    const int search_y = max_y - 2;
    const int search_width = std::min(50, max_x - 4);
    const int search_x = (max_x - search_width) / 2;

    WINDOW* search_win = newwin(3, search_width, search_y, search_x);
    if (!search_win) return;

    wbkgd(search_win, COLOR_PAIR(COLOR_PAIR_DIALOG));
    box(search_win, 0, 0);

    mvwprintw(search_win, 0, 2, " Search ");
    mvwprintw(search_win, 1, 2, "/ %s", search_input_.c_str());

    // Show cursor
    curs_set(1);
    wmove(search_win, 1, 3 + search_input_.length());

    wrefresh(search_win);
    delwin(search_win);
}

} // namespace pex
