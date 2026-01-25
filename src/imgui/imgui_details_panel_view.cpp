#include "imgui_app.hpp"
#include "imgui.h"
#include <charconv>
#include <algorithm>

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

void ImGuiApp::render_file_handles_tab() {
    auto& dp = view_model_.details_panel;

    if (ImGui::BeginTable("FileHandles", 3,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
            ImGuiTableFlags_Sortable)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("FD", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        bool needs_sort = dp.details_dirty;
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                const auto& spec = sort_specs->Specs[0];
                dp.file_handles_sort.column = spec.ColumnIndex;
                dp.file_handles_sort.ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
                needs_sort = true;
            }
        }

        if (needs_sort && !dp.file_handles.empty()) {
            const int col = dp.file_handles_sort.column;
            const bool asc = dp.file_handles_sort.ascending;
            std::ranges::sort(dp.file_handles, [col, asc](const FileHandleInfo& a, const FileHandleInfo& b) {
                int result = 0;
                switch (col) {
                    case 0: result = a.fd - b.fd; break;
                    case 1: result = a.type.compare(b.type); break;
                    case 2: result = a.path.compare(b.path); break;
                    default: result = 0;
                }
                return asc ? (result < 0) : (result > 0);
            });
            dp.details_dirty = false;
        }

        for (const auto&[fd, type, path] : dp.file_handles) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", fd);
            ImGui::TableNextColumn();
            ImGui::Text("%s", type.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", path.c_str());
        }

        ImGui::EndTable();
    }
}

void ImGuiApp::render_network_tab() {
    auto& dp = view_model_.details_panel;

    auto parse_endpoint = [](const std::string& endpoint) -> std::pair<std::string, uint16_t> {
        size_t colon_pos = endpoint.rfind(':');
        if (colon_pos == std::string::npos) return {endpoint, 0};

        std::string ip = endpoint.substr(0, colon_pos);
        uint16_t port = 0;
        std::from_chars(endpoint.data() + colon_pos + 1,
                       endpoint.data() + endpoint.size(), port);
        return {ip, port};
    };

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

        bool needs_sort = dp.details_dirty;
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                const auto& spec = sort_specs->Specs[0];
                dp.network_sort.column = spec.ColumnIndex;
                dp.network_sort.ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
                needs_sort = true;
            }
        }

        if (needs_sort && !dp.network_connections.empty()) {
            const int col = dp.network_sort.column;
            const bool asc = dp.network_sort.ascending;
            std::ranges::sort(dp.network_connections, [col, asc, &get_port, &parse_endpoint, this](const NetworkConnectionInfo& a, const NetworkConnectionInfo& b) {
                int result = 0;
                switch (col) {
                    case 0: result = a.protocol.compare(b.protocol); break;
                    case 1: result = a.local_endpoint.compare(b.local_endpoint); break;
                    case 2: {
                        auto [a_ip, _a_port] = parse_endpoint(a.local_endpoint);
                        auto [b_ip, _b_port] = parse_endpoint(b.local_endpoint);
                        std::string host_a = name_resolver_.get_hostname(a_ip);
                        std::string host_b = name_resolver_.get_hostname(b_ip);
                        if (host_a.empty()) host_a = a.local_endpoint;
                        if (host_b.empty()) host_b = b.local_endpoint;
                        result = host_a.compare(host_b);
                        break;
                    }
                    case 3: result = static_cast<int>(get_port(a.local_endpoint)) - static_cast<int>(get_port(b.local_endpoint)); break;
                    case 4: result = a.remote_endpoint.compare(b.remote_endpoint); break;
                    case 5: {
                        auto [a_ip, _a_port] = parse_endpoint(a.remote_endpoint);
                        auto [b_ip, _b_port] = parse_endpoint(b.remote_endpoint);
                        std::string host_a = name_resolver_.get_hostname(a_ip);
                        std::string host_b = name_resolver_.get_hostname(b_ip);
                        if (host_a.empty()) host_a = a.remote_endpoint;
                        if (host_b.empty()) host_b = b.remote_endpoint;
                        result = host_a.compare(host_b);
                        break;
                    }
                    case 6: result = static_cast<int>(get_port(a.remote_endpoint)) - static_cast<int>(get_port(b.remote_endpoint)); break;
                    case 7: result = a.state.compare(b.state); break;
                    default: result = 0;
                }
                return asc ? (result < 0) : (result > 0);
            });
            dp.details_dirty = false;
        }

        auto base_protocol = [](const std::string& proto) -> std::string {
            if (proto.starts_with("tcp")) return "tcp";
            if (proto.starts_with("udp")) return "udp";
            return proto;
        };

        for (const auto& conn : dp.network_connections) {
            auto [local_ip, local_port] = parse_endpoint(conn.local_endpoint);
            auto [remote_ip, remote_port] = parse_endpoint(conn.remote_endpoint);

            std::string local_host = name_resolver_.get_hostname(local_ip);
            std::string remote_host = name_resolver_.get_hostname(remote_ip);

            std::string proto_base = base_protocol(conn.protocol);
            std::string local_service = name_resolver_.get_service_name(local_port, proto_base);
            std::string remote_service = name_resolver_.get_service_name(remote_port, proto_base);

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text("%s", conn.protocol.c_str());

            ImGui::TableNextColumn();
            ImGui::Text("%s", conn.local_endpoint.c_str());

            ImGui::TableNextColumn();
            if (!local_host.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "%s", local_host.c_str());
            } else {
                ImGui::TextDisabled("-");
            }

            ImGui::TableNextColumn();
            if (!local_service.empty()) {
                ImGui::Text("%s", local_service.c_str());
            } else {
                ImGui::Text("%d", local_port);
            }

            ImGui::TableNextColumn();
            ImGui::Text("%s", conn.remote_endpoint.c_str());

            ImGui::TableNextColumn();
            if (!remote_host.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "%s", remote_host.c_str());
            } else {
                ImGui::TextDisabled("-");
            }

            ImGui::TableNextColumn();
            if (!remote_service.empty()) {
                ImGui::Text("%s", remote_service.c_str());
            } else {
                ImGui::Text("%d", remote_port);
            }

            ImGui::TableNextColumn();
            ImGui::Text("%s", conn.state.c_str());
        }

        ImGui::EndTable();
    }
}

