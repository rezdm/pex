#include "app.hpp"
#include "system_info.hpp"
#include "imgui.h"
#include <fstream>
#include <sstream>
#include <format>

namespace pex {

void App::collect_tree_pids(const ProcessNode* node, std::vector<int>& pids) {
    if (!node) return;
    pids.push_back(node->info.pid);
    for (const auto& child : node->children) {
        collect_tree_pids(child.get(), pids);
    }
}

// Helper to read CPU times from /proc/<pid>/stat
static bool read_cpu_times(int pid, uint64_t& utime, uint64_t& stime) {
    std::ifstream stat_file("/proc/" + std::to_string(pid) + "/stat");
    if (!stat_file) return false;

    std::string line;
    std::getline(stat_file, line);

    size_t paren_end = line.rfind(')');
    if (paren_end == std::string::npos) return false;

    std::istringstream iss(line.substr(paren_end + 2));
    std::string state;
    int ppid, pgrp, session, tty_nr, tpgid;
    unsigned int flags;
    uint64_t minflt, cminflt, majflt, cmajflt;

    iss >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags
        >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime;

    return true;
}

void App::update_popup_history() {
    if (!show_process_popup_ || popup_pid_ <= 0 || !current_data_) return;

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - popup_last_update_).count();
    if (elapsed < 500) return;  // Update every 500ms
    popup_last_update_ = now;

    // Find the process
    const auto it = current_data_->process_map.find(popup_pid_);
    if (it == current_data_->process_map.end()) return;

    const auto* node = it->second;

    // Collect PIDs to aggregate (just this process, or entire tree)
    std::vector<int> pids;
    if (popup_show_tree_) {
        collect_tree_pids(node, pids);
    } else {
        pids.push_back(popup_pid_);
    }

    // Aggregate CPU times from all processes
    uint64_t total_utime = 0, total_stime = 0;
    float total_mem_pct = 0.0f;

    for (int pid : pids) {
        uint64_t stime = 0;
        if (uint64_t utime = 0; read_cpu_times(pid, utime, stime)) {
            total_utime += utime;
            total_stime += stime;
        }

        // Aggregate memory
        if (auto proc_it = current_data_->process_map.find(pid); proc_it != current_data_->process_map.end()) {
            total_mem_pct += proc_it->second->info.memory_percent;
        }
    }

    // Calculate CPU percentages
    if (popup_prev_utime_ > 0) {
        const uint64_t user_delta = total_utime - popup_prev_utime_;
        const uint64_t kernel_delta = total_stime - popup_prev_stime_;

        // Use actual system tick rate and CPU count for accurate percentage
        const auto& sys = SystemInfo::instance();
        const long ticks_per_sec = sys.get_clock_ticks_per_second();
        const unsigned int cpu_count = sys.get_processor_count();

        // CPU time is in ticks. Convert delta ticks to percentage of total system capacity.
        // Formula: ((delta_ticks / ticks_per_sec) / elapsed_sec) / cpu_count * 100
        const float elapsed_sec = elapsed / 1000.0f;
        const float ticks_in_period = ticks_per_sec * elapsed_sec;

        float user_pct = 0.0f, kernel_pct = 0.0f;
        if (ticks_in_period > 0) {
            const float pct_of_one_cpu_user = (static_cast<float>(user_delta) / ticks_in_period) * 100.0f;
            const float pct_of_one_cpu_kernel = (static_cast<float>(kernel_delta) / ticks_in_period) * 100.0f;
            user_pct = pct_of_one_cpu_user / static_cast<float>(cpu_count);
            kernel_pct = pct_of_one_cpu_kernel / static_cast<float>(cpu_count);
        }

        popup_cpu_user_history_.push_back(user_pct);
        popup_cpu_kernel_history_.push_back(kernel_pct);

        if (popup_cpu_user_history_.size() > kHistorySize) {
            popup_cpu_user_history_.erase(popup_cpu_user_history_.begin());
            popup_cpu_kernel_history_.erase(popup_cpu_kernel_history_.begin());
        }
    }
    popup_prev_utime_ = total_utime;
    popup_prev_stime_ = total_stime;

