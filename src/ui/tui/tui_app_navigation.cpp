#include "tui_app.hpp"
#include <algorithm>

namespace pex {

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
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c) { return std::tolower(c); });
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
    tab = (tab + 1) % kTabCount;
    view_model_.details_panel.active_tab = static_cast<DetailsTab>(tab);
    details_scroll_offset_ = 0;
}

void TuiApp::prev_tab() {
    int tab = static_cast<int>(view_model_.details_panel.active_tab);
    tab = (tab + kTabCount - 1) % kTabCount;
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
