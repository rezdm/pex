#include "tui_app.hpp"
#include "tui_colors.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <map>
#include <set>

namespace pex {

// Helper to check if a node is the last child of its parent
static bool is_last_child(ProcessNode* node, const std::map<int, ProcessNode*>& process_map) {
    if (!process_map.contains(node->info.parent_pid)) return true;

    ProcessNode* parent = process_map.at(node->info.parent_pid);
    if (parent->children.empty()) return true;

    return parent->children.back()->info.pid == node->info.pid;
}

// Check if node has a visible parent in the tree
static bool has_visible_parent(ProcessNode* node, const std::map<int, ProcessNode*>& process_map) {
    return process_map.contains(node->info.parent_pid);
}

// Build list of ancestors from node to root (excluding node itself)
static std::vector<ProcessNode*> get_ancestors(ProcessNode* node, const std::map<int, ProcessNode*>& process_map) {
    std::vector<ProcessNode*> ancestors;
    int pid = node->info.parent_pid;
    int prev_pid = node->info.pid;

    // Prevent infinite loop: stop if pid == parent_pid (self-parent like kernel)
    // or if we've seen this pid before (cycle), or after max iterations
    int max_depth = 100;
    while (process_map.contains(pid) && pid != prev_pid && max_depth-- > 0) {
        ProcessNode* ancestor = process_map.at(pid);
        ancestors.push_back(ancestor);
        prev_pid = pid;
        pid = ancestor->info.parent_pid;
        // Also stop if parent points to itself
        if (pid == prev_pid) break;
    }

    // Reverse so oldest ancestor is first
    std::reverse(ancestors.begin(), ancestors.end());
    return ancestors;
}

// Build the tree connector prefix for a node
// Returns a vector where each entry indicates whether to draw │ (true) or space (false)
static std::vector<bool> get_tree_path(ProcessNode* node, const std::map<int, ProcessNode*>& process_map) {
    std::vector<bool> path;

    // Get all ancestors (oldest first)
    auto ancestors = get_ancestors(node, process_map);

    // For each ancestor (except the root), check if it has more siblings
    // We skip the first ancestor (root) because there's nothing to draw above it
    for (size_t i = 1; i < ancestors.size(); ++i) {
        ProcessNode* ancestor = ancestors[i];
        bool last = is_last_child(ancestor, process_map);
        path.push_back(!last);  // If ancestor is NOT last, draw continuation │
    }

    return path;
}

void TuiApp::render_process_tree() {
    if (!process_win_ || !current_data_) return;

    int max_y, max_x;
    getmaxyx(process_win_, max_y, max_x);

    // Draw border and title
    std::string title = current_focus_ == PanelFocus::ProcessList ? "[Process Tree]" : "Process Tree";
    if (process_h_scroll_ > 0) {
        title += " [</>:scroll]";
    }
    draw_box_title(process_win_, title);

    // Fixed column width (Process name with tree structure)
    constexpr int tree_col_width = 32;

    // Scrollable header columns
    std::string header_scroll = "   PID   CPU%    Memory  Mem% Threads User     State TrCPU% TrTot%   TreeMem  Command";

    // Render fixed header (Process column)
    wattron(process_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvwprintw(process_win_, 1, 2, "%-30s", "Process");

    // Render scrollable part of header
    int scroll_start = tree_col_width;
    int scroll_width = max_x - scroll_start - 2;
    if (scroll_width > 0 && process_h_scroll_ < static_cast<int>(header_scroll.length())) {
        std::string visible_header = header_scroll.substr(process_h_scroll_);
        if (static_cast<int>(visible_header.length()) > scroll_width) {
            visible_header = visible_header.substr(0, scroll_width);
        }
        mvwprintw(process_win_, 1, scroll_start, "%s", visible_header.c_str());
    }
    wattroff(process_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);

    auto visible = get_visible_items();
    visible_process_rows_ = std::max(1, max_y - 3);

    // Adjust scroll to keep selection visible
    scroll_to_selection();

    int row = 2;
    for (size_t i = process_scroll_offset_;
         i < visible.size() && row < max_y - 1;
         ++i) {
        ProcessNode* node = visible[i];
        const auto& info = node->info;
        bool is_selected = (info.pid == view_model_.process_list.selected_pid);
        bool is_match = matches_search(info) && !view_model_.process_list.search_text.empty();
        bool has_children = !node->children.empty();
        bool is_collapsed = view_model_.process_list.collapsed_pids.contains(info.pid);

        bool has_parent = has_visible_parent(node, current_data_->process_map);
        bool node_is_last = is_last_child(node, current_data_->process_map);
        auto tree_path = get_tree_path(node, current_data_->process_map);

        // Use pre-computed tree totals from DataStore (avoids O(n^2) recalculation)
        double tree_cpu = node->tree_cpu_percent;
        double tree_cpu_total = node->tree_total_cpu_percent;
        int64_t tree_mem = node->tree_working_set;

        // Highlight selected row
        if (is_selected) {
            wattron(process_win_, COLOR_PAIR(COLOR_PAIR_SELECTED));
            mvwhline(process_win_, row, 1, ' ', max_x - 2);
        } else if (is_match) {
            wattron(process_win_, COLOR_PAIR(COLOR_PAIR_SEARCH));
            mvwhline(process_win_, row, 1, ' ', max_x - 2);
        }

        int col = 2;

        // Draw tree connectors
        if (!is_selected && !is_match) {
            wattron(process_win_, COLOR_PAIR(COLOR_PAIR_TREE_LINE));
        }

        // Draw continuation lines for ancestors (│ or space)
        for (auto && d : tree_path) {
            if (d) {
                mvwaddch(process_win_, row, col, ACS_VLINE);  // │
                mvwaddch(process_win_, row, col + 1, ' ');
            } else {
                mvwaddstr(process_win_, row, col, "  ");
            }
            col += 2;
        }

        // Draw connector for this node (├ or └) if it has a parent
        if (has_parent) {
            if (node_is_last) {
                mvwaddch(process_win_, row, col, ACS_LLCORNER);  // └
            } else {
                mvwaddch(process_win_, row, col, ACS_LTEE);      // ├
            }
            mvwaddch(process_win_, row, col + 1, ACS_HLINE);     // ─
            col += 2;
        }

        if (!is_selected && !is_match) {
            wattroff(process_win_, COLOR_PAIR(COLOR_PAIR_TREE_LINE));
        }

        // Expand/collapse indicator
        if (has_children) {
            if (!is_selected && !is_match) {
                wattron(process_win_, COLOR_PAIR(COLOR_PAIR_TITLE) | A_BOLD);
            } else {
                wattron(process_win_, A_BOLD);
            }
            mvwaddch(process_win_, row, col, is_collapsed ? '+' : '-');
            if (!is_selected && !is_match) {
                wattroff(process_win_, COLOR_PAIR(COLOR_PAIR_TITLE) | A_BOLD);
            } else {
                wattroff(process_win_, A_BOLD);
            }
        } else {
            mvwaddch(process_win_, row, col, ' ');
        }
        col++;

        // Calculate available width for name
        int tree_prefix_width = static_cast<int>(tree_path.size()) * 2 + (has_parent ? 2 : 0) + 1;
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
        int state_color = get_state_color(info.state_char);
        if (!is_selected && !is_match) {
            wattron(process_win_, COLOR_PAIR(state_color));
        }

        // Print process name (fixed column)
        mvwprintw(process_win_, row, col, " %-*s", name_width - 1, name.c_str());

        // Build scrollable data row
        char data_buf[512];
        snprintf(data_buf, sizeof(data_buf),
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
            mvwprintw(process_win_, row, scroll_start, "%s", visible_data.c_str());
        }

        // Turn off attributes
        if (is_selected) {
            wattroff(process_win_, COLOR_PAIR(COLOR_PAIR_SELECTED));
        } else if (is_match) {
            wattroff(process_win_, COLOR_PAIR(COLOR_PAIR_SEARCH));
        } else {
            wattroff(process_win_, COLOR_PAIR(state_color));
        }
        row++;
    }

    // Scroll indicators
    if (process_scroll_offset_ > 0) {
        wattron(process_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(process_win_, 1, max_x - 4, "^^^");
        wattroff(process_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
    if (process_scroll_offset_ + visible_process_rows_ < static_cast<int>(visible.size())) {
        wattron(process_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(process_win_, max_y - 2, max_x - 4, "vvv");
        wattroff(process_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
}

void TuiApp::render_process_tree_node(ProcessNode& node, int depth, int& row,
                                      const std::vector<bool>& connector_state) {
    // Not used - rendering is done inline in render_process_tree()
    // Kept for interface compatibility
    (void)node;
    (void)depth;
    (void)row;
    (void)connector_state;
}

} // namespace pex
