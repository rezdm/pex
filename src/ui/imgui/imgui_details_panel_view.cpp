#include "imgui_app.hpp"
#include "imgui.h"

namespace pex {

void ImGuiApp::render_details_panel() {
    bool tab_changed = false;
    auto& dp = view_model_.details_panel;

    if (ImGui::BeginTabBar("DetailsTabs")) {
        if (ImGui::BeginTabItem("File Handles")) {
            if (dp.active_tab != DetailsTab::FileHandles) tab_changed = true;
            dp.active_tab = DetailsTab::FileHandles;
            render_file_handles_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Network")) {
            if (dp.active_tab != DetailsTab::Network) tab_changed = true;
            dp.active_tab = DetailsTab::Network;
            render_network_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Threads")) {
            if (dp.active_tab != DetailsTab::Threads) tab_changed = true;
            dp.active_tab = DetailsTab::Threads;
            render_threads_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Memory")) {
            if (dp.active_tab != DetailsTab::Memory) tab_changed = true;
            dp.active_tab = DetailsTab::Memory;
            render_memory_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Environment")) {
            if (dp.active_tab != DetailsTab::Environment) tab_changed = true;
            dp.active_tab = DetailsTab::Environment;
            render_environment_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Libraries")) {
            if (dp.active_tab != DetailsTab::Libraries) tab_changed = true;
            dp.active_tab = DetailsTab::Libraries;
            render_libraries_tab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (tab_changed) {
        refresh_selected_details();
    }
}

void ImGuiApp::refresh_selected_details() {
    const auto& pl = view_model_.process_list;
    auto& dp = view_model_.details_panel;

    if (pl.selected_pid <= 0) {
        if (dp.details_pid != -1) {
            dp.file_handles.clear();
            dp.network_connections.clear();
            dp.threads.clear();
            dp.memory_maps.clear();
            dp.environment_vars.clear();
            dp.libraries.clear();
            dp.selected_thread_idx = -1;
            dp.selected_thread_tid = -1;
            dp.cached_stack_tid = -1;
            dp.cached_stack.clear();
            dp.details_pid = -1;
            dp.details_dirty = true;
        }
        return;
    }

    if (!current_data_ || !current_data_->process_map.contains(pl.selected_pid)) {
        dp.file_handles.clear();
        dp.network_connections.clear();
        dp.threads.clear();
        dp.memory_maps.clear();
        dp.environment_vars.clear();
        dp.libraries.clear();
        dp.selected_thread_idx = -1;
        dp.selected_thread_tid = -1;
        dp.cached_stack_tid = -1;
        dp.cached_stack.clear();
        dp.details_pid = -1;
        dp.details_dirty = true;
        return;
    }

    if (dp.details_pid != pl.selected_pid) {
        dp.file_handles.clear();
        dp.network_connections.clear();
        dp.threads.clear();
        dp.memory_maps.clear();
        dp.environment_vars.clear();
        dp.libraries.clear();
        dp.selected_thread_idx = -1;
        dp.selected_thread_tid = -1;
        dp.cached_stack_tid = -1;
        dp.cached_stack.clear();
        dp.details_pid = pl.selected_pid;
        dp.details_dirty = true;
    }

    switch (dp.active_tab) {
        case DetailsTab::FileHandles:
            dp.file_handles = details_provider_->get_file_handles(pl.selected_pid);
            dp.details_dirty = true;
            break;
        case DetailsTab::Network:
            dp.network_connections = details_provider_->get_network_connections(pl.selected_pid);
            dp.details_dirty = true;
            break;
        case DetailsTab::Threads:
            dp.threads = details_provider_->get_threads(pl.selected_pid);
            dp.details_dirty = true;
            dp.selected_thread_idx = -1;
            if (dp.selected_thread_tid != -1) {
                for (int i = 0; i < static_cast<int>(dp.threads.size()); i++) {
                    if (dp.threads[i].tid == dp.selected_thread_tid) {
                        dp.selected_thread_idx = i;
                        break;
                    }
                }
            }
            if (dp.selected_thread_idx == -1) {
                dp.selected_thread_tid = -1;
                dp.cached_stack_tid = -1;
                dp.cached_stack.clear();
            } else {
                dp.cached_stack_tid = -1;
            }
            break;
        case DetailsTab::Memory:
            dp.memory_maps = details_provider_->get_memory_maps(pl.selected_pid);
            dp.details_dirty = true;
            break;
        case DetailsTab::Environment:
            dp.environment_vars = details_provider_->get_environment_variables(pl.selected_pid);
            dp.details_dirty = true;
            break;
        case DetailsTab::Libraries:
            dp.libraries = details_provider_->get_libraries(pl.selected_pid);
            dp.details_dirty = true;
            break;
    }
}

} // namespace pex
