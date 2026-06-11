#include "imgui_app.hpp"
#include "imgui.h"
#include "../../core/format_utils.hpp"
#include <algorithm>
#include <format>

namespace pex {

namespace {

constexpr const char* kMetricNames[] = {"CPU", "Memory", "I/O Read", "I/O Write"};
constexpr const char* kWindowNames[] = {"Last 1 min", "Last 5 min", "All recorded"};

HistoryMetric to_metric(const int idx) {
    switch (idx) {
        case 1: return HistoryMetric::Memory;
        case 2: return HistoryMetric::IoRead;
        case 3: return HistoryMetric::IoWrite;
        default: return HistoryMetric::Cpu;
    }
}

float metric_avg(const ProcessAggregate& a, const int idx) {
    switch (idx) {
        case 1: return a.avg_mem;
        case 2: return a.avg_io_read;
        case 3: return a.avg_io_write;
        default: return a.avg_cpu;
    }
}

float metric_peak(const ProcessAggregate& a, const int idx) {
    switch (idx) {
        case 1: return a.peak_mem;
        case 2: return a.peak_io_read;
        case 3: return a.peak_io_write;
        default: return a.peak_cpu;
    }
}

std::string format_metric(const float value, const int idx) {
    if (idx <= 1) {
        return std::format("{:.1f}%", value);
    }
    if (value < 1.0f) return "-";
    return format_bytes(static_cast<int64_t>(value), false) + "/s";
}

} // namespace

void ImGuiApp::render_history_view() {
    if (!history_view_visible_ || !history_) return;

    ImGui::SetNextWindowSize(ImVec2(760, 540), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("History - Top Consumers", &history_view_visible_)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            history_view_visible_ = false;
            ImGui::End();
            return;
        }

        // Controls
        ImGui::SetNextItemWidth(140);
        ImGui::Combo("Metric", &history_metric_idx_, kMetricNames, IM_ARRAYSIZE(kMetricNames));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        ImGui::Combo("Window", &history_window_idx_, kWindowNames, IM_ARRAYSIZE(kWindowNames));
        ImGui::SameLine();
        {
            const int refresh_ms = data_store_->get_refresh_interval();
            const size_t recorded = history_->sample_count();
            const double span_sec = static_cast<double>(recorded) * refresh_ms / 1000.0;
            ImGui::TextDisabled("(%zu ticks recorded, ~%.0f s span)", recorded, span_sec);
        }

        // Window selection -> tick count
        const int refresh_ms = std::max(1, data_store_->get_refresh_interval());
        size_t window_ticks = SIZE_MAX;  // All recorded
        if (history_window_idx_ == 0) window_ticks = static_cast<size_t>(60'000 / refresh_ms);
        else if (history_window_idx_ == 1) window_ticks = static_cast<size_t>(300'000 / refresh_ms);

        // Refresh the cached aggregation on parameter change or once per second
        const auto now = std::chrono::steady_clock::now();
        const bool params_changed = history_cache_metric_ != history_metric_idx_ ||
                                    history_cache_window_ != history_window_idx_;
        if (params_changed || now - history_cache_time_ >= std::chrono::seconds(1)) {
            history_aggregates_ = history_->aggregate(window_ticks);

            const int metric = history_metric_idx_;
            std::ranges::sort(history_aggregates_, [metric](const auto& a, const auto& b) {
                return metric_avg(a, metric) > metric_avg(b, metric);
            });
            if (history_aggregates_.size() > kHistoryTopN) {
                history_aggregates_.resize(kHistoryTopN);
            }

            history_sparklines_.clear();
            for (const auto& agg : history_aggregates_) {
                history_sparklines_[agg.pid] =
                    history_->get_metric_series(agg.pid, to_metric(metric), window_ticks);
            }

            history_cache_time_ = now;
            history_cache_metric_ = history_metric_idx_;
            history_cache_window_ = history_window_idx_;
        }

        ImGui::Spacing();

        if (ImGui::BeginTable("TopConsumers", 6,
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Process", ImGuiTableColumnFlags_WidthFixed, 170);
            ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("Trend", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Avg", ImGuiTableColumnFlags_WidthFixed, 85);
            ImGui::TableSetupColumn("Peak", ImGuiTableColumnFlags_WidthFixed, 85);
            ImGui::TableSetupColumn("Seen", ImGuiTableColumnFlags_WidthFixed, 55);
            ImGui::TableHeadersRow();

            const size_t total_window = std::min(window_ticks == SIZE_MAX
                ? history_->sample_count() : window_ticks, history_->sample_count());

            for (const auto& agg : history_aggregates_) {
                const bool alive = current_data_ && current_data_->process_map.contains(agg.pid);

                ImGui::PushID(agg.pid);
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                if (!alive) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                const std::string label = agg.name.empty() ? std::format("(pid {})", agg.pid)
                                                           : agg.name;
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)
                        && alive) {
                    // Jump to the process in the main view
                    view_model_.process_list.selected_pid = agg.pid;
                    expand_ancestors(agg.pid);
                    view_model_.process_list.scroll_to_selected = true;
                    refresh_selected_details();
                }
                if (!alive) {
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Process has exited");
                }

                ImGui::TableNextColumn();
                ImGui::Text("%d", agg.pid);

                ImGui::TableNextColumn();
                if (const auto it = history_sparklines_.find(agg.pid);
                        it != history_sparklines_.end() && !it->second.empty()) {
                    const float peak = metric_peak(agg, history_metric_idx_);
                    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.2f));
                    ImGui::PlotLines("##spark", it->second.data(),
                                     static_cast<int>(it->second.size()), 0, nullptr,
                                     0.0f, std::max(peak, 0.001f),
                                     ImVec2(ImGui::GetContentRegionAvail().x, 26));
                    ImGui::PopStyleColor(2);
                }

                ImGui::TableNextColumn();
                ImGui::Text("%s", format_metric(metric_avg(agg, history_metric_idx_), history_metric_idx_).c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%s", format_metric(metric_peak(agg, history_metric_idx_), history_metric_idx_).c_str());

                ImGui::TableNextColumn();
                if (total_window > 0) {
                    ImGui::Text("%.0f%%", 100.0 * static_cast<double>(agg.present_ticks)
                                              / static_cast<double>(total_window));
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

} // namespace pex
