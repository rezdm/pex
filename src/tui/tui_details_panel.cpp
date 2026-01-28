#include "tui_app.hpp"
#include "tui_colors.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>

namespace pex {

void TuiApp::refresh_selected_details() {
    auto& dp = view_model_.details_panel;
    int selected_pid = view_model_.process_list.selected_pid;

    if (selected_pid < 0 || !details_provider_) {
        dp.file_handles.clear();
        dp.network_connections.clear();
        dp.threads.clear();
        dp.memory_maps.clear();
        dp.environment_vars.clear();
        dp.libraries.clear();
        return;
    }

    if (dp.details_pid != selected_pid) {
        dp.details_pid = selected_pid;
        details_scroll_offset_ = 0;
    }

    // Fetch data based on active tab
    switch (dp.active_tab) {
        case DetailsTab::FileHandles:
            dp.file_handles = details_provider_->get_file_handles(selected_pid);
            break;
        case DetailsTab::Network:
            dp.network_connections = details_provider_->get_network_connections(selected_pid);
            break;
        case DetailsTab::Threads:
            dp.threads = details_provider_->get_threads(selected_pid);
            break;
        case DetailsTab::Memory:
            dp.memory_maps = details_provider_->get_memory_maps(selected_pid);
            break;
        case DetailsTab::Environment:
            dp.environment_vars = details_provider_->get_environment_variables(selected_pid);
            break;
        case DetailsTab::Libraries:
            dp.libraries = details_provider_->get_libraries(selected_pid);
            break;
    }
}

void TuiApp::render_details_panel() {
    if (!details_win_) return;

    int max_y, max_x;
    getmaxyx(details_win_, max_y, max_x);

    // Draw border
    draw_box_title(details_win_, current_focus_ == PanelFocus::DetailsPanel ? "[Details]" : "Details");

    // Refresh details for selected process
    refresh_selected_details();

    // Tab bar
    const char* tabs[] = {"[F]iles", "[N]etwork", "[T]hreads", "[M]emory", "[E]nv", "[L]ibraries"};
    int tab_x = 2;

    for (int i = 0; i < 6; ++i) {
        bool is_active = (i == static_cast<int>(view_model_.details_panel.active_tab));

        if (is_active) {
            wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TAB_ACTIVE) | A_BOLD);
        } else {
            wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TAB_INACTIVE));
        }

        mvwprintw(details_win_, 1, tab_x, " %s ", tabs[i]);

        if (is_active) {
            wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TAB_ACTIVE) | A_BOLD);
        } else {
            wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TAB_INACTIVE));
        }

        tab_x += strlen(tabs[i]) + 3;
    }

    // Separator line under tabs
    mvwhline(details_win_, 2, 1, ACS_HLINE, max_x - 2);

    visible_details_rows_ = max_y - 4;  // Account for border, tabs, and separator

    // Render active tab content
    switch (view_model_.details_panel.active_tab) {
        case DetailsTab::FileHandles:
            render_file_handles_tab();
            break;
        case DetailsTab::Network:
            render_network_tab();
            break;
        case DetailsTab::Threads:
            render_threads_tab();
            break;
        case DetailsTab::Memory:
            render_memory_tab();
            break;
        case DetailsTab::Environment:
            render_environment_tab();
            break;
        case DetailsTab::Libraries:
            render_libraries_tab();
            break;
    }
}

void TuiApp::render_file_handles_tab() {
    if (!details_win_) return;

    int max_y, max_x;
    getmaxyx(details_win_, max_y, max_x);

    const auto& handles = view_model_.details_panel.file_handles;

    // Header
    wattron(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvwprintw(details_win_, 3, 2, "%-5s %-10s %s", "FD", "Type", "Path");
    wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);

    if (handles.empty()) {
        mvwprintw(details_win_, 4, 2, "(no file handles or access denied)");
        return;
    }

    int row = 4;
    for (size_t i = details_scroll_offset_;
         i < handles.size() && row < max_y - 1;
         ++i, ++row) {

        const auto& fh = handles[i];
        std::string path = fh.path;
        int path_width = max_x - 20;
        if (static_cast<int>(path.length()) > path_width && path_width > 3) {
            path = path.substr(0, path_width - 3) + "...";
        }

        mvwprintw(details_win_, row, 2, "%-5d %-10s %s",
                  fh.fd, fh.type.c_str(), path.c_str());
    }

    // Scroll indicators
    if (details_scroll_offset_ > 0) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, 3, max_x - 4, "^^^");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
    if (details_scroll_offset_ + visible_details_rows_ < static_cast<int>(handles.size())) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, max_y - 2, max_x - 4, "vvv");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
}

