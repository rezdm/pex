#include "imgui_renderer.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>  // also pulls the GL header used for glClear/glViewport

namespace pex {

namespace {

// OpenGL 3.3 core backend — the original render path for Linux/BSD/Solaris.
class OpenGLRenderer : public ImGuiRenderer {
public:
    void set_window_hints() override {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    }

    void init(GLFWwindow* window) override {
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);  // vsync
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 330");
    }

    void render_frame(GLFWwindow* window, const std::function<void()>& frame_body) override {
        ImGui_ImplOpenGL3_NewFrame();
        frame_body();  // glfw new frame + ImGui::NewFrame + UI + ImGui::Render
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    void shutdown() override {
        ImGui_ImplOpenGL3_Shutdown();
    }
};

} // namespace

std::unique_ptr<ImGuiRenderer> make_imgui_renderer() {
    return std::make_unique<OpenGLRenderer>();
}

} // namespace pex
