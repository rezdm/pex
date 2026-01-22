#include "app.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <csignal>
#include <format>
#include "stb_image.h"
#include "pex_icon.hpp"

namespace pex {

App::App() {
    // Set up callback to wake up UI when new data is available
    data_store_.set_on_data_updated([this]() {
        post_empty_event_debounced();
    });

    // Set up callback to wake up UI when name resolution completes
    name_resolver_.set_on_resolved([this]() {
        post_empty_event_debounced();
    });
}

void App::run() {
    // Initialize GLFW
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    // GL 3.3 + GLSL 330
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Set Wayland app_id for desktop integration (icon in Alt+Tab, etc.)
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, "pex");

    // Create window
    window_ = glfwCreateWindow(1400, 900, "PEX - Process Explorer for Linux", nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1); // Enable vsync

    // Set window icon from embedded resource
    {
        int width, height, channels;
        unsigned char* pixels = stbi_load_from_memory(
            pex_icon_data, pex_icon_size,
            &width, &height, &channels, 4);
        if (pixels) {
            GLFWimage icon;
            icon.width = width;
            icon.height = height;
            icon.pixels = pixels;
            glfwSetWindowIcon(window_, 1, &icon);
            stbi_image_free(pixels);
        }
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup style
    ImGui::GetIO().FontGlobalScale = 1.5f;
    ImGui::GetStyle().ScaleAllSizes(1.5f);

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Start background threads
    name_resolver_.start();
    data_store_.start();

    // Get initial data
    current_data_ = data_store_.get_snapshot();

    // Main loop
    while (!glfwWindowShouldClose(window_)) {
        // Wait for events - blocks until event or timeout
        glfwWaitEventsTimeout(0.1); // 100ms timeout for responsive UI

        // Handle focus request from another instance
        if (focus_requested_.exchange(false)) {
            glfwFocusWindow(window_);
            glfwRequestWindowAttention(window_);
        }

        // Get latest data snapshot (lock-free read of shared_ptr)
        const auto new_data = data_store_.get_snapshot();
        const bool data_changed = !current_data_ ||
            (new_data && new_data->timestamp != current_data_->timestamp);
        current_data_ = new_data;

        // Apply UI state (collapsed nodes) to the data
        if (current_data_) {
            for (auto& [pid, node] : current_data_->process_map) {
                node->is_expanded = !collapsed_pids_.contains(pid);
            }
        }

        // Refresh details when data updates
        if (data_changed) {
            refresh_selected_details();
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        render();

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window_, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window_);
    }

    // Stop background threads
    data_store_.stop();
    name_resolver_.stop();

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window_);
    glfwTerminate();
}

void App::request_focus() {
    focus_requested_ = true;
    post_empty_event_debounced(); // Wake up the event loop
}

void App::post_empty_event_debounced() {
    if (!window_) return;

    std::lock_guard lock(event_debounce_mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (now - last_event_post_time_ >= kEventDebounceInterval) {
        last_event_post_time_ = now;
        glfwPostEmptyEvent();
    }
}

void App::render() {
    if (!current_data_) return;

    // Create main window that fills the viewport
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    constexpr ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                              ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                              ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar;

    ImGui::Begin("PEX", nullptr, window_flags);

    render_menu_bar();
    render_toolbar();
    render_system_panel();

    // Main content area with splitter
    const float available_height = ImGui::GetContentRegionAvail().y - 25; // Reserve space for status bar
    const float upper_height = available_height * 0.6f;
    const float lower_height = available_height * 0.4f;

    // Upper pane - Process list/tree
    ImGui::BeginChild("ProcessPane", ImVec2(0, upper_height), true);
    handle_keyboard_navigation();
    if (is_tree_view_) {
        render_process_tree();
    } else {
        render_process_list();
    }
    ImGui::EndChild();

    // Lower pane - Details
    ImGui::BeginChild("DetailsPane", ImVec2(0, lower_height), true);
    render_details_panel();
    ImGui::EndChild();

    // Status bar
    auto errors = data_store_.get_recent_errors();
    if (!errors.empty()) {
        // Show most recent error in yellow
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
        ImGui::Text("[!] %s", errors.back().message.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
    }
    ImGui::Text("Processes: %d | CPU: %.1f%% | Memory: %s / %s",
                current_data_->process_count, current_data_->cpu_usage,
                format_bytes(current_data_->memory_used).c_str(),
                format_bytes(current_data_->memory_total).c_str());

    // Right-aligned controls: pause button and refresh interval
    ImGui::SameLine();

    // Calculate right-aligned position
    const float combo_width = 70.0f;
    const float button_width = 30.0f;
    const float paused_text_width = data_store_.is_paused() ? 60.0f : 0.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float total_width = paused_text_width + button_width + spacing + combo_width;
    const float available = ImGui::GetContentRegionAvail().x;

    if (available > total_width + 10.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - total_width);
    }

    // Show PAUSED indicator when paused
    if (data_store_.is_paused()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.2f, 1.0f));
        ImGui::Text("PAUSED");
        ImGui::PopStyleColor();
        ImGui::SameLine();
    }

    // Pause/Resume button
    if (data_store_.is_paused()) {
        if (ImGui::Button(">")) {
            data_store_.resume();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Resume data collection");
        }
    } else {
        if (ImGui::Button("||")) {
            data_store_.pause();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Pause data collection");
        }
    }
    ImGui::SameLine();

    // Refresh interval combo
    ImGui::SetNextItemWidth(combo_width);
    const char* intervals[] = {"500ms", "1s", "2s", "5s"};
    int current_interval = 1;
    if (const int refresh_ms = data_store_.get_refresh_interval(); refresh_ms <= 500) current_interval = 0;
    else if (refresh_ms <= 1000) current_interval = 1;
    else if (refresh_ms <= 2000) current_interval = 2;
    else current_interval = 3;

    if (ImGui::Combo("##interval", &current_interval, intervals, IM_ARRAYSIZE(intervals))) {
        constexpr int values[] = {500, 1000, 2000, 5000};
        data_store_.set_refresh_interval(values[current_interval]);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Refresh interval");
    }

    ImGui::End();

    // Process popup (shown when double-clicking a process)
    render_process_popup();

    // Kill confirmation dialog
    render_kill_confirmation_dialog();
}

