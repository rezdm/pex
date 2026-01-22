#include "app.hpp"
#include "imgui.h"
#include <charconv>
#include <algorithm>

namespace pex {

void App::render_details_panel() {
    if (ImGui::BeginTabBar("DetailsTabs")) {
        if (ImGui::BeginTabItem("File Handles")) {
            active_details_tab_ = DetailsTab::FileHandles;
            render_file_handles_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Network")) {
            active_details_tab_ = DetailsTab::Network;
            render_network_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Threads")) {
            active_details_tab_ = DetailsTab::Threads;
            render_threads_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Memory")) {
            active_details_tab_ = DetailsTab::Memory;
            render_memory_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Environment")) {
            active_details_tab_ = DetailsTab::Environment;
            render_environment_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Libraries")) {
            active_details_tab_ = DetailsTab::Libraries;
            render_libraries_tab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void App::render_file_handles_tab() {
    if (ImGui::BeginTable("FileHandles", 3,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
            ImGuiTableFlags_Sortable)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("FD", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        // Handle sorting
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                const auto& spec = sort_specs->Specs[0];
                file_handles_sort_col_ = spec.ColumnIndex;
                file_handles_sort_asc_ = (spec.SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
            }
        }
        // Always apply sort
        if (!file_handles_.empty()) {
            const int col = file_handles_sort_col_;
            const bool asc = file_handles_sort_asc_;
            std::ranges::sort(file_handles_, [col, asc](const FileHandleInfo& a, const FileHandleInfo& b) {
                int result = 0;
                switch (col) {
                    case 0: result = a.fd - b.fd; break;
                    case 1: result = a.type.compare(b.type); break;
                    case 2: result = a.path.compare(b.path); break;
                    default: result = 0;
                }
                return asc ? (result < 0) : (result > 0);
            });
        }

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

    // Helper to extract port from endpoint for sorting
    auto get_port = [](const std::string& endpoint) -> uint16_t {
        size_t colon_pos = endpoint.rfind(':');
        if (colon_pos == std::string::npos) return 0;
        uint16_t port = 0;
        std::from_chars(endpoint.data() + colon_pos + 1,
                       endpoint.data() + endpoint.size(), port);
        return port;
    };

    if (ImGui::BeginTable("Network", 8,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
            ImGuiTableFlags_Sortable)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Protocol", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Local Address", ImGuiTableColumnFlags_WidthFixed, 160);
        ImGui::TableSetupColumn("Local Host", ImGuiTableColumnFlags_WidthFixed, 140);
        ImGui::TableSetupColumn("Local Port", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Remote Address", ImGuiTableColumnFlags_WidthFixed, 160);
        ImGui::TableSetupColumn("Remote Host", ImGuiTableColumnFlags_WidthFixed, 140);
        ImGui::TableSetupColumn("Remote Port", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        // Handle sorting
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                const auto& spec = sort_specs->Specs[0];
                network_sort_col_ = spec.ColumnIndex;
                network_sort_asc_ = (spec.SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
            }
        }
        // Always apply sort
        if (!network_connections_.empty()) {
            const int col = network_sort_col_;
            const bool asc = network_sort_asc_;
            std::ranges::sort(network_connections_, [col, asc, &get_port](const NetworkConnectionInfo& a, const NetworkConnectionInfo& b) {
                int result = 0;
                switch (col) {
                    case 0: result = a.protocol.compare(b.protocol); break;
                    case 1: result = a.local_endpoint.compare(b.local_endpoint); break;
                    case 2: result = a.local_endpoint.compare(b.local_endpoint); break;  // Sort by address for host column
                    case 3: result = static_cast<int>(get_port(a.local_endpoint)) - static_cast<int>(get_port(b.local_endpoint)); break;
                    case 4: result = a.remote_endpoint.compare(b.remote_endpoint); break;
                    case 5: result = a.remote_endpoint.compare(b.remote_endpoint); break;  // Sort by address for host column
                    case 6: result = static_cast<int>(get_port(a.remote_endpoint)) - static_cast<int>(get_port(b.remote_endpoint)); break;
                    case 7: result = a.state.compare(b.state); break;
                    default: result = 0;
                }
                return asc ? (result < 0) : (result > 0);
            });
        }

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
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
            ImGuiTableFlags_Sortable)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("TID", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Pri", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Current Library", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        // Handle sorting
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                const auto& spec = sort_specs->Specs[0];
                threads_sort_col_ = spec.ColumnIndex;
                threads_sort_asc_ = (spec.SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
            }
        }
        // Always apply sort
        if (!threads_.empty()) {
            const int col = threads_sort_col_;
            const bool asc = threads_sort_asc_;
            std::ranges::sort(threads_, [col, asc](const ThreadInfo& a, const ThreadInfo& b) {
                int result = 0;
                switch (col) {
                    case 0: result = a.tid - b.tid; break;
                    case 1: result = a.name.compare(b.name); break;
                    case 2: result = a.state - b.state; break;
                    case 3: result = a.priority - b.priority; break;
                    case 4: result = a.processor - b.processor; break;
                    case 5: result = a.current_library.compare(b.current_library); break;
                    default: result = 0;
                }
                return asc ? (result < 0) : (result > 0);
            });
        }

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

    // Fetch stack on-demand for the selected thread
    if (selected_thread_idx_ >= 0 && selected_thread_idx_ < static_cast<int>(threads_.size()) && details_pid_ > 0) {
        const int tid = threads_[selected_thread_idx_].tid;

        // Header with refresh button
        ImGui::Text("Stack for TID %d", tid);
        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh")) {
            cached_stack_tid_ = -1;  // Force refresh
        }
        ImGui::Separator();

        // Fetch if not cached or cache invalidated
        if (cached_stack_tid_ != tid) {
            cached_stack_ = ProcfsReader::get_thread_stack(details_pid_, tid);
            cached_stack_tid_ = tid;
        }

        // Display stack
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
        ImGui::InputTextMultiline("##stack", cached_stack_.data(), cached_stack_.size() + 1,
            ImGui::GetContentRegionAvail(), ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("Select a thread to view its kernel stack trace");
        if (cached_stack_tid_ != -1) {
            cached_stack_.clear();
            cached_stack_tid_ = -1;
        }
    }

    ImGui::EndChild();
}

void App::render_memory_tab() {
    if (ImGui::BeginTable("Memory", 4,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
            ImGuiTableFlags_Sortable)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Address Range", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 280);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Perms", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Pathname", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        // Handle sorting
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                const auto& spec = sort_specs->Specs[0];
                memory_sort_col_ = spec.ColumnIndex;
                memory_sort_asc_ = (spec.SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
            }
        }
        // Always apply sort
        if (!memory_maps_.empty()) {
            const int col = memory_sort_col_;
            const bool asc = memory_sort_asc_;
            std::ranges::sort(memory_maps_, [col, asc](const MemoryMapInfo& a, const MemoryMapInfo& b) {
                int result = 0;
                switch (col) {
                    case 0: result = a.address.compare(b.address); break;
                    case 1: result = a.size.compare(b.size); break;
                    case 2: result = a.permissions.compare(b.permissions); break;
                    case 3: result = a.pathname.compare(b.pathname); break;
                    default: result = 0;
                }
                return asc ? (result < 0) : (result > 0);
            });
        }

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

void App::render_environment_tab() {
    if (ImGui::BeginTable("Environment", 2,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
            ImGuiTableFlags_Sortable)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 250);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        // Handle sorting
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                const auto& spec = sort_specs->Specs[0];
                environment_sort_col_ = spec.ColumnIndex;
                environment_sort_asc_ = (spec.SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
            }
        }
        // Always apply sort
        if (!environment_vars_.empty()) {
            const int col = environment_sort_col_;
            const bool asc = environment_sort_asc_;
            std::ranges::sort(environment_vars_, [col, asc](const EnvironmentVariable& a, const EnvironmentVariable& b) {
                int result = 0;
                switch (col) {
                    case 0: result = a.name.compare(b.name); break;
                    case 1: result = a.value.compare(b.value); break;
                    default: result = 0;
                }
                return asc ? (result < 0) : (result > 0);
            });
        }

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

void App::render_libraries_tab() {
    if (ImGui::BeginTable("Libraries", 4,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
            ImGuiTableFlags_Sortable)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 250);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Base Address", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        // Handle sorting
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                const auto& spec = sort_specs->Specs[0];
                libraries_sort_col_ = spec.ColumnIndex;
                libraries_sort_asc_ = (spec.SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
            }
        }
        // Always apply sort
        if (!libraries_.empty()) {
            const int col = libraries_sort_col_;
            const bool asc = libraries_sort_asc_;
            std::ranges::sort(libraries_, [col, asc](const LibraryInfo& a, const LibraryInfo& b) {
                int result = 0;
                switch (col) {
                    case 0: result = a.name.compare(b.name); break;
                    case 1: result = (a.total_size < b.total_size) ? -1 : (a.total_size > b.total_size) ? 1 : 0; break;
                    case 2: result = a.base_address.compare(b.base_address); break;
                    case 3: result = a.path.compare(b.path); break;
                    default: result = 0;
                }
                return asc ? (result < 0) : (result > 0);
            });
        }

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

} // namespace pex