void ImGuiApp::render_threads_tab() {
    auto& dp = view_model_.details_panel;
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

        bool needs_sort = dp.details_dirty;
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                const auto& spec = sort_specs->Specs[0];
                dp.threads_sort.column = spec.ColumnIndex;
                dp.threads_sort.ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
                needs_sort = true;
            }
        }

        if (needs_sort && !dp.threads.empty()) {
            const int col = dp.threads_sort.column;
            const bool asc = dp.threads_sort.ascending;
            std::ranges::sort(dp.threads, [col, asc](const ThreadInfo& a, const ThreadInfo& b) {
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
            dp.details_dirty = false;
        }

        if (dp.selected_thread_tid != -1) {
            dp.selected_thread_idx = -1;
            for (int i = 0; i < static_cast<int>(dp.threads.size()); i++) {
                if (dp.threads[i].tid == dp.selected_thread_tid) {
                    dp.selected_thread_idx = i;
                    break;
                }
            }
        }

        for (int i = 0; i < static_cast<int>(dp.threads.size()); i++) {
            const auto& thread = dp.threads[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            const bool is_selected = (i == dp.selected_thread_idx);
            if (ImGui::Selectable("##row", is_selected,
                    ImGuiSelectableFlags_SpanAllColumns)) {
                dp.selected_thread_idx = i;
                dp.selected_thread_tid = thread.tid;
                dp.cached_stack_tid = -1;
                dp.cached_stack.clear();
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

    if (dp.selected_thread_idx >= 0 && dp.selected_thread_idx < static_cast<int>(dp.threads.size()) && dp.details_pid > 0) {
        const int tid = dp.threads[dp.selected_thread_idx].tid;
        dp.selected_thread_tid = tid;

        ImGui::Text("Stack for TID %d", tid);
        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh")) {
            dp.cached_stack_tid = -1;
        }
        ImGui::Separator();

        if (dp.cached_stack_tid != tid) {
            dp.cached_stack = details_provider_->get_thread_stack(dp.details_pid, tid);
            dp.cached_stack_tid = tid;
        }

        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
        ImGui::InputTextMultiline("##stack", dp.cached_stack.data(), dp.cached_stack.size() + 1,
            ImGui::GetContentRegionAvail(), ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("Select a thread to view its kernel stack trace");
        if (dp.cached_stack_tid != -1) {
            dp.cached_stack.clear();
            dp.cached_stack_tid = -1;
        }
        dp.selected_thread_tid = -1;
    }

    ImGui::EndChild();
}

void ImGuiApp::render_memory_tab() {
    auto& dp = view_model_.details_panel;

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

        bool needs_sort = dp.details_dirty;
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                const auto& spec = sort_specs->Specs[0];
                dp.memory_sort.column = spec.ColumnIndex;
                dp.memory_sort.ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
                needs_sort = true;
            }
        }

        if (needs_sort && !dp.memory_maps.empty()) {
            const int col = dp.memory_sort.column;
            const bool asc = dp.memory_sort.ascending;
            std::ranges::sort(dp.memory_maps, [col, asc](const MemoryMapInfo& a, const MemoryMapInfo& b) {
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
            dp.details_dirty = false;
        }

        for (const auto& map : dp.memory_maps) {
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

void ImGuiApp::render_environment_tab() {
    auto& dp = view_model_.details_panel;

    if (ImGui::BeginTable("Environment", 2,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
            ImGuiTableFlags_Sortable)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 250);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        bool needs_sort = dp.details_dirty;
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                const auto& spec = sort_specs->Specs[0];
                dp.environment_sort.column = spec.ColumnIndex;
                dp.environment_sort.ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
                needs_sort = true;
            }
        }

        if (needs_sort && !dp.environment_vars.empty()) {
            const int col = dp.environment_sort.column;
            const bool asc = dp.environment_sort.ascending;
            std::ranges::sort(dp.environment_vars, [col, asc](const EnvironmentVariable& a, const EnvironmentVariable& b) {
                int result = 0;
                switch (col) {
                    case 0: result = a.name.compare(b.name); break;
                    case 1: result = a.value.compare(b.value); break;
                    default: result = 0;
                }
                return asc ? (result < 0) : (result > 0);
            });
            dp.details_dirty = false;
        }

        for (const auto& var : dp.environment_vars) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", var.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", var.value.c_str());
        }

        ImGui::EndTable();
    }
}

void ImGuiApp::render_libraries_tab() {
    auto& dp = view_model_.details_panel;

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

        bool needs_sort = dp.details_dirty;
        if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                const auto& spec = sort_specs->Specs[0];
                dp.libraries_sort.column = spec.ColumnIndex;
                dp.libraries_sort.ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);
                sort_specs->SpecsDirty = false;
                needs_sort = true;
            }
        }

        if (needs_sort && !dp.libraries.empty()) {
            const int col = dp.libraries_sort.column;
            const bool asc = dp.libraries_sort.ascending;
            std::ranges::sort(dp.libraries, [col, asc](const LibraryInfo& a, const LibraryInfo& b) {
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
            dp.details_dirty = false;
        }

        for (const auto& lib : dp.libraries) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

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

void ImGuiApp::refresh_selected_details() {
    auto& pl = view_model_.process_list;
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
