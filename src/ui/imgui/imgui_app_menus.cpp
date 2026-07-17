#include "imgui_app.hpp"
#include "imgui.h"
#include <GLFW/glfw3.h>

namespace pex {

void ImGuiApp::render_menu_bar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Export History to CSV...", nullptr, false, history_ != nullptr)) {
                export_history();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Writes recorded per-tick system and per-process samples\nto ~/pex-history-<timestamp>-{system,processes}.csv");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                glfwSetWindowShouldClose(glfwGetCurrentContext(), GLFW_TRUE);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Toggle Tree/List View", "T")) {
                view_model_.process_list.is_tree_view = !view_model_.process_list.is_tree_view;
            }
            if (ImGui::MenuItem("History - Top Consumers...", nullptr, false, history_ != nullptr)) {
                history_view_visible_ = true;
            }
            ImGui::MenuItem("Show Kernel Threads", nullptr, &view_model_.process_list.show_kernel_threads);
            if (ImGui::MenuItem("Difference Highlighting", nullptr, &diff_highlight_enabled_)) {
                if (!diff_highlight_enabled_) {
                    snapshot_diff_ = {};  // Drop current highlights immediately
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("New processes flash green, exited processes linger red\nfor one refresh interval (Process Explorer style)");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Refresh Now", "F5")) {
                details_force_refresh_ = true;
                data_store_->refresh_now();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Find")) {
            if (ImGui::MenuItem("Find Open File / Handle...", "Ctrl+Shift+F")) {
                find_dialog_visible_ = true;
                find_focus_input_ = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Process")) {
            const ProcessNode* selected = nullptr;
            if (current_data_ && view_model_.process_list.selected_pid > 0) {
                if (const auto it = current_data_->process_map.find(view_model_.process_list.selected_pid); it != current_data_->process_map.end()) {
                    selected = it->second;
                }
            }

            if (ImGui::MenuItem("Kill Process...", "Delete", false, selected != nullptr)) {
                if (selected) {
                    request_kill_process(selected->info.pid, selected->info.name, false);
                }
            }
            if (ImGui::MenuItem("Kill Tree...", nullptr, false, selected != nullptr)) {
                if (selected) {
                    request_kill_process(selected->info.pid, selected->info.name, true);
                }
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }
}

void ImGuiApp::render_toolbar() {
    const char* toggle_label = view_model_.system_panel.is_visible ? "[-] System" : "[+] System";
    if (ImGui::Button(toggle_label)) {
        view_model_.system_panel.is_visible = !view_model_.system_panel.is_visible;
    }
    ImGui::SameLine();

    ImGui::Text("Search:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    if (view_model_.process_list.focus_search_box) {
        ImGui::SetKeyboardFocusHere();
        view_model_.process_list.focus_search_box = false;
    }
    if (ImGui::InputText("##search", view_model_.process_list.search_buffer, sizeof(view_model_.process_list.search_buffer),
            ImGuiInputTextFlags_EnterReturnsTrue)) {
        search_next();
    }
    if (ImGui::IsItemEdited() && view_model_.process_list.search_buffer[0] != '\0') {
        search_select_first();
    }
    ImGui::SameLine();

    if (ImGui::Button("^")) {
        search_previous();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Find previous (Shift+F3)");
    }
    ImGui::SameLine();

    if (ImGui::Button("v")) {
        search_next();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Find next (F3)");
    }
    ImGui::SameLine();

    ImGui::Spacing();
    ImGui::SameLine();

    if (ImGui::Button("Refresh")) {
        details_force_refresh_ = true;
        data_store_->refresh_now();
    }
    ImGui::SameLine();

    if (ImGui::Button(view_model_.process_list.is_tree_view ? "List View" : "Tree View")) {
        view_model_.process_list.is_tree_view = !view_model_.process_list.is_tree_view;
    }
    ImGui::SameLine();

    const ProcessNode* selected = nullptr;
    if (current_data_ && view_model_.process_list.selected_pid > 0) {
        if (const auto it = current_data_->process_map.find(view_model_.process_list.selected_pid); it != current_data_->process_map.end()) {
            selected = it->second;
        }
    }

    if (ImGui::Button("Kill") && selected) {
        request_kill_process(selected->info.pid, selected->info.name, false);
    }
    ImGui::SameLine();

    if (ImGui::Button("Kill Tree") && selected) {
        request_kill_process(selected->info.pid, selected->info.name, true);
    }
}

} // namespace pex
