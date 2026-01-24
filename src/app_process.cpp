#include "app.hpp"

namespace pex {

void App::refresh_selected_details() {
    if (selected_pid_ <= 0) {
        // No selection, clear details
        if (details_pid_ != -1) {
            file_handles_.clear();
            network_connections_.clear();
            threads_.clear();
            memory_maps_.clear();
            environment_vars_.clear();
            libraries_.clear();
            selected_thread_idx_ = -1;
            selected_thread_tid_ = -1;
            cached_stack_tid_ = -1;
            cached_stack_.clear();
            details_pid_ = -1;
            details_dirty_ = true;
        }
        return;
    }

    // Check if process still exists
    if (!current_data_ || !current_data_->process_map.contains(selected_pid_)) {
        // Process no longer exists, clear details
        file_handles_.clear();
        network_connections_.clear();
        threads_.clear();
        memory_maps_.clear();
        environment_vars_.clear();
        libraries_.clear();
        selected_thread_idx_ = -1;
        selected_thread_tid_ = -1;
        cached_stack_tid_ = -1;
        cached_stack_.clear();
        details_pid_ = -1;
        details_dirty_ = true;
        return;
    }

    // If PID changed, clear all cached data for fresh start
    if (details_pid_ != selected_pid_) {
        file_handles_.clear();
        network_connections_.clear();
        threads_.clear();
        memory_maps_.clear();
        environment_vars_.clear();
        libraries_.clear();
        selected_thread_idx_ = -1;
        selected_thread_tid_ = -1;
        cached_stack_tid_ = -1;
        cached_stack_.clear();
        details_pid_ = selected_pid_;
        details_dirty_ = true;
    }

    // Only refresh data for the currently visible tab
    switch (active_details_tab_) {
        case DetailsTab::FileHandles:
            file_handles_ = ProcfsReader::get_file_handles(selected_pid_);
            details_dirty_ = true;
            break;
        case DetailsTab::Network:
            network_connections_ = ProcfsReader::get_network_connections(selected_pid_);
            details_dirty_ = true;
            break;
        case DetailsTab::Threads:
            threads_ = details_reader_.get_threads(selected_pid_);
            details_dirty_ = true;
            // Preserve thread selection by TID across refreshes
            selected_thread_idx_ = -1;
            if (selected_thread_tid_ != -1) {
                for (int i = 0; i < static_cast<int>(threads_.size()); i++) {
                    if (threads_[i].tid == selected_thread_tid_) {
                        selected_thread_idx_ = i;
                        break;
                    }
                }
            }
            if (selected_thread_idx_ == -1) {
                selected_thread_tid_ = -1;
                cached_stack_tid_ = -1;
                cached_stack_.clear();
            } else {
                // Invalidate stack cache to refresh on next render
                cached_stack_tid_ = -1;
            }
            break;
        case DetailsTab::Memory:
            memory_maps_ = details_reader_.get_memory_maps(selected_pid_);
            details_dirty_ = true;
            break;
        case DetailsTab::Environment:
            environment_vars_ = details_reader_.get_environment_variables(selected_pid_);
            details_dirty_ = true;
            break;
        case DetailsTab::Libraries:
            libraries_ = details_reader_.get_libraries(selected_pid_);
            details_dirty_ = true;
            break;
    }
}

} // namespace pex
