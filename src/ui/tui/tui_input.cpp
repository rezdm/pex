#include "tui_app.hpp"
#include "tui_colors.hpp"

namespace pex {

void TuiApp::handle_input(int ch) {
    if (terminal_too_small_) {
        // Allow to quit and panel toggles as escape hatches to reclaim height
        switch (ch) {
            case 'q':
            case 'Q':
                running_ = false;
                break;
            case 's':  // Toggle system panel visibility
                view_model_.system_panel.is_visible = !view_model_.system_panel.is_visible;
                resize_windows();
                break;
            case 'c':  // Collapse expanded CPU view
                if (system_panel_expanded_) {
                    system_panel_expanded_ = false;
                    resize_windows();
                }
                break;
            default: ;
        }
        return;
    }

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

    // Find-open-file query input / results overlay (issue #7)
    if (find_file_mode_) {
        handle_find_file_input(ch);
        return;
    }
    if (find_results_visible_) {
        handle_find_results_input(ch);
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

        case 'o':  // Find open file/handle (issue #7)
            find_file_mode_ = true;
            find_file_input_.clear();
            return;

        case 'd':  // Dump (export) recorded history to CSV (issue #9)
            export_history();
            return;

        case 'u':  // Show/hide kernel threads (issue #2)
            view_model_.process_list.show_kernel_threads =
                !view_model_.process_list.show_kernel_threads;
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
            details_needs_refresh_ = true;  // Also refresh details panel
            return;

        case 27:  // Escape - clear search
            view_model_.process_list.search_text.clear();
            return;

        case KEY_MOUSE:
            handle_mouse_event();
            return;
        default: ;
    }

    // Panel-specific input
    if (current_focus_ == PanelFocus::ProcessList) {
        handle_process_list_input(ch);
    } else {
        handle_details_panel_input(ch);
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

void TuiApp::handle_help_input(int ch) {
    // Ignore mouse events in help overlay
    if (ch == KEY_MOUSE) {
        // Consume the mouse event but don't close
        MEVENT event;
        getmouse(&event);
        return;
    }

    // Close help on any key except escape (might be start of escape sequence)
    switch (ch) {
        case 27:  // Escape - might be start of escape sequence, ignore
            break;
        default:
            // Any other key closes help
            show_help_ = false;
            break;
    }
}

} // namespace pex
