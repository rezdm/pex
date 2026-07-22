#pragma once

#include <functional>
#include <memory>

struct GLFWwindow;

namespace pex {

// Renderer-backend abstraction so the shared ImGui UI (imgui_app) runs over
// either OpenGL 3 (Linux/BSD/Solaris) or Metal (macOS, where OpenGL is
// deprecated). Only the graphics-API glue lives behind this seam; every
// widget in render() is renderer-agnostic.
class ImGuiRenderer {
public:
    virtual ~ImGuiRenderer() = default;

    // GLFW window hints that must be set before glfwCreateWindow: a GL context
    // version for OpenGL, GLFW_NO_API for Metal.
    virtual void set_window_hints() = 0;

    // After the window exists and the ImGui context is created: make the GL
    // context current / create the Metal device+layer, then init the ImGui
    // platform and renderer backends.
    virtual void init(GLFWwindow* window) = 0;

    // Render one frame. Runs the renderer's *_NewFrame, then `frame_body`
    // (ImGui_ImplGlfw_NewFrame + ImGui::NewFrame + the UI + ImGui::Render),
    // then submits the draw data and presents/swaps.
    virtual void render_frame(GLFWwindow* window,
                              const std::function<void()>& frame_body) = 0;

    // Shut down the renderer backend. The ImGui platform backend and context
    // are owned by imgui_app and torn down there.
    virtual void shutdown() = 0;
};

// One implementation is compiled per build: Metal on macOS, OpenGL 3 elsewhere.
std::unique_ptr<ImGuiRenderer> make_imgui_renderer();

} // namespace pex