void TuiApp::render_network_tab() {
    if (!details_win_) return;

    int max_y, max_x;
    getmaxyx(details_win_, max_y, max_x);

    const auto& connections = view_model_.details_panel.network_connections;

    // Header
    wattron(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvwprintw(details_win_, 3, 2, "%-8s %-25s %-25s %s", "Proto", "Local Address", "Remote Address", "State");
    wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);

    if (connections.empty()) {
        mvwprintw(details_win_, 4, 2, "(no network connections or access denied)");
        return;
    }

    int row = 4;
    for (size_t i = details_scroll_offset_;
         i < connections.size() && row < max_y - 1;
         ++i, ++row) {

        const auto& conn = connections[i];

        std::string local = conn.local_endpoint;
        std::string remote = conn.remote_endpoint;

        if (local.length() > 25) local = local.substr(0, 22) + "...";
        if (remote.length() > 25) remote = remote.substr(0, 22) + "...";

        mvwprintw(details_win_, row, 2, "%-8s %-25s %-25s %s",
                  conn.protocol.c_str(),
                  local.c_str(),
                  remote.c_str(),
                  conn.state.c_str());
    }

    // Scroll indicators
    if (details_scroll_offset_ > 0) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, 3, max_x - 4, "^^^");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
    if (details_scroll_offset_ + visible_details_rows_ < static_cast<int>(connections.size())) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, max_y - 2, max_x - 4, "vvv");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
}

void TuiApp::render_threads_tab() {
    if (!details_win_) return;

    int max_y, max_x;
    getmaxyx(details_win_, max_y, max_x);

    const auto& threads = view_model_.details_panel.threads;

    // Header
    wattron(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvwprintw(details_win_, 3, 2, "%-8s %-20s %-5s %-8s %-4s %s",
              "TID", "Name", "State", "Priority", "CPU", "Function");
    wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);

    if (threads.empty()) {
        mvwprintw(details_win_, 4, 2, "(no threads or access denied)");
        return;
    }

    int row = 4;
    for (size_t i = details_scroll_offset_;
         i < threads.size() && row < max_y - 1;
         ++i, ++row) {

        const auto& thr = threads[i];

        std::string name = thr.name;
        if (name.length() > 20) name = name.substr(0, 17) + "...";

        std::string func = thr.current_library;
        int func_width = max_x - 55;
        if (func_width > 0 && static_cast<int>(func.length()) > func_width) {
            func = func.substr(0, func_width - 3) + "...";
        }

        int state_color = get_state_color(thr.state);
        wattron(details_win_, COLOR_PAIR(state_color));

        mvwprintw(details_win_, row, 2, "%-8d %-20s   %c   %-8d %-4d %s",
                  thr.tid,
                  name.c_str(),
                  thr.state,
                  thr.priority,
                  thr.processor,
                  func.c_str());

        wattroff(details_win_, COLOR_PAIR(state_color));
    }

    // Scroll indicators
    if (details_scroll_offset_ > 0) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, 3, max_x - 4, "^^^");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
    if (details_scroll_offset_ + visible_details_rows_ < static_cast<int>(threads.size())) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, max_y - 2, max_x - 4, "vvv");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
}

