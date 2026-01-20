#include "app.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <thread>
#include <csignal>
#include "stb_image.h"
#include "pex_icon.hpp"

namespace pex {

App::App() {
    previous_system_cpu_times_ = SystemInfo::get_cpu_times();
    last_refresh_ = std::chrono::steady_clock::now();
    last_key_time_ = std::chrono::steady_clock::now();
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
            pex_icon_data, static_cast<int>(pex_icon_size),
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

    // Initial data load
    refresh_processes();

    // Main loop
    while (!glfwWindowShouldClose(window_)) {
        // Wait for events - blocks until event or timeout (saves CPU when idle)
        glfwWaitEventsTimeout(refresh_interval_ms_ / 1000.0);

        // Handle focus request from another instance
        if (focus_requested_.exchange(false)) {
            glfwFocusWindow(window_);
            glfwRequestWindowAttention(window_);
        }

        auto now = std::chrono::steady_clock::now();

        // Check if data refresh is needed
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_refresh_).count();
        if (elapsed >= refresh_interval_ms_) {
            refresh_processes();
            last_refresh_ = now;
        }

        // Reset search after 1 second of no typing
        if (!search_text_.empty()) {
            auto since_key = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_key_time_).count();
            if (since_key > 1000) {
                search_text_.clear();
            }
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

        // Sleep briefly to reduce CPU usage
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window_);
    glfwTerminate();
}

void App::request_focus() {
    focus_requested_ = true;
    glfwPostEmptyEvent(); // Wake up the event loop
}

void App::render() {
    // Create main window that fills the viewport
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar;

    ImGui::Begin("PEX", nullptr, window_flags);

    render_menu_bar();
    render_toolbar();

    // Main content area with splitter
    float available_height = ImGui::GetContentRegionAvail().y - 25; // Reserve space for status bar
    float upper_height = available_height * 0.6f;
    float lower_height = available_height * 0.4f;

    // Upper pane - Process list/tree
    ImGui::BeginChild("ProcessPane", ImVec2(0, upper_height), true);
    handle_search_input();
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
    ImGui::Text("Processes: %d | CPU: %.1f%% | Memory: %s / %s",
                process_count_, cpu_usage_,
                format_bytes(memory_used_).c_str(),
                format_bytes(memory_total_).c_str());

    if (!search_text_.empty()) {
        ImGui::SameLine();
        ImGui::Text("| Search: %s", search_text_.c_str());
    }

    ImGui::End();
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
            if (ImGui::MenuItem("Refresh", "F5")) {
                refresh_processes();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Process")) {
            if (ImGui::MenuItem("Kill Process", "Delete", false, selected_process_ != nullptr)) {
                if (selected_process_) {
                    kill(selected_process_->info.pid, SIGTERM);
                }
            }
            if (ImGui::MenuItem("Kill Tree", nullptr, false, selected_process_ != nullptr)) {
                kill_process_tree(selected_process_);
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }
}

void App::render_toolbar() {
    if (ImGui::Button("Refresh")) {
        refresh_processes();
    }
    ImGui::SameLine();

    if (ImGui::Button(is_tree_view_ ? "List View" : "Tree View")) {
        is_tree_view_ = !is_tree_view_;
    }
    ImGui::SameLine();

    if (ImGui::Button("Kill") && selected_process_) {
        kill(selected_process_->info.pid, SIGTERM);
    }
    ImGui::SameLine();

    if (ImGui::Button("Kill Tree") && selected_process_) {
        kill_process_tree(selected_process_);
    }
    ImGui::SameLine();

    ImGui::Text("Refresh:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    const char* intervals[] = {"500ms", "1s", "2s", "5s"};
    static int current_interval = 1;
    if (ImGui::Combo("##interval", &current_interval, intervals, IM_ARRAYSIZE(intervals))) {
        const int values[] = {500, 1000, 2000, 5000};
        refresh_interval_ms_ = values[current_interval];
    }
}

} // namespace pex
