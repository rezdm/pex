#include "tui_app.hpp"
#include "tui_colors.hpp"
#include <algorithm>

namespace pex {

void TuiApp::handle_process_list_input(int ch) {
    auto& pl = view_model_.process_list;

    switch (ch) {
        case KEY_UP:
        case 'k':
            move_selection(-1);
            break;

        case KEY_DOWN:
        case 'j':
            move_selection(1);
            break;

        case KEY_PPAGE:
            page_up();
            break;

        case KEY_NPAGE:
            page_down();
            break;

        case KEY_HOME:
        case 'g':
            {
                const auto visible = get_visible_items();
                if (!visible.empty()) {
                    pl.selected_pid = visible.front()->info.pid;
                    process_scroll_offset_ = 0;
                }
            }
            break;

        case KEY_END:
        case 'G':
            {
                const auto visible = get_visible_items();
                if (!visible.empty()) {
                    pl.selected_pid = visible.back()->info.pid;
                    scroll_to_selection();
                }
            }
            break;

        case KEY_RIGHT:
        case '\n':
        case '\r':
            // Expand node in tree view
            if (pl.selected_pid > 0) {
                pl.collapsed_pids.erase(pl.selected_pid);
            }
            break;

        case KEY_LEFT:
            // Collapse node in tree view
            if (pl.selected_pid > 0 && current_data_ && current_data_->process_map.contains(pl.selected_pid)) {
                const auto* node = current_data_->process_map.at(pl.selected_pid);
                if (!node->children.empty()) {
                    // Collapse this node
                    pl.collapsed_pids.insert(pl.selected_pid);
                } else {
                    // No children - go to parent
                    const int parent_pid = node->info.parent_pid;
                    if (current_data_->process_map.contains(parent_pid)) {
                        pl.selected_pid = parent_pid;
                        scroll_to_selection();
                    }
                }
            }
            break;

        case '>':  // Horizontal scroll right (works in both views)
        case '.':  // Alternative for > without shift
            process_h_scroll_ += 10;
            break;

        case '<':  // Horizontal scroll left (works in both views)
        case ',':  // Alternative for < without shift
            process_h_scroll_ = std::max(0, process_h_scroll_ - 10);
            break;

        case KEY_SHOME:  // Shift+Home - reset horizontal scroll
        case '0':
            process_h_scroll_ = 0;
            break;

        case 'K':  // Kill process tree
            if (pl.selected_pid > 0 && current_data_ && current_data_->process_map.contains(pl.selected_pid)) {
                const auto* node = current_data_->process_map.at(pl.selected_pid);
                request_kill_process(pl.selected_pid, node->info.name, true);
            }
            break;

        case 'x':  // Kill process (single)
            if (pl.selected_pid > 0 && current_data_ && current_data_->process_map.contains(pl.selected_pid)) {
                const auto* node = current_data_->process_map.at(pl.selected_pid);
                request_kill_process(pl.selected_pid, node->info.name, false);
            }
            break;

        // Tab switching shortcuts (1-6)
        case '1':
            view_model_.details_panel.active_tab = DetailsTab::FileHandles;
            details_scroll_offset_ = 0;
            break;
        case '2':
            view_model_.details_panel.active_tab = DetailsTab::Network;
            details_scroll_offset_ = 0;
            break;
        case '3':
            view_model_.details_panel.active_tab = DetailsTab::Threads;
            details_scroll_offset_ = 0;
            break;
        case '4':
            view_model_.details_panel.active_tab = DetailsTab::Memory;
            details_scroll_offset_ = 0;
            break;
        case '5':
            view_model_.details_panel.active_tab = DetailsTab::Environment;
            details_scroll_offset_ = 0;
            break;
        case '6':
            view_model_.details_panel.active_tab = DetailsTab::Libraries;
            details_scroll_offset_ = 0;
            break;

        // Tab switching by letter
        case 'f':
            view_model_.details_panel.active_tab = DetailsTab::FileHandles;
            details_scroll_offset_ = 0;
            break;
        case 'w':  // 'n' is used for search, use 'w' for network
            view_model_.details_panel.active_tab = DetailsTab::Network;
            details_scroll_offset_ = 0;
            break;
        case 'h':  // 't' is used for tree toggle, use 'h' for threads
            view_model_.details_panel.active_tab = DetailsTab::Threads;
            details_scroll_offset_ = 0;
            break;
        case 'm':
            view_model_.details_panel.active_tab = DetailsTab::Memory;
            details_scroll_offset_ = 0;
            break;
        case 'e':
            view_model_.details_panel.active_tab = DetailsTab::Environment;
            details_scroll_offset_ = 0;
            break;
        case 'l':
            view_model_.details_panel.active_tab = DetailsTab::Libraries;
            details_scroll_offset_ = 0;
            break;
        default: ;
    }
}

void TuiApp::handle_details_panel_input(int ch) {
    switch (ch) {
        case KEY_UP:
        case 'k':
            if (details_scroll_offset_ > 0) {
                details_scroll_offset_--;
            }
            break;

        case KEY_DOWN:
        case 'j':
            details_scroll_offset_++;
            break;

        case KEY_PPAGE:
            details_scroll_offset_ = std::max(0, details_scroll_offset_ - visible_details_rows_);
            break;

        case KEY_NPAGE:
            details_scroll_offset_ += visible_details_rows_;
            break;

        case KEY_HOME:
        case 'g':
            details_scroll_offset_ = 0;
            break;

        case KEY_LEFT:
            prev_tab();
            break;

        case KEY_RIGHT:
            next_tab();
            break;

        // Tab switching shortcuts
        case '1':
            view_model_.details_panel.active_tab = DetailsTab::FileHandles;
            details_scroll_offset_ = 0;
            break;
        case '2':
            view_model_.details_panel.active_tab = DetailsTab::Network;
            details_scroll_offset_ = 0;
            break;
        case '3':
            view_model_.details_panel.active_tab = DetailsTab::Threads;
            details_scroll_offset_ = 0;
            break;
        case '4':
            view_model_.details_panel.active_tab = DetailsTab::Memory;
            details_scroll_offset_ = 0;
            break;
        case '5':
            view_model_.details_panel.active_tab = DetailsTab::Environment;
            details_scroll_offset_ = 0;
            break;
        case '6':
            view_model_.details_panel.active_tab = DetailsTab::Libraries;
            details_scroll_offset_ = 0;
            break;

        // Letter shortcuts
        case 'f':
            view_model_.details_panel.active_tab = DetailsTab::FileHandles;
            details_scroll_offset_ = 0;
            break;
        case 'w':
            view_model_.details_panel.active_tab = DetailsTab::Network;
            details_scroll_offset_ = 0;
            break;
        case 'h':
            view_model_.details_panel.active_tab = DetailsTab::Threads;
            details_scroll_offset_ = 0;
            break;
        case 'm':
            view_model_.details_panel.active_tab = DetailsTab::Memory;
            details_scroll_offset_ = 0;
            break;
        case 'e':
            view_model_.details_panel.active_tab = DetailsTab::Environment;
            details_scroll_offset_ = 0;
            break;
        case 'l':
            view_model_.details_panel.active_tab = DetailsTab::Libraries;
            details_scroll_offset_ = 0;
            break;
        default: ;
    }
}

void TuiApp::handle_kill_dialog_input(int ch) {
    // Ignore mouse events in kill dialog
    if (ch == KEY_MOUSE) {
        MEVENT event;
        getmouse(&event);
        return;
    }

    auto& kd = view_model_.kill_dialog;

    switch (ch) {
        case 'y':
        case 'Y':
            execute_kill(kd.show_force_option);
            break;

        case 'n':
        case 'N':
        case 27:  // Escape
            kd.is_visible = false;
            kd.target_pid = -1;
            break;
        // Ignore other keys
        default:
            break;
    }
}

void TuiApp::handle_mouse_event() {
    MEVENT event;
    if (getmouse(&event) != OK) {
        return;
    }

    const int y = event.y;
    const int x = event.x;

    // Handle mouse wheel scrolling
    if (event.bstate & BUTTON4_PRESSED) {
        // Scroll up (wheel up)
        if (current_focus_ == PanelFocus::ProcessList) {
            move_selection(-3);
        } else {
            details_scroll_offset_ = std::max(0, details_scroll_offset_ - 3);
        }
        return;
    }

    // BUTTON5_PRESSED (wheel down) only exists with ncurses mouse protocol v2
    // (NCURSES_MOUSE_VERSION >= 2); Solaris system ncurses headers are v1.
#ifdef BUTTON5_PRESSED
    if (event.bstate & BUTTON5_PRESSED) {
        // Scroll down (wheel down)
        if (current_focus_ == PanelFocus::ProcessList) {
            move_selection(3);
        } else {
            details_scroll_offset_ += 3;
        }
        return;
    }
#endif

    // Handle clicks
    if (event.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED | BUTTON1_RELEASED)) {
        // Check if click is in process panel
        if (y >= process_win_y_ && y < process_win_y_ + process_win_height_) {
            // Switch focus to process panel
            current_focus_ = PanelFocus::ProcessList;

            // Calculate which row was clicked (accounting for border and header)
            const int row_in_window = y - process_win_y_;
            if (row_in_window >= 2 && row_in_window < process_win_height_ - 1) {
                // Valid data row clicked
                const int clicked_index = process_scroll_offset_ + (row_in_window - 2);

                const auto visible = get_visible_items();
                if (clicked_index >= 0 && clicked_index < static_cast<int>(visible.size())) {
                    const int clicked_pid = visible[clicked_index]->info.pid;

                    // Check for double-click to toggle expand/collapse
                    if (event.bstate & BUTTON1_DOUBLE_CLICKED) {
                        auto& collapsed = view_model_.process_list.collapsed_pids;
                        if (collapsed.contains(clicked_pid)) {
                            collapsed.erase(clicked_pid);
                        } else {
                            // Only collapse if has children
                            if (!visible[clicked_index]->children.empty()) {
                                collapsed.insert(clicked_pid);
                            }
                        }
                    }

                    view_model_.process_list.selected_pid = clicked_pid;
                }
            }
            return;
        }

        // Check if click is in details panel
        if (y >= details_win_y_ && y < details_win_y_ + details_win_height_) {
            // Switch focus to details panel
            current_focus_ = PanelFocus::DetailsPanel;

            // Check if click is on tab bar (row 1 of details window)
            const int row_in_window = y - details_win_y_;
            if (row_in_window == 1) {
                // Tab bar - determine which tab was clicked based on x position
                // Tab positions (approximate): [F]iles, [N]etwork, [T]hreads, [M]emory, [E]nv, [L]ibraries
                // Each tab is roughly 10-12 chars wide, starting at x=2
                const int tab_x = x - 2;  // Adjust for border
                if (tab_x >= 0 && tab_x < 10) {
                    view_model_.details_panel.active_tab = DetailsTab::FileHandles;
                    details_scroll_offset_ = 0;
                } else if (tab_x >= 10 && tab_x < 21) {
                    view_model_.details_panel.active_tab = DetailsTab::Network;
                    details_scroll_offset_ = 0;
                } else if (tab_x >= 21 && tab_x < 32) {
                    view_model_.details_panel.active_tab = DetailsTab::Threads;
                    details_scroll_offset_ = 0;
                } else if (tab_x >= 32 && tab_x < 42) {
                    view_model_.details_panel.active_tab = DetailsTab::Memory;
                    details_scroll_offset_ = 0;
                } else if (tab_x >= 42 && tab_x < 50) {
                    view_model_.details_panel.active_tab = DetailsTab::Environment;
                    details_scroll_offset_ = 0;
                } else if (tab_x >= 50) {
                    view_model_.details_panel.active_tab = DetailsTab::Libraries;
                    details_scroll_offset_ = 0;
                }
            }
            return;
        }
    }
}

} // namespace pex
