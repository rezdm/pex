#include "app.hpp"
#include "imgui.h"
#include <algorithm>
#include <functional>

namespace pex {

void App::handle_search_input() {
    ImGuiIO& io = ImGui::GetIO();

    // Only handle when this window is focused
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;

    for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
        ImWchar c = io.InputQueueCharacters[i];
        if (c >= 'a' && c <= 'z') {
            search_text_ += static_cast<char>(c);
            last_key_time_ = std::chrono::steady_clock::now();
        } else if (c >= 'A' && c <= 'Z') {
            search_text_ += static_cast<char>(c - 'A' + 'a');
            last_key_time_ = std::chrono::steady_clock::now();
        } else if (c >= '0' && c <= '9') {
            search_text_ += static_cast<char>(c);
            last_key_time_ = std::chrono::steady_clock::now();
        } else if (c == '-' || c == '_' || c == '.') {
            search_text_ += static_cast<char>(c);
            last_key_time_ = std::chrono::steady_clock::now();
        }
    }

    // If we have search text, and it changed, find match
    if (!search_text_.empty()) {
        // Check if current selection still matches
        if (selected_process_) {
            std::string name_lower = selected_process_->info.name;
            std::ranges::transform(name_lower, name_lower.begin(), ::tolower);
            if (name_lower.starts_with(search_text_)) {
                return; // Current selection still matches
            }
        }

        // Search for match
        ProcessNode* match = nullptr;
        if (is_tree_view_) {
            match = find_matching_process(search_text_, selected_process_);
        } else {
            match = search_subtree(process_tree_, search_text_);
        }

        if (match) {
            selected_process_ = match;
            selected_pid_ = match->info.pid;
            refresh_selected_details();
        }
    }
}

ProcessNode* App::find_matching_process(const std::string& search, ProcessNode* start_node) {
    // First search in current selection's children
    if (start_node) {
        auto match = search_subtree(start_node->children, search);
        if (match) return match;
    }

    // Then search from top
    return search_subtree(process_tree_, search);
}

ProcessNode* App::search_subtree(std::vector<std::unique_ptr<ProcessNode>>& nodes, const std::string& search) {
    for (auto& node : nodes) {
        std::string name_lower = node->info.name;
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
        if (name_lower.starts_with(search)) {
            return node.get();
        }

        auto match = search_subtree(node->children, search);
        if (match) return match;
    }
    return nullptr;
}

void App::collect_visible_items(ProcessNode* node, std::vector<ProcessNode*>& items) {
    items.push_back(node);
    if (node->is_expanded) {
        for (auto& child : node->children) {
            collect_visible_items(child.get(), items);
        }
    }
}

std::vector<ProcessNode*> App::get_visible_items() {
    std::vector<ProcessNode*> items;
    if (is_tree_view_) {
        // For tree view, only collect expanded items
        for (auto& root : process_tree_) {
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
        for (auto& root : process_tree_) {
            flatten(root.get());
        }
    }
    return items;
}

void App::handle_keyboard_navigation() {
    // F5 for refresh - works globally
    if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
        refresh_processes();
        return;
    }

    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;

    auto visible_items = get_visible_items();
    if (visible_items.empty()) return;

    // Find current selection index
    int current_idx = -1;
    for (int i = 0; i < static_cast<int>(visible_items.size()); i++) {
        if (visible_items[i] == selected_process_) {
            current_idx = i;
            break;
        }
    }

    int new_idx = current_idx;
    int page_size = 20; // Items per page for PageUp/PageDown

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
        selected_process_ = visible_items[new_idx];
        selected_pid_ = selected_process_->info.pid;
        refresh_selected_details();
    }
}

} // namespace pex
