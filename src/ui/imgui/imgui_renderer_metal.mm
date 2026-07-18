#include "imgui_renderer.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_metal.h"

#define GLFW_INCLUDE_NONE          // don't pull the deprecated <OpenGL/gl.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>      // glfwGetCocoaWindow

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AppKit/AppKit.h>

namespace pex {

namespace {

// Metal backend for macOS (OpenGL is deprecated there). GLFW owns the NSWindow
// with GLFW_NO_API; we attach a CAMetalLayer to its content view and drive an
// MTLCommandQueue per frame. Mirrors Dear ImGui's official example_glfw_metal.
// This file is compiled with -fobjc-arc (see CMakeLists); the strong id<>
// members are released by ARC.
class MetalRenderer : public ImGuiRenderer {
public:
    void set_window_hints() override {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }

    void init(GLFWwindow* window) override {
        device_ = MTLCreateSystemDefaultDevice();
        queue_ = [device_ newCommandQueue];

        ImGui_ImplGlfw_InitForOther(window, true);
        ImGui_ImplMetal_Init(device_);

        NSWindow* nswin = glfwGetCocoaWindow(window);
        layer_ = [CAMetalLayer layer];
        layer_.device = device_;
        layer_.pixelFormat = MTLPixelFormatBGRA8Unorm;
        nswin.contentView.layer = layer_;
        nswin.contentView.wantsLayer = YES;
    }

    void render_frame(GLFWwindow* window, const std::function<void()>& frame_body) override {
        @autoreleasepool {
            int w = 0, h = 0;
            glfwGetFramebufferSize(window, &w, &h);
            if (w <= 0 || h <= 0) return;
            layer_.drawableSize = CGSizeMake(w, h);

            id<CAMetalDrawable> drawable = [layer_ nextDrawable];
            if (!drawable) return;  // layer not ready; retry next loop iteration

            id<MTLCommandBuffer> cmd = [queue_ commandBuffer];
            MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
            rpd.colorAttachments[0].texture = drawable.texture;
            rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
            rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
            rpd.colorAttachments[0].clearColor = MTLClearColorMake(0.1, 0.1, 0.1, 1.0);
            id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];

            ImGui_ImplMetal_NewFrame(rpd);
            frame_body();  // glfw new frame + ImGui::NewFrame + UI + ImGui::Render
            ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cmd, enc);

            [enc endEncoding];
            [cmd presentDrawable:drawable];
            [cmd commit];
        }
    }

    void shutdown() override {
        ImGui_ImplMetal_Shutdown();
    }

private:
    id<MTLDevice> device_ = nil;
    id<MTLCommandQueue> queue_ = nil;
    CAMetalLayer* layer_ = nil;
};

} // namespace

std::unique_ptr<ImGuiRenderer> make_imgui_renderer() {
    return std::make_unique<MetalRenderer>();
}

} // namespace pex