    // Memory percentage (aggregated)
    popup_memory_history_.push_back(total_mem_pct);
    if (popup_memory_history_.size() > kHistorySize) {
        popup_memory_history_.erase(popup_memory_history_.begin());
    }

    // Per-CPU usage from system-wide data (not process-specific)
    // Note: Getting per-process per-CPU usage would require parsing /proc/<pid>/task/*/stat
    // for each thread and tracking which CPU it ran on - complex and high overhead
    const size_t cpu_count = current_data_->per_cpu_usage.size();
    if (popup_per_cpu_user_history_.size() != cpu_count) {
        popup_per_cpu_user_history_.resize(cpu_count);
        popup_per_cpu_kernel_history_.resize(cpu_count);
    }

    for (size_t i = 0; i < cpu_count; i++) {
        // Use the user and system percentages from the snapshot
        const auto user_pct = i < current_data_->per_cpu_user.size()
            ? static_cast<float>(current_data_->per_cpu_user[i]) : 0.0f;
        const auto system_pct = i < current_data_->per_cpu_system.size()
            ? static_cast<float>(current_data_->per_cpu_system[i]) : 0.0f;

        popup_per_cpu_user_history_[i].push_back(user_pct);
        popup_per_cpu_kernel_history_[i].push_back(system_pct);

        if (popup_per_cpu_user_history_[i].size() > kHistorySize) {
            popup_per_cpu_user_history_[i].erase(popup_per_cpu_user_history_[i].begin());
            popup_per_cpu_kernel_history_[i].erase(popup_per_cpu_kernel_history_[i].begin());
        }
    }
}

