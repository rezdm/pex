#include "imgui_app.hpp"
#include "imgui.h"
#include <algorithm>
#include <functional>

namespace pex {

namespace {

std::string to_lower_copy(const std::string& s) {
    std::string result = s;
    std::ranges::transform(result, result.begin(),
                           [](unsigned char c) { return std::tolower(c); });
    return result;
}

// Case-insensitive match against process name or command line.
// search_lower must already be lowercased.
bool matches_search_term(const ProcessInfo& info, const std::string& search_lower) {
    return to_lower_copy(info.name).find(search_lower) != std::string::npos ||
           to_lower_copy(info.command_line).find(search_lower) != std::string::npos;
}

} // namespace

void ImGuiApp::collect_visible_items(ProcessNode* node, std::vector<ProcessNode*>& items,
                                     const bool show_kernel_threads) {
    if (!show_kernel_threads && node->info.is_kernel_thread) return;
    items.push_back(node);
    if (node->is_expanded) {
        for (auto& child : node->children) {
            collect_visible_items(child.get(), items, show_kernel_threads);
        }
    }
}

std::vector<ProcessNode*> ImGuiApp::get_visible_items() const {
    std::vector<ProcessNode*> items;
    if (!current_data_) return items;

    const bool show_kernel = view_model_.process_list.show_kernel_threads;
    if (view_model_.process_list.is_tree_view) {
        for (auto& root : current_data_->process_tree) {
            collect_visible_items(root.get(), items, show_kernel);
        }
    } else {
        std::function<void(ProcessNode*)> flatten;
        flatten = [&](ProcessNode* node) {
            if (!show_kernel && node->info.is_kernel_thread) return;
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

    // Ctrl+Shift+F opens the find-open-file dialog (issue #7)
    if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_F)) {
        find_dialog_visible_ = true;
        find_focus_input_ = true;
        return;
    }

    // Ctrl+F to focus search box
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) {
        pl.focus_search_box = true;
        return;
    }

    // F5 for refresh
    if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
        details_force_refresh_ = true;
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

    const std::string search_lower = to_lower_copy(pl.search_buffer);

    // Search ALL processes (depth-first), regardless of expansion state,
    // so matches under collapsed parents can still be found. Hidden kernel
    // threads are excluded so search cannot land on an invisible row.
    const bool show_kernel = view_model_.process_list.show_kernel_threads;
    std::function<void(ProcessNode*)> visit = [&](ProcessNode* node) {
        if (!show_kernel && node->info.is_kernel_thread) return;
        if (matches_search_term(node->info, search_lower)) {
            matches.push_back(node);
        }
        for (auto& child : node->children) {
            visit(child.get());
        }
    };
    for (auto& root : current_data_->process_tree) {
        visit(root.get());
    }
    return matches;
}

bool ImGuiApp::current_selection_matches() const {
    const auto& pl = view_model_.process_list;
    if (!current_data_ || pl.search_buffer[0] == '\0' || pl.selected_pid <= 0) return false;

    const auto it = current_data_->process_map.find(pl.selected_pid);
    if (it == current_data_->process_map.end()) return false;

    return matches_search_term(it->second->info, to_lower_copy(pl.search_buffer));
}

void ImGuiApp::expand_ancestors(int pid) {
    if (!current_data_) return;
    auto& collapsed = view_model_.process_list.collapsed_pids;

    int current = pid;
    for (int hops = 0; hops < 128; ++hops) {  // Guard against parent_pid cycles
        const auto it = current_data_->process_map.find(current);
        if (it == current_data_->process_map.end()) break;
        const int parent = it->second->info.parent_pid;
        if (parent == current) break;
        const auto parent_it = current_data_->process_map.find(parent);
        if (parent_it == current_data_->process_map.end()) break;
        collapsed.erase(parent);
        parent_it->second->is_expanded = true;  // Take effect this frame
        current = parent;
    }
}

void ImGuiApp::search_select_first() {
    auto& pl = view_model_.process_list;
    if (current_selection_matches()) return;

    const auto matches = find_matching_processes();
    if (matches.empty()) return;

    pl.selected_pid = matches[0]->info.pid;
    expand_ancestors(pl.selected_pid);
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
    expand_ancestors(pl.selected_pid);
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
    expand_ancestors(pl.selected_pid);
    pl.scroll_to_selected = true;
    refresh_selected_details();
}

} // namespace pex
