#include "app.hpp"
#include "imgui.h"
#include <format>
#include <functional>
#include <algorithm>

namespace pex {

// Helper function to get color for process state
static ImVec4 get_state_color(const char state) {
    switch (state) {
        case 'R': return ImVec4(0.2f, 0.9f, 0.2f, 1.0f);  // Green - Running
        case 'D': return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // Red - Disk sleep
        case 'Z': return ImVec4(0.8f, 0.3f, 0.8f, 1.0f);  // Purple - Zombie
        case 'T': case 't': return ImVec4(1.0f, 0.9f, 0.2f, 1.0f);  // Yellow - Stopped
        default:  return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);  // Gray - Sleeping/Idle
    }
}

// Column tooltips descriptions
static constexpr const char* kColumnTooltips[] = {
    "Process name",                                    // 0: Process
    "Process ID",                                      // 1: PID
    "CPU usage per core (100% = 1 core)",             // 2: CPU %
    "CPU usage of total system (100% = all cores)",  // 3: Total %
    "Resident memory (RSS)",                          // 4: Memory
    "Percentage of total system memory",              // 5: Mem %
    "Sum of CPU% for process and all descendants",   // 6: Tree CPU
    "Sum of Total% for process and all descendants", // 7: Tree Tot
    "Sum of memory for process and all descendants", // 8: Tree Mem
    "Sum of memory% for process and all descendants",// 9: Tree %
    "Number of threads",                              // 10: Threads
    "Owner username",                                 // 11: User
    "R=Running, S=Sleeping, D=Disk, Z=Zombie, T=Stopped", // 12: State
    "Full path to executable",                        // 13: Executable
    "Full command line with arguments"               // 14: Command Line
};

// Helper to show tooltips for column headers
static void show_column_tooltips() {
    for (int col = 0; col < 15; col++) {
        if (ImGui::TableSetColumnIndex(col)) {
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", kColumnTooltips[col]);
            }
        }
    }
}

void App::render_process_tree() {
    if (!current_data_) return;

    // Column headers (no sorting in tree view - hierarchy is preserved)
    if (ImGui::BeginTable("ProcessTree", 15,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
            ImGuiTableFlags_Hideable |
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

        // Show tooltips for column headers
        show_column_tooltips();

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
    // Handle double-click to open popup
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        popup_pid_ = node.info.pid;
        show_process_popup_ = true;
        popup_cpu_user_history_.clear();
        popup_cpu_kernel_history_.clear();
        popup_memory_history_.clear();
        popup_per_cpu_user_history_.clear();
        popup_per_cpu_kernel_history_.clear();
        popup_prev_utime_ = 0;
        popup_prev_stime_ = 0;
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
    ImGui::TextColored(get_state_color(node.info.state_char), "%c", node.info.state_char);

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
        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 70);
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

        // Show tooltips for column headers
        show_column_tooltips();

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

        // Handle sorting
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                const auto& spec = sort_specs->Specs[0];
                list_sort_col_ = spec.ColumnIndex;
                list_sort_asc_ = (spec.SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
            }
        }
        // Always apply sort
        if (!flat_list.empty()) {
            const int column = list_sort_col_;
            const bool ascending = list_sort_asc_;
            std::ranges::sort(flat_list, [column, ascending](const ProcessNode* a, const ProcessNode* b) {
                int result = 0;
                switch (column) {
                    case 0: result = a->info.name.compare(b->info.name); break;
                    case 1: result = a->info.pid - b->info.pid; break;
                    case 2: result = (a->info.cpu_percent < b->info.cpu_percent) ? -1 : (a->info.cpu_percent > b->info.cpu_percent) ? 1 : 0; break;
                    case 3: result = (a->info.total_cpu_percent < b->info.total_cpu_percent) ? -1 : (a->info.total_cpu_percent > b->info.total_cpu_percent) ? 1 : 0; break;
                    case 4: result = (a->info.resident_memory < b->info.resident_memory) ? -1 : (a->info.resident_memory > b->info.resident_memory) ? 1 : 0; break;
                    case 5: result = (a->info.memory_percent < b->info.memory_percent) ? -1 : (a->info.memory_percent > b->info.memory_percent) ? 1 : 0; break;
                    case 6: result = (a->tree_cpu_percent < b->tree_cpu_percent) ? -1 : (a->tree_cpu_percent > b->tree_cpu_percent) ? 1 : 0; break;
                    case 7: result = (a->tree_total_cpu_percent < b->tree_total_cpu_percent) ? -1 : (a->tree_total_cpu_percent > b->tree_total_cpu_percent) ? 1 : 0; break;
                    case 8: result = (a->tree_working_set < b->tree_working_set) ? -1 : (a->tree_working_set > b->tree_working_set) ? 1 : 0; break;
                    case 9: result = (a->tree_memory_percent < b->tree_memory_percent) ? -1 : (a->tree_memory_percent > b->tree_memory_percent) ? 1 : 0; break;
                    case 10: result = a->info.thread_count - b->info.thread_count; break;
                    case 11: result = a->info.user_name.compare(b->info.user_name); break;
                    case 12: result = a->info.state_char - b->info.state_char; break;
                    case 13: result = a->info.executable_path.compare(b->info.executable_path); break;
                    case 14: result = a->info.command_line.compare(b->info.command_line); break;
                    default: result = 0; break;
                }
                return ascending ? (result < 0) : (result > 0);
            });
        }

        for (const auto* node : flat_list) {
            ImGui::PushID(node->info.pid);
            ImGui::TableNextRow();

            // Set row background for selected item
            if ((node->info.pid == selected_pid_)) {
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
            ImGui::TextColored(get_state_color(node->info.state_char), "%c", node->info.state_char);

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

            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
                const ImVec2 mouse_pos = ImGui::GetMousePos();
                if (mouse_pos.x >= row_min.x && mouse_pos.x <= row_max.x &&
                    mouse_pos.y >= row_min.y && mouse_pos.y <= row_max.y) {
                    if (ImGui::IsMouseClicked(0)) {
                        selected_pid_ = node->info.pid;
                        refresh_selected_details();
                    }
                    // Handle double-click to open popup
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        popup_pid_ = node->info.pid;
                        show_process_popup_ = true;
                        popup_cpu_user_history_.clear();
                        popup_cpu_kernel_history_.clear();
                        popup_memory_history_.clear();
                        popup_per_cpu_user_history_.clear();
                        popup_per_cpu_kernel_history_.clear();
                        popup_prev_utime_ = 0;
                        popup_prev_stime_ = 0;
                    }
                }
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

} // namespace pex
