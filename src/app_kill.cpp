#include "app.hpp"
#include "imgui.h"
#include <format>
#include <csignal>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>
#include <thread>

namespace fs = std::filesystem;

namespace pex {

// Helper: get parent PID from /proc/<pid>/stat
static int get_ppid(int pid) {
    std::ifstream file("/proc/" + std::to_string(pid) + "/stat");
    if (!file) return -1;

    std::string content;
    std::getline(file, content);

    // Format: pid (comm) state ppid ...
    // comm can contain spaces/parens, so find last ')'
    size_t comm_end = content.rfind(')');
    if (comm_end == std::string::npos) return -1;

    std::istringstream iss(content.substr(comm_end + 2));
    std::string state;
    int ppid;
    iss >> state >> ppid;
    return ppid;
}

// Build current process tree from /proc and collect all descendants
static void collect_descendants_from_proc(const int root_pid, std::vector<int>& result) {
    // Build parent -> children map by scanning /proc
    std::map<int, std::vector<int>> children_map;
    std::set<int> all_pids;

    try {
        for (const auto& entry : fs::directory_iterator("/proc")) {
            try {
                if (!entry.is_directory()) continue;

                const auto& name = entry.path().filename().string();
                int pid = 0;
                auto [ptr, ec] = std::from_chars(name.data(), name.data() + name.size(), pid);
                if (ec != std::errc{} || ptr != name.data() + name.size()) continue;

                all_pids.insert(pid);
                int ppid = get_ppid(pid);
                if (ppid > 0) {
                    children_map[ppid].push_back(pid);
                }
            } catch (...) {
                // Process disappeared, skip it
                continue;
            }
        }
    } catch (...) {
        // Directory iteration failed
        return;
    }

    // BFS/DFS to collect all descendants of root_pid
    std::vector<int> stack;
    stack.push_back(root_pid);

    while (!stack.empty()) {
        int pid = stack.back();
        stack.pop_back();

        result.push_back(pid);

        if (auto it = children_map.find(pid); it != children_map.end()) {
            for (int child : it->second) {
                stack.push_back(child);
            }
        }
    }
}

// Post-order traversal to get kill order (children before parents)
static void postorder_kill_order(const int pid, const std::map<int, std::vector<int>>& children_map,
                                  std::set<int>& visited, std::vector<int>& order) {
    if (visited.contains(pid)) return;
    visited.insert(pid);

    if (const auto it = children_map.find(pid); it != children_map.end()) {
        for (const int child : it->second) {
            postorder_kill_order(child, children_map, visited, order);
        }
    }
    order.push_back(pid);
}

std::string App::get_kill_error_message(int err) {
    switch (err) {
        case EPERM:
            return "Permission denied. You may need root privileges or CAP_KILL capability to signal this process.";
        case ESRCH:
            return "Process not found. It may have already terminated.";
        case EINVAL:
            return "Invalid signal.";
        default:
            return std::format("Failed to send signal: {} (errno {})", strerror(err), err);
    }
}

void App::kill_process_tree_impl(int root_pid, bool force) {
    // Build fresh parent -> children map from /proc
    std::map<int, std::vector<int>> children_map;
    std::set<int> descendant_set;

    // First pass: collect all descendants
    std::vector<int> descendants;
    collect_descendants_from_proc(root_pid, descendants);

    for (int pid : descendants) {
        descendant_set.insert(pid);
    }

    // Second pass: build children map only for descendants
    for (int pid : descendants) {
        if (int ppid = get_ppid(pid); ppid > 0 && descendant_set.contains(ppid)) {
            children_map[ppid].push_back(pid);
        }
    }

    // Get post-order traversal (children before parents)
    std::vector<int> kill_order;
    std::set<int> visited;
    postorder_kill_order(root_pid, children_map, visited, kill_order);

    // Kill in post-order (leaves first, root last)
    const int signal = force ? SIGKILL : SIGTERM;
    for (const int pid : kill_order) {
        kill(pid, signal);
    }
}

void App::request_kill_process(int pid, const std::string& name, bool is_tree) {
    kill_target_pid_ = pid;
    kill_target_name_ = name;
    kill_is_tree_ = is_tree;
    kill_error_message_.clear();
    kill_show_force_option_ = false;
    show_kill_dialog_ = true;
}

void App::execute_kill(bool force) {
    if (kill_target_pid_ <= 0) return;

    const int signal = force ? SIGKILL : SIGTERM;
    int result;

    if (kill_is_tree_) {
        kill_process_tree_impl(kill_target_pid_, force);
        // Check if root process was killed successfully
        result = kill(kill_target_pid_, 0);  // Signal 0 checks if process exists
        if (result == 0) {
            // Process still exists - if we used SIGTERM, offer force kill
            if (!force) {
                kill_error_message_ = "SIGTERM sent. Process may still be running. Use Force Kill (SIGKILL) if it doesn't terminate.";
                kill_show_force_option_ = true;
                return;
            }
        } else if (errno == ESRCH) {
            // Process gone - success
            show_kill_dialog_ = false;
            return;
        }
    } else {
        result = kill(kill_target_pid_, signal);
        if (result == -1) {
            kill_error_message_ = get_kill_error_message(errno);
            if (errno == EPERM) {
                kill_show_force_option_ = false;  // Force won't help with permission issues
            }
            return;
        }

        // Give process a moment to terminate, then check if still alive
        if (!force) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (kill(kill_target_pid_, 0) == 0) {
                // Still running
                kill_error_message_ = "SIGTERM sent. Process may still be running. Use Force Kill (SIGKILL) if it doesn't terminate.";
                kill_show_force_option_ = true;
                return;
            }
        }
    }

    // Success
    show_kill_dialog_ = false;
}

void App::render_kill_confirmation_dialog() {
    if (!show_kill_dialog_) return;

    ImGui::OpenPopup("Kill Confirmation");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_Always);

    if (ImGui::BeginPopupModal("Kill Confirmation", &show_kill_dialog_, ImGuiWindowFlags_AlwaysAutoResize)) {
        // Handle ESC key to close dialog
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            show_kill_dialog_ = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        if (kill_is_tree_) {
            ImGui::TextWrapped("Are you sure you want to terminate the process tree?");
            ImGui::Spacing();
            ImGui::Text("Root process: %s (PID %d)", kill_target_name_.c_str(), kill_target_pid_);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                "Warning: This will terminate all child processes!");
        } else {
            ImGui::TextWrapped("Are you sure you want to terminate this process?");
            ImGui::Spacing();
            ImGui::Text("Process: %s (PID %d)", kill_target_name_.c_str(), kill_target_pid_);
        }

        // Show error message if any
        if (!kill_error_message_.empty()) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            ImGui::TextWrapped("%s", kill_error_message_.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Buttons
        if (kill_show_force_option_) {
            if (ImGui::Button("Force Kill", ImVec2(120, 0))) {
                execute_kill(true);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("SIGKILL - immediate termination");
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                show_kill_dialog_ = false;
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
                show_kill_dialog_ = false;
            }
        }

        // Help text
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Note: Killing other users' processes requires root or CAP_KILL.");
        ImGui::PopStyleColor();

        ImGui::EndPopup();
    }
}

} // namespace pex
