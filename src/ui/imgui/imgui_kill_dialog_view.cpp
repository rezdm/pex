#include "imgui_app.hpp"
#include "imgui.h"

namespace pex {

void ImGuiApp::request_kill_process(const int pid, const std::string& name, const bool is_tree) {
    auto& kd = view_model_.kill_dialog;
    kd.target_pid = pid;
    kd.target_name = name;
    // Capture the start-time token now: the dialog can stay open indefinitely
    // and the killer refuses to signal a recycled PID.
    kd.target_token = killer_->process_start_token(pid);
    kd.is_tree_kill = is_tree;
    kd.error_message.clear();
    kd.show_force_option = false;
    kd.is_visible = true;
}

void ImGuiApp::execute_kill(const bool force) {
    auto& kd = view_model_.kill_dialog;
    if (kd.target_pid <= 0) return;

    // No start-time token means the process was already gone (or unreadable)
    // when the dialog opened. Signaling now would hit whatever has since
    // recycled the PID with no way to verify identity — the exact hazard the
    // token guards against — so refuse rather than let the killer skip the
    // check on a null token.
    if (!kd.target_token) {
        kd.error_message = "Process no longer exists (its PID may have been reused). Kill aborted.";
        kd.show_force_option = false;
        return;
    }

    KillResult result;
    if (kd.is_tree_kill) {
        result = killer_->kill_process_tree(kd.target_pid, force, kd.target_token);
    } else {
        result = killer_->kill_process(kd.target_pid, force, kd.target_token);
    }

    if (result.success && !result.process_still_running) {
        kd.is_visible = false;
        return;
    }

    if (!result.error_message.empty()) {
        kd.error_message = result.error_message;
    }

    if (result.process_still_running && !force) {
        kd.show_force_option = true;
    }
}

void ImGuiApp::render_kill_confirmation_dialog() {
    auto& [is_visible, target_pid, target_name, target_token,
           is_tree_kill, error_message, show_force_option] = view_model_.kill_dialog;
    (void)target_token;  // Consumed by execute_kill()
    if (!is_visible) return;

    ImGui::OpenPopup("Kill Confirmation");

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_Always);

    if (ImGui::BeginPopupModal("Kill Confirmation", &is_visible, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            is_visible = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        if (is_tree_kill) {
            ImGui::TextWrapped("Are you sure you want to terminate the process tree?");
            ImGui::Spacing();
            ImGui::Text("Root process: %s (PID %d)", target_name.c_str(), target_pid);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                "Warning: This will terminate all child processes!");
        } else {
            ImGui::TextWrapped("Are you sure you want to terminate this process?");
            ImGui::Spacing();
            ImGui::Text("Process: %s (PID %d)", target_name.c_str(), target_pid);
        }

        if (!error_message.empty()) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            ImGui::TextWrapped("%s", error_message.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (show_force_option) {
            if (ImGui::Button("Force Kill", ImVec2(120, 0))) {
                execute_kill(true);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("SIGKILL - immediate termination");
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                is_visible = false;
            }
        } else {
            if (ImGui::Button("Terminate", ImVec2(120, 0))) {
                execute_kill(false);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("SIGTERM - allows process to clean up gracefully");
            }
            ImGui::SameLine();
            if (ImGui::Button("Force Kill", ImVec2(120, 0))) {
                execute_kill(true);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("SIGKILL - immediate termination, no cleanup");
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                is_visible = false;
            }
        }

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Note: Killing other users' processes requires root or CAP_KILL.");
        ImGui::PopStyleColor();

        ImGui::EndPopup();
    }
}

} // namespace pex
