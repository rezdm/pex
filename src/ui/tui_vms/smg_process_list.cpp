#include "smg_app.hpp"
#include "smg_colors.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <unordered_map>
#include <cstdio>

namespace pex {

// Helper to check if a node is the last child of its parent
static bool is_last_child(ProcessNode* node, const std::unordered_map<int, ProcessNode*>& process_map) {
    if (process_map.count(node->info.parent_pid) == 0) return true;

    const ProcessNode* parent = process_map.at(node->info.parent_pid);
    if (parent->children.empty()) return true;

    return parent->children.back()->info.pid == node->info.pid;
}

// Check if node has a visible parent in the tree
static bool has_visible_parent(ProcessNode* node, const std::unordered_map<int, ProcessNode*>& process_map) {
    return process_map.count(node->info.parent_pid) > 0;
}

// Build list of ancestors from node to root (excluding node itself)
static std::vector<ProcessNode*> get_ancestors(ProcessNode* node, const std::unordered_map<int, ProcessNode*>& process_map) {
    std::vector<ProcessNode*> ancestors;
    int pid = node->info.parent_pid;
    int prev_pid = node->info.pid;

    int max_depth = 100;
    while (process_map.count(pid) > 0 && pid != prev_pid && max_depth-- > 0) {
        ProcessNode* ancestor = process_map.at(pid);
        ancestors.push_back(ancestor);
        prev_pid = pid;
        pid = ancestor->info.parent_pid;
        if (pid == prev_pid) break;
    }

    std::reverse(ancestors.begin(), ancestors.end());
    return ancestors;
}

// Build the tree connector prefix path
static std::vector<bool> get_tree_path(ProcessNode* node, const std::unordered_map<int, ProcessNode*>& process_map) {
    std::vector<bool> path;

    const auto ancestors = get_ancestors(node, process_map);

    for (size_t i = 1; i < ancestors.size(); ++i) {
        ProcessNode* ancestor = ancestors[i];
        const bool last = is_last_child(ancestor, process_map);
        path.push_back(!last);
    }

    return path;
}

void SmgApp::render_process_tree() {
    if (!process_display_ || !current_data_) return;

    // The process display has a border, so inner area starts at (1,1)
    // With SMG$M_BORDER, the usable area rows = display_rows, cols = display_cols
    // Row 1 = border title area, Row 2 = header, Row 3+ = data

    // Draw title on border
    std::string title = current_focus_ == SmgPanelFocus::ProcessList ? "[Process Tree]" : "Process Tree";
    if (process_h_scroll_ > 0) {
        title += " [</>:scroll]";
    }
    smg_draw_box_title(process_display_, title);

    // Fixed column width
    constexpr int tree_col_width = 32;

    // Scrollable header columns
    const std::string header_scroll = "   PID   CPU%    Memory  Mem% Threads User     State TrCPU% TrTot%   TreeMem  Command";

    // Render fixed header
    char header_fixed[32];
    std::snprintf(header_fixed, sizeof(header_fixed), "%-30s", "Process");
    smg_put_chars(process_display_, 1, 2, header_fixed, SMG_REND_HEADER);

    // Render scrollable part of header
    constexpr int scroll_start = tree_col_width;
    const int scroll_width = term_cols_ - scroll_start - 4;  // -4 for border + margin
    if (scroll_width > 0 && process_h_scroll_ < static_cast<int>(header_scroll.length())) {
        std::string visible_header = header_scroll.substr(process_h_scroll_);
        if (static_cast<int>(visible_header.length()) > scroll_width) {
            visible_header = visible_header.substr(0, scroll_width);
        }
        smg_put_chars(process_display_, 1, scroll_start, visible_header, SMG_REND_HEADER);
    }

    const auto visible = get_visible_items();
    visible_process_rows_ = std::max(1, term_rows_ - 5);  // Approximate

    // Adjust scroll to keep selection visible
    scroll_to_selection();

    int row = 2;  // Start below header (SMG$ 1-based, row 1 = header)
    for (size_t i = process_scroll_offset_;
         i < visible.size() && row < visible_process_rows_ + 2;
         ++i) {
        ProcessNode* node = visible[i];
        const auto& info = node->info;
        const bool is_selected = (info.pid == view_model_.process_list.selected_pid);
        const bool is_match = matches_search(info) && !view_model_.process_list.search_text.empty();
        const bool has_children = !node->children.empty();
        const bool is_collapsed = view_model_.process_list.collapsed_pids.count(info.pid) > 0;

        const bool has_parent = has_visible_parent(node, current_data_->process_map);
        const bool node_is_last = is_last_child(node, current_data_->process_map);
        auto tree_path = get_tree_path(node, current_data_->process_map);

        // Use pre-computed tree totals
        const double tree_cpu = node->tree_cpu_percent;
        const double tree_cpu_total = node->tree_total_cpu_percent;
        const int64_t tree_mem = node->tree_working_set;

        // Choose rendition for the row
        unsigned int row_rend = SMG_REND_NORMAL;
        if (is_selected) {
            row_rend = SMG_REND_SELECTED;
            // Fill row with spaces in selected rendition
            std::string blank(term_cols_ - 4, ' ');
            smg_put_chars(process_display_, row, 1, blank, SMG_REND_SELECTED);
        } else if (is_match) {
            row_rend = SMG_REND_SEARCH;
            std::string blank(term_cols_ - 4, ' ');
            smg_put_chars(process_display_, row, 1, blank, SMG_REND_SEARCH);
        }

        int col = 2;

        // Draw tree connectors
        unsigned int tree_rend = (is_selected || is_match) ? row_rend : SMG_REND_TREE_LINE;

        // Draw continuation lines for ancestors
        for (auto&& d : tree_path) {
            if (d) {
                smg_put_chars(process_display_, row, col, "|", tree_rend);
                smg_put_chars(process_display_, row, col + 1, " ", tree_rend);
            } else {
                smg_put_chars(process_display_, row, col, "  ", tree_rend);
            }
            col += 2;
        }

        // Draw connector for this node
        if (has_parent) {
            if (node_is_last) {
                smg_put_chars(process_display_, row, col, "`-", tree_rend);  // VT100: use ASCII
            } else {
                smg_put_chars(process_display_, row, col, "|-", tree_rend);
            }
            col += 2;
        }

        // Expand/collapse indicator
        if (has_children) {
            unsigned int ind_rend = (is_selected || is_match) ? (row_rend | SMG_REND_BOLD) : SMG_REND_TITLE;
            std::string ind(1, is_collapsed ? '+' : '-');
            smg_put_chars(process_display_, row, col, ind, ind_rend);
        } else {
            smg_put_chars(process_display_, row, col, " ", row_rend);
        }
        col++;

        // Calculate available width for name
        const int tree_prefix_width = static_cast<int>(tree_path.size()) * 2 + (has_parent ? 2 : 0) + 1;
        int name_width = 30 - tree_prefix_width;
        if (name_width < 5) name_width = 5;

        std::string name = info.name;
        if (static_cast<int>(name.length()) > name_width) {
            if (name_width > 3) {
                name = name.substr(0, name_width - 3) + "...";
            } else {
                name = name.substr(0, name_width);
            }
        }

        // State color (only if not selected/matched)
        unsigned int name_rend = row_rend;
        if (!is_selected && !is_match) {
            name_rend = get_state_rendition(info.state_char);
        }

        // Print process name
        char name_buf[64];
        std::snprintf(name_buf, sizeof(name_buf), " %-*s", name_width - 1, name.c_str());
        smg_put_chars(process_display_, row, col, name_buf, name_rend);

        // Build scrollable data row
        char data_buf[512];
        std::snprintf(data_buf, sizeof(data_buf),
                 "%7d %5.1f%% %9s %4.1f%% %7d %-8s    %c  %5.1f%% %5.1f%% %9s  %s",
                 info.pid,
                 info.cpu_percent,
                 format_bytes(info.resident_memory).c_str(),
                 info.memory_percent,
                 info.thread_count,
                 info.user_name.substr(0, 8).c_str(),
                 info.state_char,
                 tree_cpu,
                 tree_cpu_total,
                 format_bytes(tree_mem).c_str(),
                 info.command_line.c_str());

        // Render scrollable part
        std::string data_str(data_buf);
        if (scroll_width > 0 && process_h_scroll_ < static_cast<int>(data_str.length())) {
            std::string visible_data = data_str.substr(process_h_scroll_);
            if (static_cast<int>(visible_data.length()) > scroll_width) {
                visible_data = visible_data.substr(0, scroll_width);
            }
            smg_put_chars(process_display_, row, scroll_start, visible_data, name_rend);
        }

        row++;
    }

    // Scroll indicators
    if (process_scroll_offset_ > 0) {
        smg_put_chars(process_display_, 1, term_cols_ - 6, "^^^", SMG_REND_TITLE);
    }
    if (process_scroll_offset_ + visible_process_rows_ < static_cast<int>(visible.size())) {
        smg_put_chars(process_display_, visible_process_rows_ + 1, term_cols_ - 6, "vvv", SMG_REND_TITLE);
    }
}

} // namespace pex
