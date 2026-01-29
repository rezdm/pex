#include "tui_app.hpp"
#include "tui_colors.hpp"
#include <algorithm>

namespace pex {

void TuiApp::handle_input(int ch) {
    // Debounce: ignore input for a few frames after showing dialogs
    if (dialog_debounce_ > 0) {
        dialog_debounce_--;
        // Still consume mouse events to prevent queue buildup
        if (ch == KEY_MOUSE) {
            MEVENT event;
            getmouse(&event);
        }
        return;
    }

    // Help overlay takes priority
    if (show_help_) {
        handle_help_input(ch);
        return;
    }

    // Kill dialog takes priority
    if (view_model_.kill_dialog.is_visible) {
        handle_kill_dialog_input(ch);
        return;
    }

    // Search mode takes priority
    if (search_mode_) {
        handle_search_input(ch);
        return;
    }

    // Global keys
    switch (ch) {
        case 'q':
        case 'Q':
            running_ = false;
            return;

        case '?':
        case KEY_F(1):
            show_help_ = true;
            flushinp();  // Clear any pending input
            dialog_debounce_ = 5;  // Ignore input for 5 frames
            return;

        case '/':
            search_mode_ = true;
            search_input_.clear();
            return;

        case 'n':  // Next search match
            if (!view_model_.process_list.search_text.empty()) {
                search_next();
            }
            return;

        case 'N':  // Previous search match
            if (!view_model_.process_list.search_text.empty()) {
                search_previous();
            }
            return;

        case 's':  // Toggle system panel visibility
            view_model_.system_panel.is_visible = !view_model_.system_panel.is_visible;
            resize_windows();
            return;

        case 'c':  // Toggle system panel expand/collapse (show all CPUs)
            if (view_model_.system_panel.is_visible) {
                system_panel_expanded_ = !system_panel_expanded_;
                resize_windows();
            }
            return;

        case 't':  // Toggle tree/list view
            view_model_.process_list.is_tree_view = !view_model_.process_list.is_tree_view;
            process_scroll_offset_ = 0;
            return;

        case '\t':  // Tab - switch panel focus
            if (current_focus_ == PanelFocus::ProcessList) {
                current_focus_ = PanelFocus::DetailsPanel;
            } else {
                current_focus_ = PanelFocus::ProcessList;
            }
            return;

        case KEY_BTAB:  // Shift+Tab - reverse switch
            if (current_focus_ == PanelFocus::DetailsPanel) {
                current_focus_ = PanelFocus::ProcessList;
            } else {
                current_focus_ = PanelFocus::DetailsPanel;
            }
            return;

        case 'r':
        case KEY_F(5):
            data_store_->refresh_now();
            return;

        case 27:  // Escape - clear search
            view_model_.process_list.search_text.clear();
            return;

        case KEY_MOUSE:
            handle_mouse_event();
            return;
    }

    // Panel-specific input
    if (current_focus_ == PanelFocus::ProcessList) {
        handle_process_list_input(ch);
    } else {
        handle_details_panel_input(ch);
    }
}

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
                auto visible = get_visible_items();
                if (!visible.empty()) {
                    pl.selected_pid = visible.front()->info.pid;
                    process_scroll_offset_ = 0;
                }
            }
            break;

        case KEY_END:
        case 'G':
            {
                auto visible = get_visible_items();
                if (!visible.empty()) {
                    pl.selected_pid = visible.back()->info.pid;
                    scroll_to_selection();
                }
            }
            break;

        case KEY_RIGHT:
        case '\n':
        case '\r':
            // Expand node (tree view) or horizontal scroll (list view)
            if (pl.is_tree_view && pl.selected_pid > 0) {
                pl.collapsed_pids.erase(pl.selected_pid);
            } else if (!pl.is_tree_view) {
                // Horizontal scroll right in list view
                process_h_scroll_ += 10;
            }
            break;

        case KEY_LEFT:
            // Collapse node (tree view) or horizontal scroll (list view)
            if (pl.is_tree_view && pl.selected_pid > 0) {
                // Check if node has children
                if (current_data_ && current_data_->process_map.count(pl.selected_pid)) {
                    auto* node = current_data_->process_map.at(pl.selected_pid);
                    if (!node->children.empty()) {
                        // Collapse this node
                        pl.collapsed_pids.insert(pl.selected_pid);
                    } else {
                        // No children - go to parent
                        int parent_pid = node->info.parent_pid;
                        if (current_data_->process_map.count(parent_pid)) {
                            pl.selected_pid = parent_pid;
                            scroll_to_selection();
                        }
                    }
                }
            } else if (!pl.is_tree_view) {
                // Horizontal scroll left in list view
                process_h_scroll_ = std::max(0, process_h_scroll_ - 10);
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
            if (pl.selected_pid > 0 && current_data_ && current_data_->process_map.count(pl.selected_pid)) {
                auto* node = current_data_->process_map.at(pl.selected_pid);
                request_kill_process(pl.selected_pid, node->info.name, true);
            }
            break;

        case 'x':  // Kill process (single)
            if (pl.selected_pid > 0 && current_data_ && current_data_->process_map.count(pl.selected_pid)) {
                auto* node = current_data_->process_map.at(pl.selected_pid);
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
    }
}

