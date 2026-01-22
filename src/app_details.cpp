#include "app.hpp"
#include "imgui.h"
#include <fstream>
#include <sstream>
#include <format>
#include <algorithm>
#include <charconv>

namespace pex {

void App::render_details_panel() {
    if (ImGui::BeginTabBar("DetailsTabs")) {
        if (ImGui::BeginTabItem("File Handles")) {
            render_file_handles_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Network")) {
            render_network_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Threads")) {
            render_threads_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Memory")) {
            render_memory_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Environment")) {
            render_environment_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Libraries")) {
            render_libraries_tab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void App::render_file_handles_tab() const {
    if (ImGui::BeginTable("FileHandles", 3,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("FD", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const auto& handle : file_handles_) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", handle.fd);
            ImGui::TableNextColumn();
            ImGui::Text("%s", handle.type.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", handle.path.c_str());
        }

        ImGui::EndTable();
    }
}

void App::render_network_tab() {
    // Helper to parse IP:port from endpoint string
    auto parse_endpoint = [](const std::string& endpoint) -> std::pair<std::string, uint16_t> {
        // Format is "ip:port" or "[ipv6]:port"
        size_t colon_pos = endpoint.rfind(':');
        if (colon_pos == std::string::npos) return {endpoint, 0};

        std::string ip = endpoint.substr(0, colon_pos);
        uint16_t port = 0;
        std::from_chars(endpoint.data() + colon_pos + 1,
                       endpoint.data() + endpoint.size(), port);
        return {ip, port};
    };

    if (ImGui::BeginTable("Network", 8,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Protocol", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Local Address", ImGuiTableColumnFlags_WidthFixed, 160);
        ImGui::TableSetupColumn("Local Host", ImGuiTableColumnFlags_WidthFixed, 140);
        ImGui::TableSetupColumn("Local Port", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Remote Address", ImGuiTableColumnFlags_WidthFixed, 160);
        ImGui::TableSetupColumn("Remote Host", ImGuiTableColumnFlags_WidthFixed, 140);
        ImGui::TableSetupColumn("Remote Port", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        // Get base protocol (tcp/udp without the 6)
        auto base_protocol = [](const std::string& proto) -> std::string {
            if (proto.starts_with("tcp")) return "tcp";
            if (proto.starts_with("udp")) return "udp";
            return proto;
        };

        for (const auto& conn : network_connections_) {
            auto [local_ip, local_port] = parse_endpoint(conn.local_endpoint);
            auto [remote_ip, remote_port] = parse_endpoint(conn.remote_endpoint);

            // Get resolved names (triggers async resolution if not cached)
            std::string local_host = name_resolver_.get_hostname(local_ip);
            std::string remote_host = name_resolver_.get_hostname(remote_ip);

            // Get service names
            std::string proto_base = base_protocol(conn.protocol);
            std::string local_service = name_resolver_.get_service_name(local_port, proto_base);
            std::string remote_service = name_resolver_.get_service_name(remote_port, proto_base);

            ImGui::TableNextRow();

            // Protocol
            ImGui::TableNextColumn();
            ImGui::Text("%s", conn.protocol.c_str());

            // Local Address
            ImGui::TableNextColumn();
            ImGui::Text("%s", conn.local_endpoint.c_str());

            // Local Host (resolved)
            ImGui::TableNextColumn();
            if (!local_host.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "%s", local_host.c_str());
            } else {
                ImGui::TextDisabled("-");
            }

            // Local Port (service name)
            ImGui::TableNextColumn();
            if (!local_service.empty()) {
                ImGui::Text("%s", local_service.c_str());
            } else {
                ImGui::Text("%d", local_port);
            }

            // Remote Address
            ImGui::TableNextColumn();
            ImGui::Text("%s", conn.remote_endpoint.c_str());

            // Remote Host (resolved)
            ImGui::TableNextColumn();
            if (!remote_host.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "%s", remote_host.c_str());
            } else {
                ImGui::TextDisabled("-");
            }

            // Remote Port (service name)
            ImGui::TableNextColumn();
            if (!remote_service.empty()) {
                ImGui::Text("%s", remote_service.c_str());
            } else {
                ImGui::Text("%d", remote_port);
            }

            // State
            ImGui::TableNextColumn();
            ImGui::Text("%s", conn.state.c_str());
        }

        ImGui::EndTable();
    }
}

void App::render_threads_tab() {
    // Split view: threads list on left, stack on right
    const float width = ImGui::GetContentRegionAvail().x;

    ImGui::BeginChild("ThreadsList", ImVec2(width * 0.5f, 0), true);
    if (ImGui::BeginTable("Threads", 6,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("TID", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Pri", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Current Library", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(threads_.size()); i++) {
            const auto& thread = threads_[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            const bool is_selected = (i == selected_thread_idx_);
            if (ImGui::Selectable("##row", is_selected,
                    ImGuiSelectableFlags_SpanAllColumns)) {
                selected_thread_idx_ = i;
            }
            ImGui::SameLine();
            ImGui::Text("%d", thread.tid);

            ImGui::TableNextColumn();
            ImGui::Text("%s", thread.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%c", thread.state);
            ImGui::TableNextColumn();
            ImGui::Text("%d", thread.priority);
            ImGui::TableNextColumn();
            ImGui::Text("%d", thread.processor);
            ImGui::TableNextColumn();
            ImGui::Text("%s", thread.current_library.c_str());
            ImGui::PopID();
        }

        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Stack trace panel
    ImGui::BeginChild("StackTrace", ImVec2(0, 0), true);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));

    std::string stack_text;
    if (selected_thread_idx_ >= 0 && selected_thread_idx_ < static_cast<int>(threads_.size())) {
        stack_text = threads_[selected_thread_idx_].stack;
    }

    ImGui::InputTextMultiline("##stack", stack_text.data(), stack_text.size() + 1,
        ImGui::GetContentRegionAvail(), ImGuiInputTextFlags_ReadOnly);

    ImGui::PopStyleColor();
    ImGui::EndChild();
}

void App::render_memory_tab() const {
    if (ImGui::BeginTable("Memory", 4,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Address Range", ImGuiTableColumnFlags_WidthFixed, 280);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Perms", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Pathname", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const auto& map : memory_maps_) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", map.address.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", map.size.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", map.permissions.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", map.pathname.c_str());
        }

        ImGui::EndTable();
    }
}

void App::render_environment_tab() const {
    if (ImGui::BeginTable("Environment", 2,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
            ImGuiTableFlags_Sortable)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 250);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const auto& var : environment_vars_) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", var.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", var.value.c_str());
        }

        ImGui::EndTable();
    }
}

void App::render_libraries_tab() const {
    if (ImGui::BeginTable("Libraries", 4,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
            ImGuiTableFlags_Sortable)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 250);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Base Address", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const auto& lib : libraries_) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            // Highlight executable differently
            if (lib.is_executable) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", lib.name.c_str());
            } else {
                ImGui::Text("%s", lib.name.c_str());
            }

            ImGui::TableNextColumn();
            ImGui::Text("%s", format_bytes(lib.total_size).c_str());

            ImGui::TableNextColumn();
            ImGui::Text("0x%s", lib.base_address.c_str());

            ImGui::TableNextColumn();
            ImGui::Text("%s", lib.path.c_str());
        }

        ImGui::EndTable();
    }
}

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

        // Convert to percentage (assuming ~100 ticks per second)
        const float elapsed_sec = elapsed / 1000.0f;
        const float user_pct = (user_delta / (elapsed_sec * 100.0f)) * 100.0f;
        const float kernel_pct = (kernel_delta / (elapsed_sec * 100.0f)) * 100.0f;

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

    // Per-CPU usage from system data
    const size_t cpu_count = current_data_->per_cpu_usage.size();
    if (popup_per_cpu_user_history_.size() != cpu_count) {
        popup_per_cpu_user_history_.resize(cpu_count);
        popup_per_cpu_kernel_history_.resize(cpu_count);
    }

    // For per-CPU, we use system-wide per-CPU data (process-specific per-CPU is complex)
    for (size_t i = 0; i < cpu_count; i++) {
        const auto usage = static_cast<float>(current_data_->per_cpu_usage[i]);
        popup_per_cpu_user_history_[i].push_back(usage * 0.7f);  // Approximate user
        popup_per_cpu_kernel_history_[i].push_back(usage * 0.3f);  // Approximate kernel

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

        // Per-CPU Usage (System-wide as context)
        if (ImGui::CollapsingHeader("Per-CPU (System): User (blue), Kernel (red)", ImGuiTreeNodeFlags_DefaultOpen)) {
            const int cpu_count = static_cast<int>(popup_per_cpu_user_history_.size());

            if (const int cols = std::min(4, cpu_count); cpu_count > 0 && ImGui::BeginTable("CPUCharts", cols)) {
                for (int i = 0; i < cpu_count; i++) {
                    if (i % cols == 0) ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    const float cpu_user = popup_per_cpu_user_history_[i].empty() ? 0.0f : popup_per_cpu_user_history_[i].back();
                    const float cpu_kernel = popup_per_cpu_kernel_history_[i].empty() ? 0.0f : popup_per_cpu_kernel_history_[i].back();
                    ImGui::Text("CPU %d: %.1f%%/%.1f%%", i, cpu_user, cpu_kernel);

                    if (!popup_per_cpu_user_history_[i].empty()) {
                        constexpr float chart_height = 50;
                        const ImVec2 chart_sz(ImGui::GetContentRegionAvail().x - 5, chart_height);
                        ImVec2 start_pos = ImGui::GetCursorPos();

                        // User (blue)
                        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
                        ImGui::PlotLines(("##cpu_u" + std::to_string(i)).c_str(),
                            popup_per_cpu_user_history_[i].data(),
                            static_cast<int>(popup_per_cpu_user_history_[i].size()),
                            0, nullptr, 0.0f, 100.0f, chart_sz);
                        ImGui::PopStyleColor(2);

                        // Overlay Kernel (red)
                        ImGui::SetCursorPos(start_pos);
                        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                        ImGui::PlotLines(("##cpu_k" + std::to_string(i)).c_str(),
                            popup_per_cpu_kernel_history_[i].data(),
                            static_cast<int>(popup_per_cpu_kernel_history_[i].size()),
                            0, nullptr, 0.0f, 100.0f, chart_sz);
                        ImGui::PopStyleColor(2);

                        ImGui::SetCursorPos(ImVec2(start_pos.x, start_pos.y + chart_height + 2));
                    }
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::End();
}

} // namespace pex