void TuiApp::render_memory_tab() {
    if (!details_win_) return;

    int max_y, max_x;
    getmaxyx(details_win_, max_y, max_x);

    const auto& maps = view_model_.details_panel.memory_maps;

    // Header
    wattron(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvwprintw(details_win_, 3, 2, "%-18s %-10s %-6s %s", "Address", "Size", "Perms", "Pathname");
    wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);

    if (maps.empty()) {
        mvwprintw(details_win_, 4, 2, "(no memory maps or access denied)");
        return;
    }

    int row = 4;
    for (size_t i = details_scroll_offset_;
         i < maps.size() && row < max_y - 1;
         ++i, ++row) {

        const auto& mm = maps[i];

        std::string path = mm.pathname;
        int path_width = max_x - 40;
        if (path_width > 0 && static_cast<int>(path.length()) > path_width) {
            path = path.substr(0, path_width - 3) + "...";
        }

        mvwprintw(details_win_, row, 2, "%-18s %-10s %-6s %s",
                  mm.address.c_str(),
                  mm.size.c_str(),
                  mm.permissions.c_str(),
                  path.c_str());
    }

    // Scroll indicators
    if (details_scroll_offset_ > 0) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, 3, max_x - 4, "^^^");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
    if (details_scroll_offset_ + visible_details_rows_ < static_cast<int>(maps.size())) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, max_y - 2, max_x - 4, "vvv");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
}

void TuiApp::render_environment_tab() {
    if (!details_win_) return;

    int max_y, max_x;
    getmaxyx(details_win_, max_y, max_x);

    const auto& vars = view_model_.details_panel.environment_vars;

    // Header
    wattron(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvwprintw(details_win_, 3, 2, "%-30s %s", "Variable", "Value");
    wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);

    if (vars.empty()) {
        mvwprintw(details_win_, 4, 2, "(no environment variables or access denied)");
        return;
    }

    int row = 4;
    for (size_t i = details_scroll_offset_;
         i < vars.size() && row < max_y - 1;
         ++i, ++row) {

        const auto& ev = vars[i];

        std::string name = ev.name;
        if (name.length() > 30) name = name.substr(0, 27) + "...";

        std::string value = ev.value;
        int value_width = max_x - 35;
        if (value_width > 0 && static_cast<int>(value.length()) > value_width) {
            value = value.substr(0, value_width - 3) + "...";
        }

        mvwprintw(details_win_, row, 2, "%-30s %s", name.c_str(), value.c_str());
    }

    // Scroll indicators
    if (details_scroll_offset_ > 0) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, 3, max_x - 4, "^^^");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
    if (details_scroll_offset_ + visible_details_rows_ < static_cast<int>(vars.size())) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, max_y - 2, max_x - 4, "vvv");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
}

void TuiApp::render_libraries_tab() {
    if (!details_win_) return;

    int max_y, max_x;
    getmaxyx(details_win_, max_y, max_x);

    const auto& libs = view_model_.details_panel.libraries;

    // Header
    wattron(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);
    mvwprintw(details_win_, 3, 2, "%-18s %-10s %-10s %s", "Base Address", "Size", "Resident", "Path");
    wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_HEADER) | A_BOLD);

    if (libs.empty()) {
        mvwprintw(details_win_, 4, 2, "(no libraries or access denied)");
        return;
    }

    int row = 4;
    for (size_t i = details_scroll_offset_;
         i < libs.size() && row < max_y - 1;
         ++i, ++row) {

        const auto& lib = libs[i];

        std::string path = lib.path;
        int path_width = max_x - 45;
        if (path_width > 0 && static_cast<int>(path.length()) > path_width) {
            path = path.substr(0, path_width - 3) + "...";
        }

        std::ostringstream addr;
        addr << "0x" << std::hex << lib.base_address;

        mvwprintw(details_win_, row, 2, "%-18s %-10s %-10s %s",
                  addr.str().c_str(),
                  format_bytes(lib.total_size).c_str(),
                  format_bytes(lib.resident_size).c_str(),
                  path.c_str());
    }

    // Scroll indicators
    if (details_scroll_offset_ > 0) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, 3, max_x - 4, "^^^");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
    if (details_scroll_offset_ + visible_details_rows_ < static_cast<int>(libs.size())) {
        wattron(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
        mvwprintw(details_win_, max_y - 2, max_x - 4, "vvv");
        wattroff(details_win_, COLOR_PAIR(COLOR_PAIR_TITLE));
    }
}

} // namespace pex
