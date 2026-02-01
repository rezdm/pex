#pragma once

#include "process_list_view_model.hpp"
#include "details_panel_view_model.hpp"
#include "process_popup_view_model.hpp"
#include "kill_dialog_view_model.hpp"
#include "system_panel_view_model.hpp"

namespace pex {

// Root ViewModel containing all child ViewModels
struct AppViewModel {
    ProcessListViewModel process_list;
    DetailsPanelViewModel details_panel;
    ProcessPopupViewModel process_popup;
    KillDialogViewModel kill_dialog;
    SystemPanelViewModel system_panel;

    // Update system panel from data snapshot
    void update_from_snapshot(const std::shared_ptr<DataSnapshot>& snapshot) {
        if (!snapshot) return;

        // Update process list data
        process_list.data = snapshot;

        // Update system panel
        system_panel.per_cpu_usage = snapshot->per_cpu_usage;
        system_panel.per_cpu_user = snapshot->per_cpu_user;
        system_panel.per_cpu_system = snapshot->per_cpu_system;
        system_panel.memory_used = snapshot->memory_used;
        system_panel.memory_total = snapshot->memory_total;
        system_panel.swap_info = snapshot->swap_info;
        system_panel.load_average = snapshot->load_average;
        system_panel.uptime_info = snapshot->uptime_info;
        system_panel.process_count = snapshot->process_count;
        system_panel.thread_count = snapshot->thread_count;
        system_panel.running_count = snapshot->running_count;
        system_panel.cpu_usage = snapshot->cpu_usage;
    }
};

} // namespace pex
