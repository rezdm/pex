#include "imgui_app.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <format>
#include <ctime>
#include <fstream>
#include <algorithm>
#include <functional>
#include <cassert>
#include "stb_image.h"
#include "pex_icon.hpp"

namespace pex {

ImGuiApp::ImGuiApp(DataStore* data_store,
                   ISystemDataProvider* system_provider,
                   IProcessDataProvider* details_provider,
                   IProcessKiller* killer)
    : data_store_(data_store)
    , system_provider_(system_provider)
    , details_provider_(details_provider)
    , killer_(killer) {

    // Validate required dependencies
    assert(data_store_ && "DataStore must not be null");
    assert(system_provider_ && "ISystemDataProvider must not be null");
    assert(details_provider_ && "IProcessDataProvider must not be null");
    assert(killer_ && "IProcessKiller must not be null");

    // Set up callback to wake up UI when new data is available
    data_store_->set_on_data_updated([this]() {
        post_empty_event_debounced();
    });

    // Set up callback to wake up UI when name resolution completes
    name_resolver_.set_on_resolved([this]() {
        post_empty_event_debounced();
    });
}

ImGuiApp::~ImGuiApp() = default;

void ImGuiApp::run() {
    // Initialize GLFW
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    // GL 3.3 + GLSL 330
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Set Wayland app_id for desktop integration
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, "pex");

    // Create window
    window_ = glfwCreateWindow(1400, 900, "PEX - Process Explorer for Linux", nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);

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
    data_store_->start();

    // Get initial data
    current_data_ = data_store_->get_snapshot();

    // Main loop
    while (!glfwWindowShouldClose(window_)) {
        glfwWaitEventsTimeout(0.1);

        // Handle focus request from another instance
        if (focus_requested_.exchange(false)) {
            glfwFocusWindow(window_);
            glfwRequestWindowAttention(window_);
        }

        // Get latest data snapshot
        const auto new_data = data_store_->get_snapshot();
        const bool data_changed = !current_data_ ||
            (new_data && new_data->timestamp != current_data_->timestamp);
        current_data_ = new_data;

        // Apply UI state (collapsed nodes) to the data
        if (current_data_) {
            for (auto& [pid, node] : current_data_->process_map) {
                node->is_expanded = !view_model_.process_list.collapsed_pids.contains(pid);
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
    data_store_->stop();
    name_resolver_.stop();

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window_);
    glfwTerminate();
}

void ImGuiApp::request_focus() {
    focus_requested_ = true;
    post_empty_event_debounced();
}

void ImGuiApp::post_empty_event_debounced() {
    if (!window_) return;

    std::lock_guard lock(event_debounce_mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (now - last_event_post_time_ >= kEventDebounceInterval) {
        last_event_post_time_ = now;
        glfwPostEmptyEvent();
    }
}

std::string ImGuiApp::format_bytes(int64_t bytes) {
    if (bytes < 1024) return std::format("{} B", bytes);
    if (bytes < 1024 * 1024) return std::format("{:.1f} KB", bytes / 1024.0);
    if (bytes < 1024LL * 1024 * 1024) return std::format("{:.1f} MB", bytes / (1024.0 * 1024));
    return std::format("{:.2f} GB", bytes / (1024.0 * 1024 * 1024));
}

std::string ImGuiApp::format_time(const std::chrono::system_clock::time_point tp) {
    const auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_val;
    localtime_r(&time_t_val, &tm_val);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_val);
    return buf;
}

void ImGuiApp::render() {
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
    const float available_height = ImGui::GetContentRegionAvail().y - 25;
    const float upper_height = available_height * 0.6f;
    const float lower_height = available_height * 0.4f;

    // Upper pane - Process list/tree
    ImGui::BeginChild("ProcessPane", ImVec2(0, upper_height), true);
    handle_keyboard_navigation();
    if (view_model_.process_list.is_tree_view) {
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
    if (const auto errors = data_store_->get_recent_errors(); !errors.empty()) {
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

    // Right-aligned controls
    ImGui::SameLine();

    const float combo_width = 70.0f;
    const float button_width = 30.0f;
    const float paused_text_width = data_store_->is_paused() ? 60.0f : 0.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float total_width = paused_text_width + button_width + spacing + combo_width;
    const float available = ImGui::GetContentRegionAvail().x;

    if (available > total_width + 10.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - total_width);
    }

    if (data_store_->is_paused()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.2f, 1.0f));
        ImGui::Text("PAUSED");
        ImGui::PopStyleColor();
        ImGui::SameLine();
    }

    if (data_store_->is_paused()) {
        if (ImGui::Button(">")) {
            data_store_->resume();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Resume data collection");
        }
    } else {
        if (ImGui::Button("||")) {
            data_store_->pause();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Pause data collection");
        }
    }
    ImGui::SameLine();

    ImGui::SetNextItemWidth(combo_width);
    const char* intervals[] = {"500ms", "1s", "2s", "5s"};
    int current_interval = 1;
    if (const int refresh_ms = data_store_->get_refresh_interval(); refresh_ms <= 500) current_interval = 0;
    else if (refresh_ms <= 1000) current_interval = 1;
    else if (refresh_ms <= 2000) current_interval = 2;
    else current_interval = 3;

    if (ImGui::Combo("##interval", &current_interval, intervals, IM_ARRAYSIZE(intervals))) {
        constexpr int values[] = {500, 1000, 2000, 5000};
        data_store_->set_refresh_interval(values[current_interval]);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Refresh interval");
    }

    ImGui::End();

    render_process_popup();
    render_kill_confirmation_dialog();
}

void ImGuiApp::render_menu_bar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                glfwSetWindowShouldClose(glfwGetCurrentContext(), GLFW_TRUE);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Toggle Tree/List View", "T")) {
                view_model_.process_list.is_tree_view = !view_model_.process_list.is_tree_view;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Refresh Now", "F5")) {
                data_store_->refresh_now();
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
