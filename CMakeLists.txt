cmake_minimum_required(VERSION 3.20)
project(pex VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Platform selection - user can override with -DPEX_PLATFORM=stub
# Auto-detect default based on OS, but allow explicit override
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PEX_PLATFORM_DEFAULT "linux")
else()
    set(PEX_PLATFORM_DEFAULT "stub")
endif()
set(PEX_PLATFORM "${PEX_PLATFORM_DEFAULT}" CACHE STRING "Platform backend (linux, stub)")
set_property(CACHE PEX_PLATFORM PROPERTY STRINGS linux stub)

message(STATUS "PEX platform: ${PEX_PLATFORM}")

# Include custom CMake modules
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(EmbedResource)

# Embed the icon as a C array
embed_resource(
    ${CMAKE_SOURCE_DIR}/src/pex.png
    ${CMAKE_BINARY_DIR}/generated/pex_icon.hpp
    pex_icon
)

# Fetch dependencies
include(FetchContent)

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# Dear ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.6
)
FetchContent_MakeAvailable(imgui)

# stb (for image loading)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
)
FetchContent_MakeAvailable(stb)

# ImGui library
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw)

# Find OpenGL
find_package(OpenGL REQUIRED)

# Core sources (platform-independent)
set(PEX_CORE_SOURCES
    src/main.cpp
    src/data_store.cpp
    src/name_resolver.cpp
    src/single_instance.cpp
    src/stb_impl.cpp

    # ImGui application and views
    src/imgui/imgui_app.cpp
    src/imgui/imgui_system_panel_view.cpp
    src/imgui/imgui_process_list_view.cpp
    src/imgui/imgui_details_panel_view.cpp
    src/imgui/imgui_process_popup_view.cpp
    src/imgui/imgui_kill_dialog_view.cpp
    src/imgui/imgui_input.cpp
)

# Platform-specific sources
if(PEX_PLATFORM STREQUAL "linux")
    set(PEX_PLATFORM_SOURCES
        src/procfs_reader.cpp
        src/system_info.cpp
        src/linux/linux_process_data_provider.cpp
        src/linux/linux_system_data_provider.cpp
        src/linux/linux_process_killer.cpp
        src/linux/platform_factory_linux.cpp
    )
    set(PEX_PLATFORM_DEFINE "PEX_PLATFORM_LINUX")
elseif(PEX_PLATFORM STREQUAL "stub")
    set(PEX_PLATFORM_SOURCES
        src/stub/stub_providers.cpp
        src/stub/platform_factory_stub.cpp
    )
    set(PEX_PLATFORM_DEFINE "PEX_PLATFORM_STUB")
else()
    message(FATAL_ERROR "Unknown PEX_PLATFORM: ${PEX_PLATFORM}. Use 'linux' or 'stub'.")
endif()

# Main executable
add_executable(pex
    ${PEX_CORE_SOURCES}
    ${PEX_PLATFORM_SOURCES}
)

target_compile_definitions(pex PRIVATE ${PEX_PLATFORM_DEFINE})
target_include_directories(pex PRIVATE src ${stb_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(pex PRIVATE imgui OpenGL::GL)
