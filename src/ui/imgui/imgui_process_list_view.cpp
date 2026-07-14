#include "imgui_app.hpp"
#include "imgui.h"
#include "../../core/format_utils.hpp"
#include <format>
#include <functional>
#include <algorithm>

namespace pex {

// Helper function to get color for process state
static ImVec4 get_state_color(const char state) {
    switch (state) {
        case 'R': return {0.2f, 0.9f, 0.2f, 1.0f};  // Green - Running
        case 'D': return {1.0f, 0.3f, 0.3f, 1.0f};  // Red - Disk sleep
        case 'Z': return {0.8f, 0.3f, 0.8f, 1.0f};  // Purple - Zombie
        case 'T': case 't': return {1.0f, 0.9f, 0.2f, 1.0f};  // Yellow - Stopped
        default:  return {0.7f, 0.7f, 0.7f, 1.0f};  // Gray - Sleeping/Idle
    }
}

// Column tooltips descriptions
static constexpr const char* kColumnTooltips[] = {
    "Process name",
    "Process ID",
    "CPU usage per core (100% = 1 core)",
    "CPU usage of total system (100% = all cores)",
    "Resident memory (RSS)",
    "Percentage of total system memory",
    "Sum of CPU% for process and all descendants",
    "Sum of Total% for process and all descendants",
    "Sum of memory for process and all descendants",
    "Sum of memory% for process and all descendants",
    "Storage read rate (bytes/sec)",
    "Storage write rate (bytes/sec)",
    "Number of threads",
    "Owner username",
    "R=Running, S=Sleeping, D=Disk, Z=Zombie, T=Stopped",
    "Full path to executable",
    "Full command line with arguments"
};

static constexpr int kColumnCount = 17;

static void show_column_tooltips() {
    for (int col = 0; col < kColumnCount; col++) {
        if (ImGui::TableSetColumnIndex(col)) {
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", kColumnTooltips[col]);
            }
        }
    }
}

// "-" for idle, otherwise e.g. "1.2 MB/s"
static std::string format_io_rate(const double rate) {
    if (rate < 1.0) return "-";
    return pex::format_bytes(static_cast<int64_t>(rate), false) + "/s";
}