void TuiApp::handle_search_input(int ch) {
    switch (ch) {
        case 27:  // Escape
            search_mode_ = false;
            curs_set(0);
            break;

        case '\n':
        case '\r':
            // Commit search
            search_mode_ = false;
            curs_set(0);
            view_model_.process_list.search_text = search_input_;
            search_select_first();
            break;

        case KEY_BACKSPACE:
        case 127:
        case '\b':
            if (!search_input_.empty()) {
                search_input_.pop_back();
            }
            break;

        default:
            // Add printable characters to search
            if (ch >= 32 && ch < 127) {
                search_input_ += static_cast<char>(ch);
            }
            break;
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

void TuiApp::handle_help_input(int ch) {
    // Ignore mouse events in help overlay
    if (ch == KEY_MOUSE) {
        // Consume the mouse event but don't close
        MEVENT event;
        getmouse(&event);
        return;
    }

    // Close help on specific keys only (not on escape sequences)
    switch (ch) {
        case 27:        // Escape
        case 'q':
        case 'Q':
        case '\n':
        case '\r':
        case ' ':
        case '?':
        case KEY_F(1):
            show_help_ = false;
            break;
        // Ignore other keys (escape sequence bytes, etc.)
        default:
            break;
    }
}

void TuiApp::handle_mouse_event() {
    MEVENT event;
    if (getmouse(&event) != OK) {
        return;
    }

    int y = event.y;
    int x = event.x;

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

    if (event.bstate & BUTTON5_PRESSED) {
        // Scroll down (wheel down)
        if (current_focus_ == PanelFocus::ProcessList) {
            move_selection(3);
        } else {
            details_scroll_offset_ += 3;
        }
        return;
    }

    // Handle clicks
    if (event.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED | BUTTON1_RELEASED)) {
        // Check if click is in process panel
        if (y >= process_win_y_ && y < process_win_y_ + process_win_height_) {
            // Switch focus to process panel
            current_focus_ = PanelFocus::ProcessList;

            // Calculate which row was clicked (accounting for border and header)
            int row_in_window = y - process_win_y_;
            if (row_in_window >= 2 && row_in_window < process_win_height_ - 1) {
                // Valid data row clicked
                int clicked_index = process_scroll_offset_ + (row_in_window - 2);

                auto visible = get_visible_items();
                if (clicked_index >= 0 && clicked_index < static_cast<int>(visible.size())) {
                    int clicked_pid = visible[clicked_index]->info.pid;

                    // Check for double-click to toggle expand/collapse
                    if (event.bstate & BUTTON1_DOUBLE_CLICKED) {
                        if (view_model_.process_list.is_tree_view) {
                            auto& collapsed = view_model_.process_list.collapsed_pids;
                            if (collapsed.count(clicked_pid)) {
                                collapsed.erase(clicked_pid);
                            } else {
                                // Only collapse if has children
                                if (!visible[clicked_index]->children.empty()) {
                                    collapsed.insert(clicked_pid);
                                }
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
            int row_in_window = y - details_win_y_;
            if (row_in_window == 1) {
                // Tab bar - determine which tab was clicked based on x position
                // Tab positions (approximate): [F]iles, [N]etwork, [T]hreads, [M]emory, [E]nv, [L]ibraries
                // Each tab is roughly 10-12 chars wide, starting at x=2
                int tab_x = x - 2;  // Adjust for border
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
