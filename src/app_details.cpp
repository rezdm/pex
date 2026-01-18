#include "app.hpp"
#include "imgui.h"

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
        ImGui::EndTabBar();
    }
}

void App::render_file_handles_tab() {
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
    if (ImGui::BeginTable("Network", 4,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Protocol", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Local Address", ImGuiTableColumnFlags_WidthFixed, 200);
        ImGui::TableSetupColumn("Remote Address", ImGuiTableColumnFlags_WidthFixed, 200);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const auto& conn : network_connections_) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", conn.protocol.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", conn.local_endpoint.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", conn.remote_endpoint.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", conn.state.c_str());
        }

        ImGui::EndTable();
    }
}

void App::render_threads_tab() {
    // Split view: threads list on left, stack on right
    float width = ImGui::GetContentRegionAvail().x;

    ImGui::BeginChild("ThreadsList", ImVec2(width * 0.4f, 0), true);
    if (ImGui::BeginTable("Threads", 5,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter)) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("TID", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Pri", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(threads_.size()); i++) {
            const auto& thread = threads_[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            bool is_selected = (i == selected_thread_idx_);
            if (ImGui::Selectable(std::to_string(thread.tid).c_str(), is_selected,
                    ImGuiSelectableFlags_SpanAllColumns)) {
                selected_thread_idx_ = i;
            }

            ImGui::TableNextColumn();
            ImGui::Text("%s", thread.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%c", thread.state);
            ImGui::TableNextColumn();
            ImGui::Text("%d", thread.priority);
            ImGui::TableNextColumn();
            ImGui::Text("%d", thread.processor);
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

void App::render_memory_tab() {
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

void App::render_environment_tab() {
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

} // namespace pex