void ImGuiApp::render_process_tree() {
    if (!current_data_) return;

    if (ImGui::BeginTable("ProcessTree", kColumnCount,
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
        ImGui::TableSetupColumn("Read/s", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Write/s", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Threads", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("User", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Executable", ImGuiTableColumnFlags_WidthFixed, 200);
        ImGui::TableSetupColumn("Command Line", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        show_column_tooltips();

        // Exited since the previous snapshot (issue #61): red ghost rows
        // rendered *in place* — right below their still-living parent, where
        // the process's row was a tick ago (Process Explorer behavior).
        // Ghosts whose parent died with them fall back to top level.
        std::unordered_map<int, std::vector<const ProcessInfo*>> ghost_children;
        std::vector<const ProcessInfo*> orphan_ghosts;
        for (const auto& info : snapshot_diff_.exited_processes) {
            if (!view_model_.process_list.show_kernel_threads && info.is_kernel_thread) continue;
            if (current_data_->process_map.contains(info.parent_pid)) {
                ghost_children[info.parent_pid].push_back(&info);
            } else {
                orphan_ghosts.push_back(&info);
            }
        }

        for (auto& root : current_data_->process_tree) {
            render_process_tree_node(*root, 0, ghost_children);
        }

        for (const ProcessInfo* info : orphan_ghosts) {
            render_ghost_row(*info);
        }

        ImGui::EndTable();
    }
}

void ImGuiApp::render_ghost_children(const int parent_pid,
        const std::unordered_map<int, std::vector<const ProcessInfo*>>& ghost_children) {
    if (const auto it = ghost_children.find(parent_pid); it != ghost_children.end()) {
        for (const ProcessInfo* info : it->second) {
            render_ghost_row(*info);
        }
    }
}

// A red one-tick row for a process that exited since the previous snapshot
// (issue #61). Rendered from a copied ProcessInfo — the live node is gone —
// so rate columns are blank and the row is not selectable.
void ImGuiApp::render_ghost_row(const ProcessInfo& info) {
    // Offset the ID space: the PID may already be recycled by a live row
    ImGui::PushID(-info.pid - 1);
    ImGui::TableNextRow();
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
        ImGui::GetColorU32(ImVec4(0.55f, 0.1f, 0.1f, 0.5f)));
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
        ImGui::GetColorU32(ImVec4(0.55f, 0.1f, 0.1f, 0.5f)));

    ImGui::TableNextColumn();
    ImGui::Text("%s", info.name.c_str());
    ImGui::TableNextColumn();
    ImGui::Text("%d", info.pid);
    for (int i = 0; i < 2; i++) {  // CPU %, Total %
        ImGui::TableNextColumn();
        ImGui::TextDisabled("-");
    }
    ImGui::TableNextColumn();
    ImGui::Text("%s", format_bytes(info.resident_memory).c_str());
    ImGui::TableNextColumn();
    ImGui::Text("%.1f", info.memory_percent);
    for (int i = 0; i < 6; i++) {  // Tree CPU/Tot/Mem/%, Read/s, Write/s
        ImGui::TableNextColumn();
        ImGui::TextDisabled("-");
    }
    ImGui::TableNextColumn();
    ImGui::Text("%d", info.thread_count);
    ImGui::TableNextColumn();
    ImGui::Text("%s", info.user_name.c_str());
    ImGui::TableNextColumn();
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "X");
    ImGui::TableNextColumn();
    ImGui::Text("%s", info.executable_path.c_str());
    ImGui::TableNextColumn();
    ImGui::Text("%s", info.command_line.c_str());

    ImGui::PopID();
}

void ImGuiApp::render_process_tree_node(ProcessNode& node, const int depth,
        const std::unordered_map<int, std::vector<const ProcessInfo*>>& ghost_children) {
    // Kernel threads hidden: skip the node and its (kernel) subtree
    if (!view_model_.process_list.show_kernel_threads && node.info.is_kernel_thread) return;

    ImGui::PushID(node.info.pid);
    ImGui::TableNextRow();

    const bool is_selected = (node.info.pid == view_model_.process_list.selected_pid);

    if (is_selected) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
            ImGui::GetColorU32(ImVec4(0.3f, 0.5f, 0.8f, 0.5f)));
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
            ImGui::GetColorU32(ImVec4(0.3f, 0.5f, 0.8f, 0.5f)));
        if (view_model_.process_list.scroll_to_selected) {
            ImGui::SetScrollHereY(0.5f);
            view_model_.process_list.scroll_to_selected = false;
        }
    } else if (snapshot_diff_.new_pids.contains(node.info.pid)) {
        // New since the previous snapshot (issue #61): green for one tick
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
            ImGui::GetColorU32(ImVec4(0.1f, 0.5f, 0.1f, 0.45f)));
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
            ImGui::GetColorU32(ImVec4(0.1f, 0.5f, 0.1f, 0.45f)));
    }

    ImGui::TableNextColumn();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow;
    if (node.children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (node.is_expanded) {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    const std::string label = std::format("{}##{}", node.info.name, node.info.pid);
    const bool is_open = ImGui::TreeNodeEx(label.c_str(), flags);

    if (ImGui::IsItemClicked()) {
        view_model_.process_list.selected_pid = node.info.pid;
        refresh_selected_details();
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        view_model_.process_popup.target_pid = node.info.pid;
        view_model_.process_popup.is_visible = true;
        view_model_.process_popup.include_tree = true;
        view_model_.process_popup.clear_history();
    }

    const ImVec2 row_min = ImGui::GetItemRectMin();
    const ImVec2 row_max = ImGui::GetItemRectMax();
    const float row_y_min = row_min.y;
    const float row_y_max = row_max.y;

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
    ImGui::Text("%s", format_io_rate(node.info.io_read_rate).c_str());

    ImGui::TableNextColumn();
    ImGui::Text("%s", format_io_rate(node.info.io_write_rate).c_str());

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

    if (ImGui::IsMouseClicked(0) && !ImGui::IsItemClicked() && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        const ImVec2 mouse_pos = ImGui::GetMousePos();
        const float win_x = ImGui::GetWindowPos().x;
        if (const float win_w = ImGui::GetWindowWidth(); mouse_pos.x >= win_x && mouse_pos.x <= win_x + win_w &&
                                                         mouse_pos.y >= row_y_min && mouse_pos.y <= row_y_max) {
            view_model_.process_list.selected_pid = node.info.pid;
            refresh_selected_details();
        }
    }

    if (is_open && !node.children.empty()) {
        view_model_.process_list.collapsed_pids.erase(node.info.pid);
        for (auto& child : node.children) {
            render_process_tree_node(*child, depth + 1, ghost_children);
        }
        // Children that exited last tick appear where they used to be
        render_ghost_children(node.info.pid, ghost_children);
        ImGui::TreePop();
    } else if (!node.children.empty()) {
        // Collapsed: ghost children stay hidden along with the live ones
        view_model_.process_list.collapsed_pids.insert(node.info.pid);
    } else {
        // Leaf whose only children just exited: show them right below it
        render_ghost_children(node.info.pid, ghost_children);
    }

    ImGui::PopID();
}

void ImGuiApp::render_process_list() {
    if (!current_data_) return;

    if (ImGui::BeginTable("ProcessList", kColumnCount,
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
        ImGui::TableSetupColumn("Read/s", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Write/s", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Threads", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("User", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Executable", ImGuiTableColumnFlags_WidthFixed, 200);
        ImGui::TableSetupColumn("Command Line", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        show_column_tooltips();

        // Flatten tree for list view
        const bool show_kernel = view_model_.process_list.show_kernel_threads;
        std::vector<ProcessNode*> flat_list;
        std::function<void(ProcessNode*)> flatten;
        flatten = [&](ProcessNode* node) {
            if (!show_kernel && node->info.is_kernel_thread) return;
            flat_list.push_back(node);
            for (auto& child : node->children) {
                flatten(child.get());
            }
        };
        for (auto& root : current_data_->process_tree) {
            flatten(root.get());
        }

        // Live rows plus exited ghosts (issue #61), merged before sorting so
        // a ghost occupies the position its process held a tick ago instead
        // of clustering at one end of the table (node == nullptr marks a
        // ghost; its tree aggregates sort as zero).
        struct RowRef {
            const ProcessNode* node;
            const ProcessInfo* info;
        };
        std::vector<RowRef> rows;
        rows.reserve(flat_list.size() + snapshot_diff_.exited_processes.size());
        for (const ProcessNode* n : flat_list) {
            rows.push_back({n, &n->info});
        }
        for (const auto& info : snapshot_diff_.exited_processes) {
            if (!show_kernel && info.is_kernel_thread) continue;
            rows.push_back({nullptr, &info});
        }

        // Handle sorting
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                const auto& spec = sort_specs->Specs[0];
                view_model_.process_list.sort_column = spec.ColumnIndex;
                view_model_.process_list.sort_ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
            }
        }

        if (!rows.empty()) {
            const int column = view_model_.process_list.sort_column;
            const bool ascending = view_model_.process_list.sort_ascending;
            std::ranges::sort(rows, [column, ascending](const RowRef& ra, const RowRef& rb) {
                const ProcessInfo& a = *ra.info;
                const ProcessInfo& b = *rb.info;
                const double a_tcpu = ra.node ? ra.node->tree_cpu_percent : 0.0;
                const double b_tcpu = rb.node ? rb.node->tree_cpu_percent : 0.0;
                const double a_ttot = ra.node ? ra.node->tree_total_cpu_percent : 0.0;
                const double b_ttot = rb.node ? rb.node->tree_total_cpu_percent : 0.0;
                const int64_t a_tmem = ra.node ? ra.node->tree_working_set : 0;
                const int64_t b_tmem = rb.node ? rb.node->tree_working_set : 0;
                const double a_tpct = ra.node ? ra.node->tree_memory_percent : 0.0;
                const double b_tpct = rb.node ? rb.node->tree_memory_percent : 0.0;
                int result = 0;
                switch (column) {
                    case 0: result = a.name.compare(b.name); break;
                    case 1: result = (a.pid < b.pid) ? -1 : (a.pid > b.pid) ? 1 : 0; break;
                    case 2: result = (a.cpu_percent < b.cpu_percent) ? -1 : (a.cpu_percent > b.cpu_percent) ? 1 : 0; break;
                    case 3: result = (a.total_cpu_percent < b.total_cpu_percent) ? -1 : (a.total_cpu_percent > b.total_cpu_percent) ? 1 : 0; break;
                    case 4: result = (a.resident_memory < b.resident_memory) ? -1 : (a.resident_memory > b.resident_memory) ? 1 : 0; break;
                    case 5: result = (a.memory_percent < b.memory_percent) ? -1 : (a.memory_percent > b.memory_percent) ? 1 : 0; break;
                    case 6: result = (a_tcpu < b_tcpu) ? -1 : (a_tcpu > b_tcpu) ? 1 : 0; break;
                    case 7: result = (a_ttot < b_ttot) ? -1 : (a_ttot > b_ttot) ? 1 : 0; break;
                    case 8: result = (a_tmem < b_tmem) ? -1 : (a_tmem > b_tmem) ? 1 : 0; break;
                    case 9: result = (a_tpct < b_tpct) ? -1 : (a_tpct > b_tpct) ? 1 : 0; break;
                    case 10: result = (a.io_read_rate < b.io_read_rate) ? -1 : (a.io_read_rate > b.io_read_rate) ? 1 : 0; break;
                    case 11: result = (a.io_write_rate < b.io_write_rate) ? -1 : (a.io_write_rate > b.io_write_rate) ? 1 : 0; break;
                    case 12: result = (a.thread_count < b.thread_count) ? -1 : (a.thread_count > b.thread_count) ? 1 : 0; break;
                    case 13: result = a.user_name.compare(b.user_name); break;
                    case 14: result = a.state_char - b.state_char; break;
                    case 15: result = a.executable_path.compare(b.executable_path); break;
                    case 16: result = a.command_line.compare(b.command_line); break;
                    default: result = 0; break;
                }
                return ascending ? (result < 0) : (result > 0);
            });
        }

        for (const RowRef& row_ref : rows) {
            if (!row_ref.node) {
                render_ghost_row(*row_ref.info);  // Exited: red, in sorted position
                continue;
            }
            const ProcessNode* node = row_ref.node;
            ImGui::PushID(node->info.pid);
            ImGui::TableNextRow();

            if ((node->info.pid == view_model_.process_list.selected_pid)) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                    ImGui::GetColorU32(ImVec4(0.3f, 0.5f, 0.8f, 0.5f)));
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                    ImGui::GetColorU32(ImVec4(0.3f, 0.5f, 0.8f, 0.5f)));
                if (view_model_.process_list.scroll_to_selected) {
                    ImGui::SetScrollHereY(0.5f);
                    view_model_.process_list.scroll_to_selected = false;
                }
            } else if (snapshot_diff_.new_pids.contains(node->info.pid)) {
                // New since the previous snapshot (issue #61): green for one tick
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                    ImGui::GetColorU32(ImVec4(0.1f, 0.5f, 0.1f, 0.45f)));
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                    ImGui::GetColorU32(ImVec4(0.1f, 0.5f, 0.1f, 0.45f)));
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
            ImGui::Text("%s", format_io_rate(node->info.io_read_rate).c_str());

            ImGui::TableNextColumn();
            ImGui::Text("%s", format_io_rate(node->info.io_write_rate).c_str());

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

            // Handle row click
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
                        view_model_.process_list.selected_pid = node->info.pid;
                        refresh_selected_details();
                    }
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        view_model_.process_popup.target_pid = node->info.pid;
                        view_model_.process_popup.is_visible = true;
                        view_model_.process_popup.include_tree = true;
                        view_model_.process_popup.clear_history();
                    }
                }
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

} // namespace pex