void App::render_process_popup() {
    if (!show_process_popup_) return;

    update_popup_history();

    // Find process name
    std::string title = "Process Details";
    if (current_data_) {
        if (const auto it = current_data_->process_map.find(popup_pid_); it != current_data_->process_map.end()) {
            title = std::format("{} (PID: {})", it->second->info.name, popup_pid_);
        }
    }

    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(title.c_str(), &show_process_popup_, ImGuiWindowFlags_NoCollapse)) {
        // Handle ESC key to close popup
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            show_process_popup_ = false;
            ImGui::End();
            return;
        }

        // Toggle for process only vs process tree
        if (ImGui::Checkbox("Include descendants (process tree)", &popup_show_tree_)) {
            // Clear history when toggling to get fresh data
            popup_cpu_user_history_.clear();
            popup_cpu_kernel_history_.clear();
            popup_memory_history_.clear();
            popup_prev_utime_ = 0;
            popup_prev_stime_ = 0;
        }
        if (popup_show_tree_) {
            // Count descendants
            if (current_data_) {
                const auto it = current_data_->process_map.find(popup_pid_);
                if (it != current_data_->process_map.end()) {
                    std::vector<int> pids;
                    collect_tree_pids(it->second, pids);
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%zu processes)", pids.size());
                }
            }
        }
        ImGui::Separator();

        // Combined CPU & Memory chart
        float cur_user = popup_cpu_user_history_.empty() ? 0.0f : popup_cpu_user_history_.back();
        float cur_kernel = popup_cpu_kernel_history_.empty() ? 0.0f : popup_cpu_kernel_history_.back();
        float cur_mem = popup_memory_history_.empty() ? 0.0f : popup_memory_history_.back();
        const std::string chart_label = std::format("{}: User {:.1f}% / Kernel {:.1f}% / Mem {:.1f}%",
            popup_show_tree_ ? "Tree" : "Process", cur_user, cur_kernel, cur_mem);
        if (ImGui::CollapsingHeader(chart_label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            constexpr float chart_height = 100;
            const ImVec2 chart_size(ImGui::GetContentRegionAvail().x, chart_height);

            if (!popup_cpu_user_history_.empty()) {
                const ImVec2 start_pos = ImGui::GetCursorPos();

                // Draw CPU User (blue)
                ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                ImGui::PlotLines("##cpu_user", popup_cpu_user_history_.data(),
                    static_cast<int>(popup_cpu_user_history_.size()), 0, nullptr,
                    0.0f, 100.0f, chart_size);
                ImGui::PopStyleColor(2);

                // Overlay CPU Kernel (red)
                ImGui::SetCursorPos(start_pos);
                ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PlotLines("##cpu_kernel", popup_cpu_kernel_history_.data(),
                    static_cast<int>(popup_cpu_kernel_history_.size()), 0, nullptr,
                    0.0f, 100.0f, chart_size);
                ImGui::PopStyleColor(2);

                // Overlay Memory (green)
                ImGui::SetCursorPos(start_pos);
                ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PlotLines("##memory", popup_memory_history_.data(),
                    static_cast<int>(popup_memory_history_.size()), 0, nullptr,
                    0.0f, 100.0f, chart_size);
                ImGui::PopStyleColor(2);

                // Move cursor past the chart
                ImGui::SetCursorPos(ImVec2(start_pos.x, start_pos.y + chart_height + 4));
            } else {
                ImGui::Text("Collecting data...");
            }
        }

        // Per-CPU Usage (System-wide context, not process-specific)
        if (ImGui::CollapsingHeader("System CPU Usage (context, not process-specific)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Shows system-wide per-CPU load: User (blue) + Kernel (red)");
            const int cpu_count = static_cast<int>(popup_per_cpu_user_history_.size());

            if (const int cols = std::min(4, cpu_count); cpu_count > 0 && ImGui::BeginTable("CPUCharts", cols)) {
                for (int i = 0; i < cpu_count; i++) {
                    if (i % cols == 0) ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    const float user_pct = popup_per_cpu_user_history_[i].empty() ? 0.0f : popup_per_cpu_user_history_[i].back();
                    const float kernel_pct = popup_per_cpu_kernel_history_[i].empty() ? 0.0f : popup_per_cpu_kernel_history_[i].back();
                    const float total_pct = user_pct + kernel_pct;
                    ImGui::Text("CPU %d: %.1f%% (U:%.1f%% K:%.1f%%)", i, total_pct, user_pct, kernel_pct);

                    if (!popup_per_cpu_user_history_[i].empty()) {
                        constexpr float chart_height = 50;
                        const ImVec2 chart_sz(ImGui::GetContentRegionAvail().x - 5, chart_height);
                        const ImVec2 start_pos = ImGui::GetCursorPos();

                        // User CPU (blue) - base layer
                        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                        ImGui::PlotLines(("##cpu_user_" + std::to_string(i)).c_str(),
                            popup_per_cpu_user_history_[i].data(),
                            static_cast<int>(popup_per_cpu_user_history_[i].size()),
                            0, nullptr, 0.0f, 100.0f, chart_sz);
                        ImGui::PopStyleColor(2);

                        // Overlay Kernel CPU (red) - stacked on top of user
                        // Create stacked values (user + kernel)
                        std::vector<float> stacked;
                        stacked.reserve(popup_per_cpu_user_history_[i].size());
                        for (size_t j = 0; j < popup_per_cpu_user_history_[i].size(); j++) {
                            stacked.push_back(popup_per_cpu_user_history_[i][j] + popup_per_cpu_kernel_history_[i][j]);
                        }
                        ImGui::SetCursorPos(start_pos);
                        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                        ImGui::PlotLines(("##cpu_total_" + std::to_string(i)).c_str(),
                            stacked.data(),
                            static_cast<int>(stacked.size()),
                            0, nullptr, 0.0f, 100.0f, chart_sz);
                        ImGui::PopStyleColor(2);

                        // Move cursor past the chart
                        ImGui::SetCursorPos(ImVec2(start_pos.x, start_pos.y + chart_height + 4));
                    }
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::End();
}

} // namespace pex
