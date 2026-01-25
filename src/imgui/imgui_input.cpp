#include "imgui_app.hpp"
#include "imgui.h"
#include <algorithm>
#include <functional>

namespace pex {

void ImGuiApp::collect_visible_items(ProcessNode* node, std::vector<ProcessNode*>& items) {
    items.push_back(node);
    if (node->is_expanded) {
        for (auto& child : node->children) {
            collect_visible_items(child.get(), items);
        }
    }
}

std::vector<ProcessNode*> ImGuiApp::get_visible_items() const {
    std::vector<ProcessNode*> items;
    if (!current_data_) return items;

    if (view_model_.process_list.is_tree_view) {
        for (auto& root : current_data_->process_tree) {
            collect_visible_items(root.get(), items);
        }
    } else {
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

void ImGuiApp::handle_keyboard_navigation() {
    auto& pl = view_model_.process_list;

    // Ctrl+F to focus search box
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) {
        pl.focus_search_box = true;
        return;
    }

    // F5 for refresh
    if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
        data_store_->refresh_now();
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

    int current_idx = -1;
    for (int i = 0; i < static_cast<int>(visible_items.size()); i++) {
        if (visible_items[i]->info.pid == pl.selected_pid) {
            current_idx = i;
            break;
        }
    }

    int new_idx = current_idx;
    constexpr int page_size = 20;

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
        pl.selected_pid = visible_items[new_idx]->info.pid;
        refresh_selected_details();
    }
}

std::vector<ProcessNode*> ImGuiApp::find_matching_processes() const {
    std::vector<ProcessNode*> matches;
    const auto& pl = view_model_.process_list;
    if (!current_data_ || pl.search_buffer[0] == '\0') return matches;

    std::string search_lower = pl.search_buffer;
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

bool ImGuiApp::current_selection_matches() const {
    const auto& pl = view_model_.process_list;
    if (!current_data_ || pl.search_buffer[0] == '\0' || pl.selected_pid <= 0) return false;

    const auto it = current_data_->process_map.find(pl.selected_pid);
    if (it == current_data_->process_map.end()) return false;

    std::string search_lower = pl.search_buffer;
    std::ranges::transform(search_lower, search_lower.begin(), ::tolower);

    std::string name_lower = it->second->info.name;
    std::ranges::transform(name_lower, name_lower.begin(), ::tolower);

    return name_lower.find(search_lower) != std::string::npos;
}

void ImGuiApp::search_select_first() {
    auto& pl = view_model_.process_list;
    if (current_selection_matches()) return;

    const auto matches = find_matching_processes();
    if (matches.empty()) return;

    pl.selected_pid = matches[0]->info.pid;
    pl.scroll_to_selected = true;
    refresh_selected_details();
}

void ImGuiApp::search_next() {
    auto& pl = view_model_.process_list;
    const auto matches = find_matching_processes();
    if (matches.empty()) return;

    int current_match_idx = -1;
    for (int i = 0; i < static_cast<int>(matches.size()); i++) {
        if (matches[i]->info.pid == pl.selected_pid) {
            current_match_idx = i;
            break;
        }
    }

    const int next_idx = (current_match_idx + 1) % static_cast<int>(matches.size());
    pl.selected_pid = matches[next_idx]->info.pid;
    pl.scroll_to_selected = true;
    refresh_selected_details();
}

void ImGuiApp::search_previous() {
    auto& pl = view_model_.process_list;
    const auto matches = find_matching_processes();
    if (matches.empty()) return;

    int current_match_idx = -1;
    for (int i = 0; i < static_cast<int>(matches.size()); i++) {
        if (matches[i]->info.pid == pl.selected_pid) {
            current_match_idx = i;
            break;
        }
    }

    int prev_idx;
    if (current_match_idx <= 0) {
        prev_idx = static_cast<int>(matches.size()) - 1;
    } else {
        prev_idx = current_match_idx - 1;
    }
    pl.selected_pid = matches[prev_idx]->info.pid;
    pl.scroll_to_selected = true;
    refresh_selected_details();
}

} // namespace pex
