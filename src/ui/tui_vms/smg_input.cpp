#include "smg_app.hpp"
#include "smg_colors.hpp"
#include <algorithm>

// SMG$ terminator codes for special keys.
// These constants are defined in <smgdef.h> on VMS.
// We provide stubs for non-VMS compilation.
#ifdef __VMS
#define __NEW_STARLET 1
#include <smgdef.h>
#else
// Stub key codes matching VMS SMG$ terminator definitions
#define SMG$K_TRM_UP          274
#define SMG$K_TRM_DOWN        275
#define SMG$K_TRM_LEFT        276
#define SMG$K_TRM_RIGHT       277
#define SMG$K_TRM_PF1         256  // GOLD key / F1
#define SMG$K_TRM_PF2         257
#define SMG$K_TRM_PF3         258
#define SMG$K_TRM_PF4         259
#define SMG$K_TRM_KP0         260
#define SMG$K_TRM_ENTER       270
#define SMG$K_TRM_F6          281
#define SMG$K_TRM_F7          282
#define SMG$K_TRM_F8          283
#define SMG$K_TRM_F9          284
#define SMG$K_TRM_F10         285
#define SMG$K_TRM_F11         286
#define SMG$K_TRM_F12         287
#define SMG$K_TRM_F13         288
#define SMG$K_TRM_F14         289
#define SMG$K_TRM_F17         292
#define SMG$K_TRM_F18         293
#define SMG$K_TRM_F19         294
#define SMG$K_TRM_F20         295
#define SMG$K_TRM_PREV_SCREEN 298
#define SMG$K_TRM_NEXT_SCREEN 299
#define SMG$K_TRM_DELETE      127
#define SMG$K_TRM_CR          13
#define SMG$K_TRM_HT          9    // Tab
#define SMG$K_TRM_CTRLZ       26
#define SMG$K_TRM_CTRLU       21
#endif

