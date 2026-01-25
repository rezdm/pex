#include "imgui_app.hpp"
#include "imgui.h"
#include <format>
#include <algorithm>

namespace pex {

void ImGuiApp::render_system_panel() const {
    if (!current_data_) return;

    if (!view_model_.system_panel.is_visible) {
        return;
    }

    // Compact bytes format like htop
    auto format_compact = [](int64_t bytes) -> std::string {
        if (bytes < 1024) return std::format("{}B", bytes);
        if (bytes < 1024 * 1024) return std::format("{:.0f}K", bytes / 1024.0);
        if (bytes < 1024LL * 1024 * 1024) return std::format("{:.2f}G", bytes / (1024.0 * 1024 * 1024));
        return std::format("{:.2f}G", bytes / (1024.0 * 1024 * 1024));
    };

    const auto& mem_info_used = current_data_->memory_used;
    const auto& mem_info_total = current_data_->memory_total;
    const auto& swap_info = current_data_->swap_info;
    const auto& load = current_data_->load_average;
    const auto&[uptime_seconds, idle_seconds] = current_data_->uptime_info;
    const auto& per_cpu_usage = current_data_->per_cpu_usage;

    const int cpu_count = static_cast<int>(per_cpu_usage.size());

    const float available_width = ImGui::GetContentRegionAvail().x;
    float stats_width = 350.0f;
    float cpu_width = available_width - stats_width - 10.0f;

    if (cpu_width < 200.0f) {
        cpu_width = available_width;
        stats_width = available_width;
    }

    constexpr float cpu_item_width = 120.0f;
    const int cpu_cols = std::max(1, static_cast<int>(cpu_width / cpu_item_width));
    const float text_height = ImGui::GetTextLineHeight();

    auto draw_bar = [&](const float ratio, const float width, const ImVec4& color) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::ProgressBar(ratio, ImVec2(width, text_height), "");
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
    };

    if ((available_width - stats_width - 10.0f) >= 200.0f) {
        if (ImGui::BeginTable("SystemPanelLayout", 2, ImGuiTableFlags_None)) {
            ImGui::TableSetupColumn("CPUs", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Stats", ImGuiTableColumnFlags_WidthFixed, stats_width);

            ImGui::TableNextRow();

            // Left column - CPUs
            ImGui::TableNextColumn();
            if (ImGui::BeginTable("CPUGrid", cpu_cols, ImGuiTableFlags_None)) {
                for (int i = 0; i < cpu_count; i++) {
                    if (i % cpu_cols == 0) ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    const double usage = per_cpu_usage[i];
                    ImVec4 bar_color;
                    if (usage < 25.0) bar_color = ImVec4(0.0f, 0.8f, 0.0f, 1.0f);
                    else if (usage < 50.0) bar_color = ImVec4(0.5f, 0.8f, 0.0f, 1.0f);
                    else if (usage < 75.0) bar_color = ImVec4(0.8f, 0.8f, 0.0f, 1.0f);
                    else bar_color = ImVec4(0.8f, 0.2f, 0.0f, 1.0f);

                    ImGui::Text("%2d[", i);
                    ImGui::SameLine(0, 0);
                    draw_bar(static_cast<float>(usage / 100.0), 40, bar_color);
                    ImGui::SameLine(0, 0);
                    ImGui::Text("]%5.1f%%", usage);
                }
                ImGui::EndTable();
            }

            // Right column - Stats
            ImGui::TableNextColumn();

            {
                const float mem_ratio = mem_info_total > 0 ? static_cast<float>(mem_info_used) / static_cast<float>(mem_info_total) : 0.0f;
                ImGui::Text("Mem[");
                ImGui::SameLine(0, 0);
                draw_bar(mem_ratio, 120, ImVec4(0.0f, 0.6f, 0.0f, 1.0f));
                ImGui::SameLine(0, 0);
                ImGui::Text("] %s/%s", format_compact(mem_info_used).c_str(), format_compact(mem_info_total).c_str());
            }

            {
                const float swap_ratio = swap_info.total > 0 ? static_cast<float>(swap_info.used) / static_cast<float>(swap_info.total) : 0.0f;
                ImGui::Text("Swp[");
                ImGui::SameLine(0, 0);
                draw_bar(swap_ratio, 120, ImVec4(0.6f, 0.0f, 0.0f, 1.0f));
                ImGui::SameLine(0, 0);
                ImGui::Text("] %s/%s", format_compact(swap_info.used).c_str(), format_compact(swap_info.total).c_str());
            }

            ImGui::Text("Tasks: %d, %d thr; %d running",
                current_data_->process_count, current_data_->thread_count, current_data_->running_count);

            ImGui::Text("Load average: %.2f %.2f %.2f", load.one_min, load.five_min, load.fifteen_min);

            uint64_t secs = uptime_seconds;
            const uint64_t days = secs / 86400;
            secs %= 86400;
            const uint64_t hours = secs / 3600;
            secs %= 3600;
            const uint64_t mins = secs / 60;
            secs %= 60;
            if (days > 0) {
                ImGui::Text("Uptime: %lu day%s, %02lu:%02lu:%02lu", days, days > 1 ? "s" : "", hours, mins, secs);
            } else {
                ImGui::Text("Uptime: %02lu:%02lu:%02lu", hours, mins, secs);
            }

            ImGui::EndTable();
        }
    } else {
        {
            const float mem_ratio = mem_info_total > 0 ? static_cast<float>(mem_info_used) / static_cast<float>(mem_info_total) : 0.0f;
            ImGui::Text("Mem[");
            ImGui::SameLine(0, 0);
            draw_bar(mem_ratio, 80, ImVec4(0.0f, 0.6f, 0.0f, 1.0f));
            ImGui::SameLine(0, 0);
            ImGui::Text("]%s/%s", format_compact(mem_info_used).c_str(), format_compact(mem_info_total).c_str());
            ImGui::SameLine();
            ImGui::Text("Tasks:%d Load:%.1f", current_data_->process_count, load.one_min);
        }

        if (ImGui::BeginTable("CPUGrid", cpu_cols, ImGuiTableFlags_None)) {
            for (int i = 0; i < cpu_count; i++) {
                if (i % cpu_cols == 0) ImGui::TableNextRow();
                ImGui::TableNextColumn();

                const double usage = per_cpu_usage[i];
                ImVec4 bar_color;
                if (usage < 25.0) bar_color = ImVec4(0.0f, 0.8f, 0.0f, 1.0f);
                else if (usage < 50.0) bar_color = ImVec4(0.5f, 0.8f, 0.0f, 1.0f);
                else if (usage < 75.0) bar_color = ImVec4(0.8f, 0.8f, 0.0f, 1.0f);
                else bar_color = ImVec4(0.8f, 0.2f, 0.0f, 1.0f);

                ImGui::Text("%2d[", i);
                ImGui::SameLine(0, 0);
                draw_bar(static_cast<float>(usage / 100.0), 30, bar_color);
                ImGui::SameLine(0, 0);
                ImGui::Text("]%4.0f%%", usage);
            }
            ImGui::EndTable();
        }
    }

    ImGui::Separator();
}

} // namespace pex
