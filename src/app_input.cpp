#include "app.hpp"
#include "imgui.h"
#include <algorithm>
#include <functional>

namespace pex {

void App::collect_visible_items(ProcessNode* node, std::vector<ProcessNode*>& items) {
    items.push_back(node);
    if (node->is_expanded) {
        for (auto& child : node->children) {
            collect_visible_items(child.get(), items);
        }
    }
}

std::vector<ProcessNode*> App::get_visible_items() const {
    std::vector<ProcessNode*> items;
    if (!current_data_) return items;

    if (is_tree_view_) {
        // For tree view, only collect expanded items
        for (auto& root : current_data_->process_tree) {
            collect_visible_items(root.get(), items);
        }
    } else {
        // For flat list, collect all items
        std::function<void(ProcessNode*)> flatten;
        flatten = [&](ProcessNode* node) {
            items.push_back(node);
            for (auto& child : node->children) {
                flatten(child.get());
            }
        };
        for (auto& root : current_data_->process_tree) {
            flatten(root.get());
        }
    }
    return items;
}

void App::handle_keyboard_navigation() {
    // Ctrl+F to focus search box - works globally
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) {
        focus_search_box_ = true;
        return;
    }

    // F5 for refresh - works globally
    if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
        data_store_.refresh_now();
        return;
    }

    // F3 / Shift+F3 for search next/previous
    if (ImGui::IsKeyPressed(ImGuiKey_F3)) {
        if (ImGui::GetIO().KeyShift) {
            search_previous();
        } else {
            search_next();
        }
        return;
    }

    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;

    const auto visible_items = get_visible_items();
    if (visible_items.empty()) return;

    // Find current selection index by PID
    int current_idx = -1;
    for (int i = 0; i < static_cast<int>(visible_items.size()); i++) {
        if (visible_items[i]->info.pid == selected_pid_) {
            current_idx = i;
            break;
        }
    }

    int new_idx = current_idx;
    constexpr int page_size = 20; // Items per page for PageUp/PageDown

    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        new_idx = (current_idx < 0) ? 0 : std::min(current_idx + 1, static_cast<int>(visible_items.size()) - 1);
    } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        new_idx = (current_idx < 0) ? 0 : std::max(current_idx - 1, 0);
    } else if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
        new_idx = (current_idx < 0) ? 0 : std::min(current_idx + page_size, static_cast<int>(visible_items.size()) - 1);
    } else if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
        new_idx = (current_idx < 0) ? 0 : std::max(current_idx - page_size, 0);
    } else if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
        new_idx = 0;
    } else if (ImGui::IsKeyPressed(ImGuiKey_End)) {
        new_idx = static_cast<int>(visible_items.size()) - 1;
    }

    if (new_idx != current_idx && new_idx >= 0 && new_idx < static_cast<int>(visible_items.size())) {
        selected_pid_ = visible_items[new_idx]->info.pid;
        refresh_selected_details();
    }
}

std::vector<ProcessNode*> App::find_matching_processes() const {
    std::vector<ProcessNode*> matches;
    if (!current_data_ || search_buffer_[0] == '\0') return matches;

    std::string search_lower = search_buffer_;
    std::ranges::transform(search_lower, search_lower.begin(), ::tolower);

    const auto visible = get_visible_items();
    for (auto* node : visible) {
        std::string name_lower = node->info.name;
        std::ranges::transform(name_lower, name_lower.begin(), ::tolower);
        if (name_lower.find(search_lower) != std::string::npos) {
            matches.push_back(node);
        }
    }
    return matches;
}

bool App::current_selection_matches() const {
    if (!current_data_ || search_buffer_[0] == '\0' || selected_pid_ <= 0) return false;

    const auto it = current_data_->process_map.find(selected_pid_);
    if (it == current_data_->process_map.end()) return false;

    std::string search_lower = search_buffer_;
    std::ranges::transform(search_lower, search_lower.begin(), ::tolower);

    std::string name_lower = it->second->info.name;
    std::ranges::transform(name_lower, name_lower.begin(), ::tolower);

    return name_lower.find(search_lower) != std::string::npos;
}

void App::search_select_first() {
    // If current selection already matches, don't change it
    if (current_selection_matches()) return;

    const auto matches = find_matching_processes();
    if (matches.empty()) return;

    // Select first match
    selected_pid_ = matches[0]->info.pid;
    scroll_to_selected_ = true;
    refresh_selected_details();
}

void App::search_next() {
    const auto matches = find_matching_processes();
    if (matches.empty()) return;

    // Find current selection in matches
    int current_match_idx = -1;
    for (int i = 0; i < static_cast<int>(matches.size()); i++) {
        if (matches[i]->info.pid == selected_pid_) {
            current_match_idx = i;
            break;
        }
    }

    // Select next match (or first if none selected)
    const int next_idx = (current_match_idx + 1) % static_cast<int>(matches.size());
    selected_pid_ = matches[next_idx]->info.pid;
    scroll_to_selected_ = true;
    refresh_selected_details();
}

void App::search_previous() {
    const auto matches = find_matching_processes();
    if (matches.empty()) return;

    // Find current selection in matches
    int current_match_idx = -1;
    for (int i = 0; i < static_cast<int>(matches.size()); i++) {
        if (matches[i]->info.pid == selected_pid_) {
            current_match_idx = i;
            break;
        }
    }

    // Select previous match (or last if none selected)
    int prev_idx;
    if (current_match_idx <= 0) {
        prev_idx = static_cast<int>(matches.size()) - 1;
    } else {
        prev_idx = current_match_idx - 1;
    }
    selected_pid_ = matches[prev_idx]->info.pid;
    scroll_to_selected_ = true;
    refresh_selected_details();
}

} // namespace pex
