#include "tui_app.hpp"
#include "tui_colors.hpp"
#include <algorithm>
#include <cstring>

namespace pex {

void TuiApp::refresh_selected_details() {
    auto& dp = view_model_.details_panel;
    const int selected_pid = view_model_.process_list.selected_pid;

    if (selected_pid < 0 || !details_provider_) {
        dp.file_handles.clear();
        dp.network_connections.clear();
        dp.threads.clear();
        dp.memory_maps.clear();
        dp.environment_vars.clear();
        dp.libraries.clear();
        details_last_pid_ = -1;
        details_needs_refresh_ = false;
        return;
    }

    // Check if refresh is actually needed (PID changed, tab changed, or explicit request)
    const bool pid_changed = (selected_pid != details_last_pid_);
    const bool tab_changed = (dp.active_tab != details_last_tab_);

    if (!pid_changed && !tab_changed && !details_needs_refresh_) {
        return;  // No refresh needed - use cached data
    }

    // Update tracking
    if (pid_changed) {
        dp.details_pid = selected_pid;
        details_scroll_offset_ = 0;
    }
    details_last_pid_ = selected_pid;
    details_last_tab_ = dp.active_tab;
    details_needs_refresh_ = false;

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
        const bool is_active = (i == static_cast<int>(view_model_.details_panel.active_tab));

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

    // Clamp scroll offset to valid range after rendering (data may have changed)
    int data_count = 0;
    switch (view_model_.details_panel.active_tab) {
        case DetailsTab::FileHandles: data_count = static_cast<int>(view_model_.details_panel.file_handles.size()); break;
        case DetailsTab::Network: data_count = static_cast<int>(view_model_.details_panel.network_connections.size()); break;
        case DetailsTab::Threads: data_count = static_cast<int>(view_model_.details_panel.threads.size()); break;
        case DetailsTab::Memory: data_count = static_cast<int>(view_model_.details_panel.memory_maps.size()); break;
        case DetailsTab::Environment: data_count = static_cast<int>(view_model_.details_panel.environment_vars.size()); break;
        case DetailsTab::Libraries: data_count = static_cast<int>(view_model_.details_panel.libraries.size()); break;
    }
    const int max_scroll = std::max(0, data_count - visible_details_rows_);
    if (details_scroll_offset_ > max_scroll) {
        details_scroll_offset_ = max_scroll;
    }
}

} // namespace pex
