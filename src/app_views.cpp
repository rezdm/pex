#include "app.hpp"
#include "imgui.h"
#include <format>
#include <functional>

namespace pex {

void App::render_process_tree() {
    if (!current_data_) return;

    // Column headers
    if (ImGui::BeginTable("ProcessTree", 15,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
            ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Process", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthFixed, 200);
        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("CPU %", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Total %", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Memory", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Mem %", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Tree CPU", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Tree Tot", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Tree Mem", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Tree %", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Threads", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("User", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Executable", ImGuiTableColumnFlags_WidthFixed, 200);
        ImGui::TableSetupColumn("Command Line", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (auto& root : current_data_->process_tree) {
            render_process_tree_node(*root, 0);
        }

        ImGui::EndTable();
    }
}

void App::render_process_tree_node(ProcessNode& node, const int depth) {
    ImGui::PushID(node.info.pid);
    ImGui::TableNextRow();

    const bool is_selected = (node.info.pid == selected_pid_);

    // Set row background for selected item
    if (is_selected) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
            ImGui::GetColorU32(ImVec4(0.3f, 0.5f, 0.8f, 0.5f)));
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
            ImGui::GetColorU32(ImVec4(0.3f, 0.5f, 0.8f, 0.5f)));
        // Scroll to selected item if requested
        if (scroll_to_selected_) {
            ImGui::SetScrollHereY(0.5f);
            scroll_to_selected_ = false;
        }
    }

    ImGui::TableNextColumn();

    // Tree node with process name - don't use built-in selection highlight
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow;
    if (node.children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (node.is_expanded) {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    // Use PID as unique ID to avoid collisions with same-named processes
    const std::string label = std::format("{}##{}", node.info.name, node.info.pid);
    const bool is_open = ImGui::TreeNodeEx(label.c_str(), flags);

    // Handle click on tree node
    if (ImGui::IsItemClicked()) {
        selected_pid_ = node.info.pid;
        refresh_selected_details();
    }

    // Store row bounds for click detection on other columns
    const ImVec2 row_min = ImGui::GetItemRectMin();
    const ImVec2 row_max = ImGui::GetItemRectMax();
    const float row_y_min = row_min.y;
    const float row_y_max = row_max.y;

    // Other columns
    ImGui::TableNextColumn();
    ImGui::Text("%d", node.info.pid);

    ImGui::TableNextColumn();
    ImGui::Text("%.1f", node.info.cpu_percent);

    ImGui::TableNextColumn();
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 0.6f, 1.0f), "%.2f", node.info.total_cpu_percent);

    ImGui::TableNextColumn();
    ImGui::Text("%s", format_bytes(node.info.resident_memory).c_str());

    ImGui::TableNextColumn();
    ImGui::Text("%.1f", node.info.memory_percent);

    ImGui::TableNextColumn();
    ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "%.1f", node.tree_cpu_percent);

    ImGui::TableNextColumn();
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 0.6f, 1.0f), "%.2f", node.tree_total_cpu_percent);

    ImGui::TableNextColumn();
    ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "%s", format_bytes(node.tree_working_set).c_str());

    ImGui::TableNextColumn();
    ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "%.1f", node.tree_memory_percent);

    ImGui::TableNextColumn();
    ImGui::Text("%d", node.info.thread_count);

    ImGui::TableNextColumn();
    ImGui::Text("%s", node.info.user_name.c_str());

    ImGui::TableNextColumn();
    ImGui::Text("%c", node.info.state_char);

    ImGui::TableNextColumn();
    ImGui::Text("%s", node.info.executable_path.c_str());

    ImGui::TableNextColumn();
    ImGui::Text("%s", node.info.command_line.c_str());

    // Handle row click - only if click is within the table (not on toolbar buttons, etc.)
    if (ImGui::IsMouseClicked(0) && !ImGui::IsItemClicked() && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        const ImVec2 mouse_pos = ImGui::GetMousePos();
        const float win_x = ImGui::GetWindowPos().x;
        if (const float win_w = ImGui::GetWindowWidth(); mouse_pos.x >= win_x && mouse_pos.x <= win_x + win_w &&
                                                         mouse_pos.y >= row_y_min && mouse_pos.y <= row_y_max) {
            selected_pid_ = node.info.pid;
            refresh_selected_details();
        }
    }

    // Render children if expanded
    if (is_open && !node.children.empty()) {
        // Update UI state: this node is expanded
        collapsed_pids_.erase(node.info.pid);
        for (auto& child : node.children) {
            render_process_tree_node(*child, depth + 1);
        }
        ImGui::TreePop();
    } else if (!node.children.empty()) {
        // Update UI state: this node is collapsed
        collapsed_pids_.insert(node.info.pid);
    }

    ImGui::PopID();
}

