#include "imgui_app.hpp"
#include "imgui.h"
#include "../../core/services/history_store.hpp"
#include <format>
#include <algorithm>

namespace pex {

void ImGuiApp::collect_tree_pids(const ProcessNode* node, std::vector<int>& pids) {
    if (!node) return;
    pids.push_back(node->info.pid);
    for (const auto& child : node->children) {
        collect_tree_pids(child.get(), pids);
    }
}

void ImGuiApp::update_popup_history() {
    auto& pp = view_model_.process_popup;
    if (!pp.is_visible || pp.target_pid <= 0 || !current_data_) return;

    // Seed the charts from recorded history so the past is visible the moment
    // the popup opens (issue #9). Live sampling below then appends to it.
    if (pp.needs_backfill) {
        pp.needs_backfill = false;
        if (history_) {
            std::vector<int> backfill_pids;
            if (const auto it = current_data_->process_map.find(pp.target_pid); it != current_data_->process_map.end()) {
                if (pp.include_tree) {
                    collect_tree_pids(it->second, backfill_pids);
                } else {
                    backfill_pids.push_back(pp.target_pid);
                }
            }
            if (!backfill_pids.empty()) {
                auto series = history_->get_series(backfill_pids, ProcessPopupViewModel::kHistorySize);
                pp.cpu_user_history = std::move(series.cpu_user);
                pp.cpu_kernel_history = std::move(series.cpu_kernel);
                pp.memory_history = std::move(series.memory_percent);
                pp.per_cpu_user_history = std::move(series.per_cpu_user);
                pp.per_cpu_kernel_history = std::move(series.per_cpu_kernel);
            }
        }
    }

    // Append one sample per *new* snapshot only. current_data_ changes once per
    // refresh interval; a fixed wall-clock poll sampled the same snapshot twice
    // (a 0% reading) or split one tick's counter delta over half the elapsed
    // time (a ~2x spike). Consuming the already-correct per-process percentages
    // (computed once per tick by DataStore with the right elapsed time) avoids
    // re-deriving deltas with an unrelated denominator entirely (issue #85).
    if (current_data_->timestamp == pp.last_sampled_time) return;
    pp.last_sampled_time = current_data_->timestamp;

    const auto it = current_data_->process_map.find(pp.target_pid);
    if (it == current_data_->process_map.end()) return;

    const auto* node = it->second;

    std::vector<int> pids;
    if (pp.include_tree) {
        collect_tree_pids(node, pids);
    } else {
        pids.push_back(pp.target_pid);
    }

    // Sum the already-computed shares (of all cores) across the pid set.
    float user_pct = 0.0f, kernel_pct = 0.0f, mem_pct = 0.0f;
    for (int pid : pids) {
        if (auto proc_it = current_data_->process_map.find(pid); proc_it != current_data_->process_map.end()) {
            user_pct += static_cast<float>(proc_it->second->info.cpu_user_percent);
            kernel_pct += static_cast<float>(proc_it->second->info.cpu_kernel_percent);
            mem_pct += static_cast<float>(proc_it->second->info.memory_percent);
        }
    }

    pp.cpu_user_history.push_back(user_pct);
    pp.cpu_kernel_history.push_back(kernel_pct);
    if (pp.cpu_user_history.size() > ProcessPopupViewModel::kHistorySize) {
        pp.cpu_user_history.erase(pp.cpu_user_history.begin());
        pp.cpu_kernel_history.erase(pp.cpu_kernel_history.begin());
    }

    pp.memory_history.push_back(mem_pct);
    if (pp.memory_history.size() > ProcessPopupViewModel::kHistorySize) {
        pp.memory_history.erase(pp.memory_history.begin());
    }

    const size_t cpu_count = current_data_->per_cpu_usage.size();
    if (pp.per_cpu_user_history.size() != cpu_count) {
        pp.per_cpu_user_history.resize(cpu_count);
        pp.per_cpu_kernel_history.resize(cpu_count);
    }

    for (size_t i = 0; i < cpu_count; i++) {
        const auto user_pct = i < current_data_->per_cpu_user.size()
            ? static_cast<float>(current_data_->per_cpu_user[i]) : 0.0f;
        const auto system_pct = i < current_data_->per_cpu_system.size()
            ? static_cast<float>(current_data_->per_cpu_system[i]) : 0.0f;

        pp.per_cpu_user_history[i].push_back(user_pct);
        pp.per_cpu_kernel_history[i].push_back(system_pct);

        if (pp.per_cpu_user_history[i].size() > ProcessPopupViewModel::kHistorySize) {
            pp.per_cpu_user_history[i].erase(pp.per_cpu_user_history[i].begin());
            pp.per_cpu_kernel_history[i].erase(pp.per_cpu_kernel_history[i].begin());
        }
    }
}

void ImGuiApp::render_process_popup() {
    auto& pp = view_model_.process_popup;
    if (!pp.is_visible) return;

    update_popup_history();

    std::string title = "Process Details";
    if (current_data_) {
        if (const auto it = current_data_->process_map.find(pp.target_pid); it != current_data_->process_map.end()) {
            title = std::format("{} (PID: {})", it->second->info.name, pp.target_pid);
        }
    }

    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(title.c_str(), &pp.is_visible, ImGuiWindowFlags_NoCollapse)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            pp.is_visible = false;
            ImGui::End();
            return;
        }

