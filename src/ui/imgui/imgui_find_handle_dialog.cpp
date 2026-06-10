#include "imgui_app.hpp"
#include "imgui.h"
#include "../../platform/platform_factory.hpp"
#include <algorithm>
#include <cctype>
#include <format>

namespace pex {

namespace {

std::string lower_copy(const std::string& s) {
    std::string result = s;
    std::ranges::transform(result, result.begin(),
                           [](unsigned char c) { return std::tolower(c); });
    return result;
}

} // namespace

void ImGuiApp::stop_handle_search() {
    find_cancel_ = true;
    if (find_thread_.joinable()) {
        find_thread_.join();
    }
    find_cancel_ = false;
}

void ImGuiApp::start_handle_search() {
    if (!current_data_) return;
    stop_handle_search();  // Join any previous worker

    const std::string query = lower_copy(find_query_);
    if (query.empty()) return;

    if (!find_provider_) {
        find_provider_ = make_details_data_provider();
    }

    // Snapshot the PID list on the UI thread; the worker must not touch
    // current_data_ (it is replaced by the UI thread every tick).
    std::vector<std::pair<int, std::string>> targets;
    targets.reserve(current_data_->process_map.size());
    for (const auto& [pid, node] : current_data_->process_map) {
        targets.emplace_back(pid, node->info.name);
    }
    std::ranges::sort(targets);

    {
        std::lock_guard lock(find_mutex_);
        find_results_.clear();
        find_status_ = "Scanning...";
    }
    find_selected_row_ = -1;
    find_running_ = true;

    find_thread_ = std::thread([this, query, targets = std::move(targets)]() {
        size_t scanned = 0;
        size_t total_matches = 0;

        for (const auto& [pid, name] : targets) {
            if (find_cancel_.load()) break;

            std::vector<HandleSearchResult> batch;

            for (const auto& fh : find_provider_->get_file_handles(pid)) {
                if (lower_copy(fh.path).find(query) != std::string::npos) {
                    batch.push_back({pid, name, fh.type, fh.path});
                }
            }
            for (const auto& lib : find_provider_->get_libraries(pid)) {
                if (lower_copy(lib.path).find(query) != std::string::npos) {
                    batch.push_back({pid, name, "library", lib.path});
                }
            }

            scanned++;
            {
                std::lock_guard lock(find_mutex_);
                for (auto& r : batch) {
                    if (find_results_.size() >= kMaxFindResults) break;
                    find_results_.push_back(std::move(r));
                }
                total_matches = find_results_.size();
                find_status_ = std::format("Scanning... {}/{} processes, {} matches",
                                           scanned, targets.size(), total_matches);
                if (find_results_.size() >= kMaxFindResults) {
                    find_status_ = std::format("Stopped at {} matches (refine the query)", kMaxFindResults);
                    break;
                }
            }

            if (scanned % 10 == 0) {
                post_empty_event_debounced();
            }
        }

        {
            std::lock_guard lock(find_mutex_);
            if (find_cancel_.load()) {
                find_status_ = std::format("Cancelled: {} matches in {} scanned processes",
                                           total_matches, scanned);
            } else if (find_results_.size() < kMaxFindResults) {
                find_status_ = std::format("Done: {} matches in {} processes",
                                           total_matches, scanned);
            }
        }
        find_running_ = false;
        post_empty_event_debounced();
    });
}

void ImGuiApp::render_find_handle_dialog() {
    if (!find_dialog_visible_) return;

    ImGui::SetNextWindowSize(ImVec2(820, 480), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Find Open File / Handle", &find_dialog_visible_)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            find_dialog_visible_ = false;
            ImGui::End();
            return;
        }

        if (find_focus_input_) {
            ImGui::SetKeyboardFocusHere();
            find_focus_input_ = false;
        }
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 220);
        const bool enter = ImGui::InputTextWithHint("##find_query",
            "path substring, e.g. .log, /var/lib, socket:", find_query_, sizeof(find_query_),
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();

        const bool running = find_running_.load();
        if (running) {
            if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                find_cancel_ = true;
            }
        } else {
            if ((ImGui::Button("Search", ImVec2(100, 0)) || enter) && find_query_[0] != '\0') {
                start_handle_search();
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Searches open file descriptors and mapped libraries of every\n"
                              "process for a case-insensitive path substring.\n"
                              "Other users' processes need elevated privileges to inspect.");
        }

        {
            std::lock_guard lock(find_mutex_);
            if (!find_status_.empty()) {
                ImGui::TextDisabled("%s", find_status_.c_str());
            }
        }

        if (ImGui::BeginTable("FindResults", 4,
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Process", ImGuiTableColumnFlags_WidthFixed, 180);
            ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            std::lock_guard lock(find_mutex_);
            for (int i = 0; i < static_cast<int>(find_results_.size()); i++) {
                const auto& r = find_results_[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                if (ImGui::Selectable(r.process_name.c_str(), find_selected_row_ == i,
                        ImGuiSelectableFlags_SpanAllColumns)) {
                    find_selected_row_ = i;
                    // Jump to the process in the main view
                    view_model_.process_list.selected_pid = r.pid;
                    expand_ancestors(r.pid);
                    view_model_.process_list.scroll_to_selected = true;
                    refresh_selected_details();
                }

                ImGui::TableNextColumn();
                ImGui::Text("%d", r.pid);
                ImGui::TableNextColumn();
                ImGui::Text("%s", r.type.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", r.path.c_str());

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

} // namespace pex
