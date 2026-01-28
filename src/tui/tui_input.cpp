#include "tui_app.hpp"
#include "tui_colors.hpp"

namespace pex {

void TuiApp::handle_input(int ch) {
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

        case 's':  // Toggle system panel
            view_model_.system_panel.is_visible = !view_model_.system_panel.is_visible;
            resize_windows();
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
            // Expand node (tree view) or no-op (list view)
            if (pl.is_tree_view && pl.selected_pid > 0) {
                pl.collapsed_pids.erase(pl.selected_pid);
            }
            break;

        case KEY_LEFT:
            // Collapse node (tree view) or no-op (list view)
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
            }
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
    }
}

void TuiApp::handle_help_input([[maybe_unused]] int ch) {
    // Any key closes help
    show_help_ = false;
}

} // namespace pex