        if (ImGui::Checkbox("Include descendants (process tree)", &pp.include_tree)) {
            pp.clear_history();
        }
        if (pp.include_tree) {
            if (current_data_) {
                const auto it = current_data_->process_map.find(pp.target_pid);
                if (it != current_data_->process_map.end()) {
                    std::vector<int> pids;
                    collect_tree_pids(it->second, pids);
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%zu processes)", pids.size());
                }
            }
        }
        ImGui::Separator();

        float cur_user = pp.cpu_user_history.empty() ? 0.0f : pp.cpu_user_history.back();
        float cur_kernel = pp.cpu_kernel_history.empty() ? 0.0f : pp.cpu_kernel_history.back();
        float cur_mem = pp.memory_history.empty() ? 0.0f : pp.memory_history.back();
        const std::string chart_label = std::format("{}: User {:.1f}% / Kernel {:.1f}% / Mem {:.1f}%",
            pp.include_tree ? "Tree" : "Process", cur_user, cur_kernel, cur_mem);
        if (ImGui::CollapsingHeader(chart_label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            constexpr float chart_height = 100;
            const ImVec2 chart_size(ImGui::GetContentRegionAvail().x, chart_height);

            if (!pp.cpu_user_history.empty()) {
                const ImVec2 start_pos = ImGui::GetCursorPos();

                ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                ImGui::PlotLines("##cpu_user", pp.cpu_user_history.data(),
                    static_cast<int>(pp.cpu_user_history.size()), 0, nullptr,
                    0.0f, 100.0f, chart_size);
                ImGui::PopStyleColor(2);

                ImGui::SetCursorPos(start_pos);
                ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PlotLines("##cpu_kernel", pp.cpu_kernel_history.data(),
                    static_cast<int>(pp.cpu_kernel_history.size()), 0, nullptr,
                    0.0f, 100.0f, chart_size);
                ImGui::PopStyleColor(2);

                ImGui::SetCursorPos(start_pos);
                ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PlotLines("##memory", pp.memory_history.data(),
                    static_cast<int>(pp.memory_history.size()), 0, nullptr,
                    0.0f, 100.0f, chart_size);
                ImGui::PopStyleColor(2);

                ImGui::SetCursorPos(ImVec2(start_pos.x, start_pos.y + chart_height + 4));
            } else {
                ImGui::Text("Collecting data...");
            }
        }

        if (ImGui::CollapsingHeader("System CPU Usage (context, not process-specific)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Shows system-wide per-CPU load: User (blue) + Kernel (red, system+irq+softirq)");
            const int cpu_count = static_cast<int>(pp.per_cpu_user_history.size());

            float max_sample = 0.0f;
            for (size_t i = 0; i < pp.per_cpu_user_history.size(); i++) {
                const auto& user_hist = pp.per_cpu_user_history[i];
                const auto& kernel_hist = pp.per_cpu_kernel_history[i];
                const size_t count = std::min(user_hist.size(), kernel_hist.size());
                for (size_t j = 0; j < count; j++) {
                    const float total = user_hist[j] + kernel_hist[j];
                    if (total > max_sample) max_sample = total;
                }
            }
            const float plot_max = std::max(max_sample + 10.0f, 100.0f);

            if (const int cols = std::min(4, cpu_count); cpu_count > 0 && ImGui::BeginTable("CPUCharts", cols)) {
                for (int i = 0; i < cpu_count; i++) {
                    if (i % cols == 0) ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    const float user_pct = pp.per_cpu_user_history[i].empty() ? 0.0f : pp.per_cpu_user_history[i].back();
                    const float kernel_pct = pp.per_cpu_kernel_history[i].empty() ? 0.0f : pp.per_cpu_kernel_history[i].back();
                    const float total_pct = user_pct + kernel_pct;
                    ImGui::Text("CPU %d: %.1f%% (U:%.1f%% K:%.1f%%)", i, total_pct, user_pct, kernel_pct);

                    if (!pp.per_cpu_user_history[i].empty()) {
                        constexpr float chart_height = 50;
                        const ImVec2 chart_sz(ImGui::GetContentRegionAvail().x - 5, chart_height);
                        const ImVec2 start_pos = ImGui::GetCursorPos();

                        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                        ImGui::PlotLines(("##cpu_user_" + std::to_string(i)).c_str(),
                            pp.per_cpu_user_history[i].data(),
                            static_cast<int>(pp.per_cpu_user_history[i].size()),
                            0, nullptr, 0.0f, plot_max, chart_sz);
                        ImGui::PopStyleColor(2);

                        ImGui::SetCursorPos(start_pos);
                        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                        ImGui::PlotLines(("##cpu_kernel_" + std::to_string(i)).c_str(),
                            pp.per_cpu_kernel_history[i].data(),
                            static_cast<int>(pp.per_cpu_kernel_history[i].size()),
                            0, nullptr, 0.0f, plot_max, chart_sz);
                        ImGui::PopStyleColor(2);

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