void App::render_menu_bar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                glfwSetWindowShouldClose(glfwGetCurrentContext(), GLFW_TRUE);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Toggle Tree/List View", "T")) {
                is_tree_view_ = !is_tree_view_;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Refresh Now", "F5")) {
                data_store_.refresh_now();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Process")) {
            const ProcessNode* selected = nullptr;
            if (current_data_ && selected_pid_ > 0) {
                if (const auto it = current_data_->process_map.find(selected_pid_); it != current_data_->process_map.end()) {
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

void App::render_toolbar() {
    // System panel toggle
    const char* toggle_label = show_system_panel_ ? "[-] System" : "[+] System";
    if (ImGui::Button(toggle_label)) {
        show_system_panel_ = !show_system_panel_;
    }
    ImGui::SameLine();

    // Search box
    ImGui::Text("Search:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    if (focus_search_box_) {
        ImGui::SetKeyboardFocusHere();
        focus_search_box_ = false;
    }
    if (ImGui::InputText("##search", search_buffer_, sizeof(search_buffer_),
            ImGuiInputTextFlags_EnterReturnsTrue)) {
        search_next();
    }
    // Also search on text change - but stay on current if it still matches
    if (ImGui::IsItemEdited() && search_buffer_[0] != '\0') {
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
        data_store_.refresh_now();
    }
    ImGui::SameLine();

    if (ImGui::Button(is_tree_view_ ? "List View" : "Tree View")) {
        is_tree_view_ = !is_tree_view_;
    }
    ImGui::SameLine();

    const ProcessNode* selected = nullptr;
    if (current_data_ && selected_pid_ > 0) {
        if (const auto it = current_data_->process_map.find(selected_pid_); it != current_data_->process_map.end()) {
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

void App::render_system_panel() const {
    if (!current_data_) return;

    if (!show_system_panel_) {
        return;
    }

    // Compact bytes format like htop: "6.77G" instead of "6.77 GB"
    auto format_compact = [](int64_t bytes) -> std::string {
        if (bytes < 1024) return std::format("{}B", bytes);
        if (bytes < 1024 * 1024) return std::format("{:.0f}K", bytes / 1024.0);
        if (bytes < 1024LL * 1024 * 1024) return std::format("{:.2f}G", bytes / (1024.0 * 1024 * 1024));
        return std::format("{:.2f}G", bytes / (1024.0 * 1024 * 1024));
    };

    // Get data from snapshot
    const auto& mem_info_used = current_data_->memory_used;
    const auto& mem_info_total = current_data_->memory_total;
    const auto& swap_info = current_data_->swap_info;
    const auto& load = current_data_->load_average;
    const auto&[uptime_seconds, idle_seconds] = current_data_->uptime_info;
    const auto& per_cpu_usage = current_data_->per_cpu_usage;

    const int cpu_count = static_cast<int>(per_cpu_usage.size());

    // Two-column layout: CPUs on left, stats on right
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

    // Helper to draw a progress bar aligned with text
    auto draw_bar = [&](float ratio, float width, const ImVec4& color) {
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

            // Memory
            {
                const float mem_ratio = mem_info_total > 0 ? static_cast<float>(mem_info_used) / static_cast<float>(mem_info_total) : 0.0f;
                ImGui::Text("Mem[");
                ImGui::SameLine(0, 0);
                draw_bar(mem_ratio, 120, ImVec4(0.0f, 0.6f, 0.0f, 1.0f));
                ImGui::SameLine(0, 0);
                ImGui::Text("] %s/%s", format_compact(mem_info_used).c_str(), format_compact(mem_info_total).c_str());
            }

            // Swap
            {
                const float swap_ratio = swap_info.total > 0 ? static_cast<float>(swap_info.used) / static_cast<float>(swap_info.total) : 0.0f;
                ImGui::Text("Swp[");
                ImGui::SameLine(0, 0);
                draw_bar(swap_ratio, 120, ImVec4(0.6f, 0.0f, 0.0f, 1.0f));
                ImGui::SameLine(0, 0);
                ImGui::Text("] %s/%s", format_compact(swap_info.used).c_str(), format_compact(swap_info.total).c_str());
            }

            // Tasks
            ImGui::Text("Tasks: %d, %d thr; %d running",
                current_data_->process_count, current_data_->thread_count, current_data_->running_count);

            // Load
            ImGui::Text("Load average: %.2f %.2f %.2f", load.one_min, load.five_min, load.fifteen_min);

            // Uptime
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
        // Narrow window - stack vertically
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

        // CPUs in grid
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