namespace pex {

void SmgApp::handle_input(unsigned int key_code) {
    if (terminal_too_small_) {
        // Allow quit and panel toggles
        if (key_code == 'q' || key_code == 'Q' || key_code == SMG$K_TRM_CTRLZ) {
            running_ = false;
        } else if (key_code == 's') {
            view_model_.system_panel.is_visible = !view_model_.system_panel.is_visible;
            resize_displays();
        } else if (key_code == 'c') {
            if (system_panel_expanded_) {
                system_panel_expanded_ = false;
                resize_displays();
            }
        }
        return;
    }

    // Debounce
    if (dialog_debounce_ > 0) {
        dialog_debounce_--;
        return;
    }

    // Help overlay takes priority
    if (show_help_) {
        handle_help_input(key_code);
        return;
    }

    // Kill dialog takes priority
    if (view_model_.kill_dialog.is_visible) {
        handle_kill_dialog_input(key_code);
        return;
    }

    // Search mode takes priority
    if (search_mode_) {
        handle_search_input(key_code);
        return;
    }

    // Global keys
    switch (key_code) {
        case 'q':
        case 'Q':
        case SMG$K_TRM_CTRLZ:  // Ctrl+Z = quit on VMS (like Ctrl+C on Unix)
            running_ = false;
            return;

        case '?':
        case SMG$K_TRM_PF1:    // PF1/F1 = help
            show_help_ = true;
            dialog_debounce_ = 5;
            return;

        case '/':
            search_mode_ = true;
            search_input_.clear();
            return;

        case 'n':
            if (!view_model_.process_list.search_text.empty()) {
                search_next();
            }
            return;

        case 'N':
            if (!view_model_.process_list.search_text.empty()) {
                search_previous();
            }
            return;

        case 's':
            view_model_.system_panel.is_visible = !view_model_.system_panel.is_visible;
            resize_displays();
            return;

        case 'c':
            if (view_model_.system_panel.is_visible) {
                system_panel_expanded_ = !system_panel_expanded_;
                resize_displays();
            }
            return;

        case SMG$K_TRM_HT:  // Tab - switch panel focus
            if (current_focus_ == SmgPanelFocus::ProcessList) {
                current_focus_ = SmgPanelFocus::DetailsPanel;
            } else {
                current_focus_ = SmgPanelFocus::ProcessList;
            }
            return;

        case 'r':
        case SMG$K_TRM_F17:  // F17 = F5 equivalent on VMS LK201 keyboard
            data_store_->refresh_now();
            details_needs_refresh_ = true;
            return;

        case 27:  // Escape - clear search
            view_model_.process_list.search_text.clear();
            return;

        default:
            break;
    }

    // Panel-specific input
    if (current_focus_ == SmgPanelFocus::ProcessList) {
        handle_process_list_input(key_code);
    } else {
        handle_details_panel_input(key_code);
    }
}

void SmgApp::handle_process_list_input(unsigned int key_code) {
    auto& pl = view_model_.process_list;

    switch (key_code) {
        case SMG$K_TRM_UP:
        case 'k':
            move_selection(-1);
            break;

        case SMG$K_TRM_DOWN:
        case 'j':
            move_selection(1);
            break;

        case SMG$K_TRM_PREV_SCREEN:
            page_up();
            break;

        case SMG$K_TRM_NEXT_SCREEN:
            page_down();
            break;

        case 'g':
            {
                const auto visible_items = get_visible_items();
                if (!visible_items.empty()) {
                    pl.selected_pid = visible_items.front()->info.pid;
                    process_scroll_offset_ = 0;
                }
            }
            break;

        case 'G':
            {
                const auto visible_items = get_visible_items();
                if (!visible_items.empty()) {
                    pl.selected_pid = visible_items.back()->info.pid;
                    scroll_to_selection();
                }
            }
            break;

        case SMG$K_TRM_RIGHT:
        case SMG$K_TRM_CR:
        case SMG$K_TRM_ENTER:
            // Expand node
            if (pl.selected_pid > 0) {
                pl.collapsed_pids.erase(pl.selected_pid);
            }
            break;

        case SMG$K_TRM_LEFT:
            // Collapse node
            if (pl.selected_pid > 0 && current_data_ &&
                current_data_->process_map.count(pl.selected_pid) > 0) {
                const auto* node = current_data_->process_map.at(pl.selected_pid);
                if (!node->children.empty()) {
                    pl.collapsed_pids.insert(pl.selected_pid);
                } else {
                    const int parent_pid = node->info.parent_pid;
                    if (current_data_->process_map.count(parent_pid) > 0) {
                        pl.selected_pid = parent_pid;
                        scroll_to_selection();
                    }
                }
            }
            break;

        case '>':
        case '.':
            process_h_scroll_ += 10;
            break;

        case '<':
        case ',':
            process_h_scroll_ = std::max(0, process_h_scroll_ - 10);
            break;

        case '0':
            process_h_scroll_ = 0;
            break;

        case 'K':  // Kill process tree
            if (pl.selected_pid > 0 && current_data_ &&
                current_data_->process_map.count(pl.selected_pid) > 0) {
                const auto* node = current_data_->process_map.at(pl.selected_pid);
                request_kill_process(pl.selected_pid, node->info.name, true);
            }
            break;

        case 'x':  // Kill process (single)
            if (pl.selected_pid > 0 && current_data_ &&
                current_data_->process_map.count(pl.selected_pid) > 0) {
                const auto* node = current_data_->process_map.at(pl.selected_pid);
                request_kill_process(pl.selected_pid, node->info.name, false);
            }
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

        // Tab switching by letter
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

        default:
            break;
    }
}

void SmgApp::handle_details_panel_input(unsigned int key_code) {
    switch (key_code) {
        case SMG$K_TRM_UP:
        case 'k':
            if (details_scroll_offset_ > 0) {
                details_scroll_offset_--;
            }
            break;

        case SMG$K_TRM_DOWN:
        case 'j':
            details_scroll_offset_++;
            break;

        case SMG$K_TRM_PREV_SCREEN:
            details_scroll_offset_ = std::max(0, details_scroll_offset_ - visible_details_rows_);
            break;

        case SMG$K_TRM_NEXT_SCREEN:
            details_scroll_offset_ += visible_details_rows_;
            break;

        case 'g':
            details_scroll_offset_ = 0;
            break;

        case SMG$K_TRM_LEFT:
            prev_tab();
            break;

        case SMG$K_TRM_RIGHT:
            next_tab();
            break;

        // Tab switching shortcuts (same as process list)
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

        default:
            break;
    }
}

void SmgApp::handle_search_input(unsigned int key_code) {
    switch (key_code) {
        case 27:  // Escape
            search_mode_ = false;
            break;

        case SMG$K_TRM_CR:
        case SMG$K_TRM_ENTER:
            // Commit search
            search_mode_ = false;
            view_model_.process_list.search_text = search_input_;
            search_select_first();
            break;

        case SMG$K_TRM_DELETE:  // DEL (127)
        case 8:   // Backspace
            if (!search_input_.empty()) {
                search_input_.pop_back();
            }
            break;

        case SMG$K_TRM_CTRLU:
            // Clear search input
            search_input_.clear();
            break;

        default:
            // Add printable characters
            if (key_code >= 32 && key_code < 127) {
                search_input_ += static_cast<char>(key_code);
            }
            break;
    }
}

void SmgApp::handle_kill_dialog_input(unsigned int key_code) {
    auto& kd = view_model_.kill_dialog;

    switch (key_code) {
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

        default:
            break;
    }
}

void SmgApp::handle_help_input(unsigned int key_code) {
    // Close help on any key except escape sequences
    if (key_code != 27) {
        show_help_ = false;
    }
}

} // namespace pex