void App::render_process_list() {
    if (!current_data_) return;

    if (ImGui::BeginTable("ProcessList", 15,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
            ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable |
            ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Process", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthFixed, 200);
        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("CPU %", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Total %", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Memory", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Mem %", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Tree CPU", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Tree Tot", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Tree Mem", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Tree %", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Threads", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("User", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Executable", ImGuiTableColumnFlags_WidthFixed, 200);
        ImGui::TableSetupColumn("Command Line", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        // Flatten tree for list view
        std::vector<ProcessNode*> flat_list;
        std::function<void(ProcessNode*)> flatten;
        flatten = [&](ProcessNode* node) {
            flat_list.push_back(node);
            for (auto& child : node->children) {
                flatten(child.get());
            }
        };
        for (auto& root : current_data_->process_tree) {
            flatten(root.get());
        }

        for (const auto* node : flat_list) {
            ImGui::PushID(node->info.pid);
            ImGui::TableNextRow();

            const bool is_selected = (node->info.pid == selected_pid_);

            // Set row background for selected item
            if (is_selected) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                    ImGui::GetColorU32(ImVec4(0.3f, 0.5f, 0.8f, 0.5f)));
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                    ImGui::GetColorU32(ImVec4(0.3f, 0.5f, 0.8f, 0.5f)));
                // Scroll to selected item if requested
                if (scroll_to_selected_) {
                    ImGui::SetScrollHereY(0.5f);
                    scroll_to_selected_ = false;
                }
            }

            ImGui::TableNextColumn();
            ImGui::Text("%s", node->info.name.c_str());

            ImGui::TableNextColumn();
            ImGui::Text("%d", node->info.pid);

            ImGui::TableNextColumn();
            ImGui::Text("%.1f", node->info.cpu_percent);

            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 0.6f, 1.0f), "%.2f", node->info.total_cpu_percent);

            ImGui::TableNextColumn();
            ImGui::Text("%s", format_bytes(node->info.resident_memory).c_str());

            ImGui::TableNextColumn();
            ImGui::Text("%.1f", node->info.memory_percent);

            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "%.1f", node->tree_cpu_percent);

            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 0.6f, 1.0f), "%.2f", node->tree_total_cpu_percent);

            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "%s", format_bytes(node->tree_working_set).c_str());

            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "%.1f", node->tree_memory_percent);

            ImGui::TableNextColumn();
            ImGui::Text("%d", node->info.thread_count);

            ImGui::TableNextColumn();
            ImGui::Text("%s", node->info.user_name.c_str());

            ImGui::TableNextColumn();
            ImGui::Text("%c", node->info.state_char);

            ImGui::TableNextColumn();
            ImGui::Text("%s", node->info.executable_path.c_str());

            ImGui::TableNextColumn();
            ImGui::Text("%s", node->info.command_line.c_str());

            // Handle row click - check if any column in this row was clicked
            ImGui::TableSetColumnIndex(0);
            ImVec2 row_min = ImGui::GetItemRectMin();
            row_min.x = ImGui::GetWindowPos().x;
            ImVec2 row_max = ImGui::GetItemRectMax();
            row_max.x = row_min.x + ImGui::GetWindowWidth();

            if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
                const ImVec2 mouse_pos = ImGui::GetMousePos();
                if (mouse_pos.x >= row_min.x && mouse_pos.x <= row_max.x &&
                    mouse_pos.y >= row_min.y && mouse_pos.y <= row_max.y) {
                    selected_pid_ = node->info.pid;
                    refresh_selected_details();
                }
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

} // namespace pex
