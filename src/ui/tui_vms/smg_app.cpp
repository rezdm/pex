#include "smg_app.hpp"
#include "smg_colors.hpp"
#include <cassert>
#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdio>
#include <iostream>

#ifdef __VMS
#define __NEW_STARLET 1
#include <descrip.h>
#include <smgdef.h>
#include <smg$routines.h>
#include <ssdef.h>
#endif

namespace pex {

SmgApp::SmgApp(DataStore* data_store,
               ISystemDataProvider* system_provider,
               IProcessDataProvider* details_provider,
               IProcessKiller* killer)
    : data_store_(data_store)
    , system_provider_(system_provider)
    , details_provider_(details_provider)
    , killer_(killer)
{
    assert(data_store_ != nullptr);
    assert(system_provider_ != nullptr);
    assert(details_provider_ != nullptr);
    assert(killer_ != nullptr);
}

SmgApp::~SmgApp() {
    cleanup_displays();
}

// Helper to create a VMS descriptor from a std::string
#ifdef __VMS
static struct dsc$descriptor_s make_descriptor(const std::string& str) {
    struct dsc$descriptor_s desc;
    desc.dsc$w_length = str.length();
    desc.dsc$b_dtype = DSC$K_DTYPE_T;
    desc.dsc$b_class = DSC$K_CLASS_S;
    desc.dsc$a_pointer = const_cast<char*>(str.c_str());
    return desc;
}
#endif

void SmgApp::smg_put_chars(unsigned int display_id, int row, int col,
                           const std::string& text, unsigned int rendition) {
#ifdef __VMS
    struct dsc$descriptor_s text_desc = make_descriptor(text);
    int vms_row = row;  // SMG$ uses 1-based rows
    int vms_col = col;  // SMG$ uses 1-based columns
    smg$put_chars(&display_id, &text_desc, &vms_row, &vms_col, 0, &rendition);
#else
    (void)display_id; (void)row; (void)col; (void)text; (void)rendition;
#endif
}

void SmgApp::smg_erase_display(unsigned int display_id) {
#ifdef __VMS
    smg$erase_display(&display_id);
#else
    (void)display_id;
#endif
}

void SmgApp::smg_draw_border(unsigned int display_id) {
#ifdef __VMS
    smg$draw_rectangle(&display_id, nullptr, nullptr, nullptr, nullptr);
#else
    (void)display_id;
#endif
}

void SmgApp::smg_draw_box_title(unsigned int display_id, const std::string& title) {
    // SMG$CREATE_VIRTUAL_DISPLAY with SMG$M_BORDER handles the border.
    // We just put the title text at row 1, col 3 with bold+cyan rendition.
    if (!title.empty()) {
        std::string padded = " " + title + " ";
        smg_put_chars(display_id, 1, 3, padded, SMG_REND_TITLE);
    }
}

void SmgApp::run() {
#ifdef __VMS
    unsigned int status;

    std::cerr << "[SMG] run: creating pasteboard" << std::endl;
    // Create the pasteboard (screen)
    status = smg$create_pasteboard(&pasteboard_id_, 0, &term_rows_, &term_cols_);
    if (!(status & 1)) {
        std::cerr << "[SMG] run: pasteboard failed, status=" << status << std::endl;
        return;
    }
    std::cerr << "[SMG] run: pasteboard ok (" << term_cols_ << "x" << term_rows_ << ")" << std::endl;

    // Create the virtual keyboard for input
    std::cerr << "[SMG] run: creating keyboard" << std::endl;
    status = smg$create_virtual_keyboard(&keyboard_id_);
    if (!(status & 1)) {
        std::cerr << "[SMG] run: keyboard failed, status=" << status << std::endl;
        smg$delete_pasteboard(&pasteboard_id_, 0);
        return;
    }
    std::cerr << "[SMG] run: keyboard ok" << std::endl;

    // Create virtual displays (panels)
    std::cerr << "[SMG] run: creating displays" << std::endl;
    create_displays();
    std::cerr << "[SMG] run: displays created" << std::endl;

    // Start background services
    std::cerr << "[SMG] run: calling data_store_->start()" << std::endl;
    data_store_->start();
    std::cerr << "[SMG] run: data_store started" << std::endl;

    // Get initial data - wait for actual data
    std::cerr << "[SMG] run: waiting for initial data..." << std::endl;
    int retries = 50;
    current_data_ = data_store_->get_snapshot();
    while (retries-- > 0 && (!current_data_ || current_data_->process_count == 0)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        current_data_ = data_store_->get_snapshot();
    }

    if (!current_data_ || current_data_->process_count == 0) {
        std::cerr << "[SMG] run: no initial data after retries, exiting" << std::endl;
        cleanup_displays();
        smg$delete_virtual_keyboard(&keyboard_id_);
        smg$delete_pasteboard(&pasteboard_id_, 0);
        return;
    }

    std::cerr << "[SMG] run: got data (" << current_data_->process_count << " procs)" << std::endl;
    view_model_.update_from_snapshot(current_data_);
    std::cerr << "[SMG] run: entering main loop" << std::endl;

    running_ = true;
    auto last_update = std::chrono::steady_clock::now();
    constexpr auto update_interval = std::chrono::milliseconds(100);

    while (running_) {
        // Handle input (non-blocking with 16ms timeout)
        unsigned int key_code = 0;
        unsigned short term_code = 0;
        unsigned int timeout_val = 0;  // 0 = no wait (non-blocking)
        status = smg$read_keystroke(&keyboard_id_, &term_code, 0, &timeout_val);

        if (status == SS$_NORMAL && term_code != 0) {
            key_code = term_code;
            handle_input(key_code);
        }

        // Update data periodically
        auto now = std::chrono::steady_clock::now();
        if (now - last_update >= update_interval) {
            const auto new_data = data_store_->get_snapshot();
            if (new_data && (!current_data_ || new_data->timestamp != current_data_->timestamp)) {
                current_data_ = new_data;
                view_model_.update_from_snapshot(current_data_);
            }
            last_update = now;
        }

        // Render with batched pasteboard updates
        smg$begin_pasteboard_update(&pasteboard_id_);
        render();
        smg$end_pasteboard_update(&pasteboard_id_);

        // Small sleep to reduce CPU usage
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // Cleanup
    data_store_->stop();
    cleanup_displays();
    smg$delete_virtual_keyboard(&keyboard_id_);
    smg$delete_pasteboard(&pasteboard_id_, 0);

#else
    // Non-VMS stub: cannot run SMG$ TUI
    (void)this;
#endif
}

void SmgApp::render() {
    if (terminal_too_small_) return;

    // Erase all displays
    if (system_display_) smg_erase_display(system_display_);
    smg_erase_display(process_display_);
    smg_erase_display(details_display_);
    smg_erase_display(status_display_);

    // Render panels
    if (view_model_.system_panel.is_visible && system_display_) {
        render_system_panel();
    }

    render_process_tree();
    render_details_panel();
    render_status_bar();

    // Render overlays
    if (view_model_.kill_dialog.is_visible) {
        render_kill_dialog();
    }

    if (show_help_) {
        render_help_overlay();
    }

    if (search_mode_) {
        render_search_bar();
    }
}

// --- Navigation helpers (same logic as ncurses TUI) ---

std::vector<ProcessNode*> SmgApp::get_visible_items() const {
    std::vector<ProcessNode*> items;
    if (!current_data_) return items;

    for (const auto& root : current_data_->process_tree) {
        collect_visible_items(root.get(), items, view_model_.process_list.collapsed_pids);
    }
    return items;
}

void SmgApp::collect_visible_items(ProcessNode* node, std::vector<ProcessNode*>& items,
                                   const std::set<int>& collapsed) {
    if (!node) return;
    items.push_back(node);

    if (collapsed.count(node->info.pid) == 0) {
        for (const auto& child : node->children) {
            collect_visible_items(child.get(), items, collapsed);
        }
    }
}

void SmgApp::move_selection(int delta) {
    const auto visible = get_visible_items();
    if (visible.empty()) return;

    int current_pos = 0;
    for (size_t i = 0; i < visible.size(); ++i) {
        if (visible[i]->info.pid == view_model_.process_list.selected_pid) {
            current_pos = static_cast<int>(i);
            break;
        }
    }

    const int new_pos = std::clamp(current_pos + delta, 0, static_cast<int>(visible.size()) - 1);
    view_model_.process_list.selected_pid = visible[new_pos]->info.pid;
    scroll_to_selection();
}

void SmgApp::page_up() {
    move_selection(-visible_process_rows_);
}

void SmgApp::page_down() {
    move_selection(visible_process_rows_);
}

void SmgApp::scroll_to_selection() {
    const auto visible = get_visible_items();
    int selected_idx = 0;

    for (size_t i = 0; i < visible.size(); ++i) {
        if (visible[i]->info.pid == view_model_.process_list.selected_pid) {
            selected_idx = static_cast<int>(i);
            break;
        }
    }

    if (selected_idx < process_scroll_offset_) {
        process_scroll_offset_ = selected_idx;
    } else if (selected_idx >= process_scroll_offset_ + visible_process_rows_) {
        process_scroll_offset_ = selected_idx - visible_process_rows_ + 1;
    }
}

bool SmgApp::matches_search(const ProcessInfo& info) const {
    if (view_model_.process_list.search_text.empty()) return false;

    const auto& search = view_model_.process_list.search_text;

    auto to_lower = [](const std::string& s) {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return result;
    };

    const std::string search_lower = to_lower(search);

    if (to_lower(info.name).find(search_lower) != std::string::npos) return true;
    if (to_lower(info.command_line).find(search_lower) != std::string::npos) return true;
    if (std::to_string(info.pid) == search) return true;

    return false;
}

std::vector<ProcessNode*> SmgApp::find_matching_processes() const {
    std::vector<ProcessNode*> matches;
    const auto visible = get_visible_items();
    for (auto* node : visible) {
        if (matches_search(node->info)) {
            matches.push_back(node);
        }
    }
    return matches;
}

void SmgApp::search_select_first() {
    const auto matches = find_matching_processes();
    if (!matches.empty()) {
        view_model_.process_list.selected_pid = matches[0]->info.pid;
        scroll_to_selection();
    }
}

void SmgApp::search_next() {
    const auto matches = find_matching_processes();
    if (matches.empty()) return;

    for (size_t i = 0; i < matches.size(); ++i) {
        if (matches[i]->info.pid == view_model_.process_list.selected_pid) {
            const size_t next = (i + 1) % matches.size();
            view_model_.process_list.selected_pid = matches[next]->info.pid;
            scroll_to_selection();
            return;
        }
    }

    view_model_.process_list.selected_pid = matches[0]->info.pid;
    scroll_to_selection();
}

void SmgApp::search_previous() {
    const auto matches = find_matching_processes();
    if (matches.empty()) return;

    for (size_t i = 0; i < matches.size(); ++i) {
        if (matches[i]->info.pid == view_model_.process_list.selected_pid) {
            const size_t prev = (i == 0) ? matches.size() - 1 : i - 1;
            view_model_.process_list.selected_pid = matches[prev]->info.pid;
            scroll_to_selection();
            return;
        }
    }

    view_model_.process_list.selected_pid = matches[0]->info.pid;
    scroll_to_selection();
}

void SmgApp::next_tab() {
    int tab = static_cast<int>(view_model_.details_panel.active_tab);
    tab = (tab + 1) % kTabCount;
    view_model_.details_panel.active_tab = static_cast<DetailsTab>(tab);
    details_scroll_offset_ = 0;
}

void SmgApp::prev_tab() {
    int tab = static_cast<int>(view_model_.details_panel.active_tab);
    tab = (tab + kTabCount - 1) % kTabCount;
    view_model_.details_panel.active_tab = static_cast<DetailsTab>(tab);
    details_scroll_offset_ = 0;
}

void SmgApp::request_kill_process(int pid, const std::string& name, bool is_tree) {
    view_model_.kill_dialog.is_visible = true;
    view_model_.kill_dialog.target_pid = pid;
    view_model_.kill_dialog.target_name = name;
    view_model_.kill_dialog.is_tree_kill = is_tree;
    view_model_.kill_dialog.error_message.clear();
    view_model_.kill_dialog.show_force_option = false;
    dialog_debounce_ = 5;
}

void SmgApp::execute_kill(bool force) {
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
        kd.show_force_option = true;
        kd.error_message = "Process still running after $FORCEX";
    } else {
        kd.error_message = result.error_message;
    }
}

void SmgApp::collect_tree_pids(const ProcessNode* node, std::vector<int>& pids) {
    if (!node) return;
    pids.push_back(node->info.pid);
    for (const auto& child : node->children) {
        collect_tree_pids(child.get(), pids);
    }
}

std::string SmgApp::format_bytes(int64_t bytes) {
    // C++17-compatible implementation (no std::format)
    static const char* units[] = {"B", "K", "M", "G", "T", "P"};
    constexpr int max_unit = 5;

    int unit_idx = 0;
    auto size = static_cast<double>(bytes);

    while (size >= 1024.0 && unit_idx < max_unit) {
        size /= 1024.0;
        unit_idx++;
    }

    char buf[32];
    if (unit_idx == 0) {
        std::snprintf(buf, sizeof(buf), "%lld%s", static_cast<long long>(bytes), units[unit_idx]);
    } else if (size >= 100.0) {
        std::snprintf(buf, sizeof(buf), "%.0f%s", size, units[unit_idx]);
    } else if (size >= 10.0) {
        std::snprintf(buf, sizeof(buf), "%.1f%s", size, units[unit_idx]);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f%s", size, units[unit_idx]);
    }
    return std::string(buf);
}

std::string SmgApp::format_uptime(int64_t seconds) {
    const int days = seconds / 86400;
    const int hours = (seconds % 86400) / 3600;
    const int minutes = (seconds % 3600) / 60;
    const int secs = seconds % 60;

    std::ostringstream oss;
    if (days > 0) {
        oss << days << "d " << std::setfill('0') << std::setw(2) << hours
            << ":" << std::setw(2) << minutes << ":" << std::setw(2) << secs;
    } else {
        oss << std::setfill('0') << std::setw(2) << hours
            << ":" << std::setw(2) << minutes << ":" << std::setw(2) << secs;
    }
    return oss.str();
}

void SmgApp::draw_progress_bar(unsigned int display_id, int row, int col, int width,
                               double percent, unsigned int rendition, const std::string& label) {
    if (width < 3) return;

    const int bar_width = width - 2;
    const int filled = static_cast<int>(bar_width * std::clamp(percent, 0.0, 100.0) / 100.0);

    // Build the bar string: [####    ]
    std::string bar = "[";
    for (int i = 0; i < filled; ++i) bar += '#';
    for (int i = filled; i < bar_width; ++i) bar += ' ';
    bar += ']';

    // Put the bracket parts with normal rendition
    smg_put_chars(display_id, row, col, "[", SMG_REND_NORMAL);

    // Put filled portion with color rendition
    if (filled > 0) {
        std::string filled_str(filled, '#');
        smg_put_chars(display_id, row, col + 1, filled_str, rendition);
    }

    // Put empty portion
    if (filled < bar_width) {
        std::string empty_str(bar_width - filled, ' ');
        smg_put_chars(display_id, row, col + 1 + filled, empty_str, SMG_REND_NORMAL);
    }

    smg_put_chars(display_id, row, col + bar_width + 1, "]", SMG_REND_NORMAL);

    if (!label.empty()) {
        smg_put_chars(display_id, row, col + width + 1, label, SMG_REND_NORMAL);
    }
}

void SmgApp::draw_cpu_bar(unsigned int display_id, int row, int col, int width,
                          double user_pct, double system_pct, const std::string& label) {
    if (width < 3) return;

    const int bar_width = width - 2;
    const int user_chars = static_cast<int>(bar_width * std::clamp(user_pct, 0.0, 100.0) / 100.0);
    int system_chars = static_cast<int>(bar_width * std::clamp(system_pct, 0.0, 100.0) / 100.0);

    if (user_chars + system_chars > bar_width) {
        system_chars = bar_width - user_chars;
    }

    smg_put_chars(display_id, row, col, "[", SMG_REND_NORMAL);

    // User portion (green)
    if (user_chars > 0) {
        std::string user_str(user_chars, '#');
        smg_put_chars(display_id, row, col + 1, user_str, SMG_REND_CPU_USER);
    }

    // System portion (red)
    if (system_chars > 0) {
        std::string sys_str(system_chars, '#');
        smg_put_chars(display_id, row, col + 1 + user_chars, sys_str, SMG_REND_CPU_SYSTEM);
    }

    // Empty portion
    const int empty = bar_width - user_chars - system_chars;
    if (empty > 0) {
        std::string empty_str(empty, ' ');
        smg_put_chars(display_id, row, col + 1 + user_chars + system_chars, empty_str, SMG_REND_NORMAL);
    }

    smg_put_chars(display_id, row, col + bar_width + 1, "]", SMG_REND_NORMAL);

    if (!label.empty()) {
        smg_put_chars(display_id, row, col + width + 1, label, SMG_REND_NORMAL);
    }
}

} // namespace pex
