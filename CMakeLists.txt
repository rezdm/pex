cmake_minimum_required(VERSION 3.20)
project(pex VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to an optimized build: an empty build type means -O0, and a process
# monitor's own overhead is on display in its own process list.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (default Release)" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Platform selection - user can override with -DPEX_PLATFORM=<platform>
# Auto-detect default based on OS, but allow explicit override
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PEX_PLATFORM_DEFAULT "linux")
elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    set(PEX_PLATFORM_DEFAULT "freebsd")
elseif(CMAKE_SYSTEM_NAME STREQUAL "SunOS")
    set(PEX_PLATFORM_DEFAULT "solaris")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PEX_PLATFORM_DEFAULT "windows")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PEX_PLATFORM_DEFAULT "macos")
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}. Supported: Linux, FreeBSD, SunOS (Solaris), Windows, Darwin (macOS).")
endif()
set(PEX_PLATFORM "${PEX_PLATFORM_DEFAULT}" CACHE STRING "Platform backend (linux, freebsd, solaris, windows, macos)")
set_property(CACHE PEX_PLATFORM PROPERTY STRINGS linux freebsd solaris windows macos)

# Windows is TUI-only (issue #43): Process Explorer already owns the GUI niche
if(PEX_PLATFORM STREQUAL "windows")
    set(BUILD_GUI OFF CACHE BOOL "" FORCE)
endif()

message(STATUS "PEX platform: ${PEX_PLATFORM}")

# Include custom CMake modules
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include(GNUInstallDirs)

# Build options
option(BUILD_GUI "Build GUI version (pex) - requires OpenGL and X11/Wayland" ON)

# Solaris needs feature test macros for clock_gettime prototype
if(CMAKE_SYSTEM_NAME STREQUAL "SunOS")
    add_compile_definitions(_POSIX_C_SOURCE=199309L __EXTENSIONS__)
endif()

# =============================================================================
# GUI dependencies (GLFW, ImGui, stb, OpenGL) - only when building GUI
# =============================================================================
if(BUILD_GUI)
    # macOS renders through Metal (.mm backends); everyone else uses OpenGL 3.
    if(APPLE)
        enable_language(OBJCXX)
    endif()

    include(EmbedResource)

    # Embed the icon as a C array
    embed_resource(
        ${CMAKE_SOURCE_DIR}/src/pex.png
        ${CMAKE_BINARY_DIR}/generated/pex_icon.hpp
        pex_icon
    )

    include(FetchContent)

    # GLFW (pinned to the commit of tag 3.4 — tags are mutable, hashes are not)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 7b6aead9fb88b3623e3b3725ebb42670cbe4c579  # 3.4
    )
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)  # Keep 'make install' to pex files only

    # Wayland is only available on Linux; other platforms use X11 only
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
        set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    else()
        # FreeBSD/Solaris typically don't have Linux headers for Wayland
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    endif()

    FetchContent_MakeAvailable(glfw)

    # Dear ImGui (pinned to the commit of tag v1.91.6)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG 993fa347495860ed44b83574254ef2a317d0c14f  # v1.91.6
    )
    FetchContent_MakeAvailable(imgui)

    # stb (for image loading)
    FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG f1c79c02822848a9bed4315b12c8c8f3761e1296
    )
    FetchContent_MakeAvailable(stb)

    # Renderer backend: Metal (.mm, ARC) on macOS, OpenGL 3 elsewhere.
    if(APPLE)
        set(PEX_IMGUI_BACKEND ${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm)
    else()
        find_package(OpenGL REQUIRED)
        set(PEX_IMGUI_BACKEND ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    endif()

    # ImGui library (imgui_demo.cpp intentionally excluded: nothing calls
    # ShowDemoWindow and it bloats the release binary)
    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${PEX_IMGUI_BACKEND}
    )
    target_include_directories(imgui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    if(APPLE)
        # imgui_impl_metal.mm requires ARC; link the Metal stack.
        set_source_files_properties(${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm
            PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_link_libraries(imgui PUBLIC glfw
            "-framework Metal" "-framework MetalKit"
            "-framework QuartzCore" "-framework Cocoa")
    else()
        target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
    endif()
endif()

# =============================================================================
# pex_core - Core library (model + services)
# =============================================================================
set(PEX_CORE_SOURCES
    src/core/services/data_store.cpp
    src/core/services/history_store.cpp
    src/core/services/name_resolver.cpp
    src/core/services/settings.cpp
    src/core/services/snapshot_diff.cpp
    src/core/model/system_info.cpp
)
if(NOT PEX_PLATFORM STREQUAL "windows")
    # SingleInstance (unix domain sockets) is used by the GUI only
    list(APPEND PEX_CORE_SOURCES src/core/services/single_instance.cpp)
endif()
add_library(pex_core STATIC ${PEX_CORE_SOURCES})
target_include_directories(pex_core PUBLIC src)
if(MSVC)
    # PUBLIC: propagates to everything that links pex_core
    target_compile_options(pex_core PUBLIC /W4 /utf-8)
    target_compile_definitions(pex_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(pex_core PUBLIC -Wall -Wextra -Wpedantic)
endif()
find_package(Threads REQUIRED)
target_link_libraries(pex_core PUBLIC Threads::Threads)
if(PEX_PLATFORM STREQUAL "windows")
    target_link_libraries(pex_core PUBLIC ws2_32)  # NameResolver uses Winsock
endif()

# =============================================================================
# pex_platform - Platform-specific library
# =============================================================================
if(PEX_PLATFORM STREQUAL "linux")
    set(PEX_PLATFORM_SOURCES
        src/platform/linux/procfs/procfs_common.cpp
        src/platform/linux/procfs/procfs_processes.cpp
        src/platform/linux/procfs/procfs_threads.cpp
        src/platform/linux/procfs/procfs_fds.cpp
        src/platform/linux/procfs/procfs_network.cpp
        src/platform/linux/procfs/procfs_memory.cpp
        src/platform/linux/procfs/procfs_environment.cpp
        src/platform/linux/procfs/procfs_libraries.cpp
        src/platform/linux/linux_process_data_provider.cpp
        src/platform/linux/linux_system_data_provider.cpp
        src/platform/linux/linux_process_killer.cpp
        src/platform/linux/linux_process_event_source.cpp
        src/platform/linux/platform_factory_linux.cpp
    )
    set(PEX_PLATFORM_DEFINE "PEX_PLATFORM_LINUX")
    set(PEX_PLATFORM_LIBS "")
elseif(PEX_PLATFORM STREQUAL "freebsd")
    set(PEX_PLATFORM_SOURCES
        src/platform/freebsd/freebsd_process_data_provider.cpp
        src/platform/freebsd/freebsd_process_details.cpp
        src/platform/freebsd/freebsd_system_data_provider.cpp
        src/platform/freebsd/freebsd_process_killer.cpp
        src/platform/freebsd/freebsd_process_event_source.cpp
        src/platform/freebsd/platform_factory_freebsd.cpp
    )
    set(PEX_PLATFORM_DEFINE "PEX_PLATFORM_FREEBSD")
    set(PEX_PLATFORM_LIBS procstat kvm)
elseif(PEX_PLATFORM STREQUAL "solaris")
    set(PEX_PLATFORM_SOURCES
        src/platform/solaris/solaris_process_data_provider.cpp
        src/platform/solaris/solaris_process_details.cpp
        src/platform/solaris/solaris_system_data_provider.cpp
        src/platform/solaris/solaris_process_killer.cpp
        src/platform/solaris/platform_factory_solaris.cpp
    )
    set(PEX_PLATFORM_DEFINE "PEX_PLATFORM_SOLARIS")
    set(PEX_PLATFORM_LIBS kstat rt)
    if(BUILD_GUI)
        # GLFW needs rt on Solaris
        target_link_libraries(glfw PUBLIC rt)
    endif()
elseif(PEX_PLATFORM STREQUAL "windows")
    set(PEX_PLATFORM_SOURCES
        src/platform/windows/windows_process_data_provider.cpp
        src/platform/windows/windows_system_data_provider.cpp
        src/platform/windows/windows_process_killer.cpp
        src/platform/windows/platform_factory_windows.cpp
    )
    set(PEX_PLATFORM_DEFINE "PEX_PLATFORM_WINDOWS")
    set(PEX_PLATFORM_LIBS psapi iphlpapi ws2_32 ntdll advapi32)
elseif(PEX_PLATFORM STREQUAL "macos")
    set(PEX_PLATFORM_SOURCES
        src/platform/macos/macos_process_data_provider.cpp
        src/platform/macos/macos_process_details.cpp
        src/platform/macos/macos_system_data_provider.cpp
        src/platform/macos/macos_process_killer.cpp
        src/platform/macos/macos_process_event_source.cpp
        src/platform/macos/platform_factory_macos.cpp
    )
    set(PEX_PLATFORM_DEFINE "PEX_PLATFORM_MACOS")
    # libproc and Mach are in libSystem (linked by default); frameworks for
    # the host_* Mach calls come via CoreFoundation.
    set(PEX_PLATFORM_LIBS "-framework CoreFoundation")
else()
    message(FATAL_ERROR "Unknown PEX_PLATFORM: ${PEX_PLATFORM}. Use 'linux', 'freebsd', 'solaris', 'windows', or 'macos'.")
endif()

add_library(pex_platform STATIC ${PEX_PLATFORM_SOURCES})
target_include_directories(pex_platform PUBLIC src)
if(PEX_PLATFORM STREQUAL "windows")
    target_compile_definitions(pex_platform PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
target_compile_definitions(pex_core PUBLIC ${PEX_PLATFORM_DEFINE})
target_link_libraries(pex_platform PUBLIC pex_core)
if(PEX_PLATFORM_LIBS)
    target_link_libraries(pex_platform PUBLIC ${PEX_PLATFORM_LIBS})
endif()

# =============================================================================
# pex_ui_imgui - ImGui UI library (only when building GUI)
# =============================================================================
if(BUILD_GUI)
    if(APPLE)
        set(PEX_RENDERER_SRC src/ui/imgui/imgui_renderer_metal.mm)
    else()
        set(PEX_RENDERER_SRC src/ui/imgui/imgui_renderer_opengl.cpp)
    endif()

    add_library(pex_ui_imgui STATIC
        src/ui/imgui/imgui_app.cpp
        src/ui/imgui/imgui_app_menus.cpp
        src/ui/imgui/imgui_system_panel_view.cpp
        src/ui/imgui/imgui_process_list_view.cpp
        src/ui/imgui/imgui_details_panel_view.cpp
        src/ui/imgui/imgui_details_tabs.cpp
        src/ui/imgui/imgui_process_popup_view.cpp
        src/ui/imgui/imgui_kill_dialog_view.cpp
        src/ui/imgui/imgui_find_handle_dialog.cpp
        src/ui/imgui/imgui_history_view.cpp
        src/ui/imgui/imgui_input.cpp
        ${PEX_RENDERER_SRC}
    )
    if(APPLE)
        set_source_files_properties(src/ui/imgui/imgui_renderer_metal.mm
            PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    endif()
    target_include_directories(pex_ui_imgui PUBLIC src PRIVATE ${stb_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
    target_link_libraries(pex_ui_imgui PUBLIC pex_core pex_platform imgui)

    # =============================================================================
    # Main GUI executable (pex)
    # =============================================================================
    add_executable(pex
        src/main.cpp
        src/stb_impl.cpp
    )
    target_include_directories(pex PRIVATE ${stb_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
    target_link_libraries(pex PRIVATE pex_ui_imgui)
    if(NOT APPLE)
        target_link_libraries(pex PRIVATE OpenGL::GL)
    endif()

    install(TARGETS pex RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(NOT APPLE)
        install(FILES pex.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
        install(FILES src/pex.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
    endif()
endif()

# =============================================================================
# TUI executable (pexc) - optional, requires ncurses
# =============================================================================
option(BUILD_TUI "Build TUI version (pexc)" ON)
option(BUILD_NCURSES "Build ncurses from source if not found" OFF)

# Allow manual override of curses paths, otherwise use find_package
set(NCURSES_FOUND FALSE)
if(PEX_PLATFORM STREQUAL "windows" AND BUILD_TUI)
    # Windows: PDCursesMod (curses API, WinCon backend) built from source.
    # SOURCE_SUBDIR points at a non-existent dir so MakeAvailable only
    # populates the sources without running PDCursesMod's own CMake.
    enable_language(C)
    include(FetchContent)
    FetchContent_Declare(
        pdcursesmod
        GIT_REPOSITORY https://github.com/Bill-Gray/PDCursesMod.git
        GIT_TAG v4.5.4
        SOURCE_SUBDIR cmake-not-used
    )
    FetchContent_MakeAvailable(pdcursesmod)

    file(GLOB PDCURSES_SOURCES
        ${pdcursesmod_SOURCE_DIR}/pdcurses/*.c
        ${pdcursesmod_SOURCE_DIR}/wincon/*.c
    )
    add_library(pdcurses STATIC ${PDCURSES_SOURCES})
    target_include_directories(pdcurses PUBLIC ${pdcursesmod_SOURCE_DIR})
    # Wide-char + UTF-8 output so ACS box-drawing renders in Windows Terminal
    target_compile_definitions(pdcurses PUBLIC PDC_WIDE PDC_FORCE_UTF8)
    target_link_libraries(pdcurses PUBLIC winmm)

    set(CURSES_FOUND TRUE)
    set(NCURSES_FOUND TRUE)
    set(CURSES_INCLUDE_DIR "")     # Comes via the pdcurses target
    set(CURSES_LIBRARIES pdcurses)
elseif(CURSES_INCLUDE_DIR AND CURSES_LIBRARY)
    message(STATUS "Using manually specified curses: ${CURSES_INCLUDE_DIR}, ${CURSES_LIBRARY}")
    set(CURSES_FOUND TRUE)
    set(NCURSES_FOUND TRUE)
    set(CURSES_LIBRARIES ${CURSES_LIBRARY})
else()
    find_package(Curses)
    # Check if we actually have ncurses (not just old SVR4 curses like on Solaris)
    if(CURSES_FOUND)
        include(CheckIncludeFileCXX)
        set(CMAKE_REQUIRED_INCLUDES ${CURSES_INCLUDE_DIR})
        check_include_file_cxx("ncurses.h" HAVE_NCURSES_H)
        if(HAVE_NCURSES_H)
            set(NCURSES_FOUND TRUE)
            message(STATUS "Found ncurses: ${CURSES_LIBRARIES}")
        else()
            message(STATUS "Found curses but not ncurses (missing ncurses.h)")
        endif()
    endif()
endif()

# Auto-build ncurses if not found and BUILD_NCURSES is enabled
if(NOT NCURSES_FOUND AND BUILD_TUI AND BUILD_NCURSES)
    message(STATUS "ncurses not found - building from source")
    include(ExternalProject)

    set(NCURSES_VERSION "6.6")
    set(NCURSES_PREFIX "${CMAKE_BINARY_DIR}/ncurses-install")

    ExternalProject_Add(ncurses_external
        URL "https://ftp.gnu.org/gnu/ncurses/ncurses-${NCURSES_VERSION}.tar.gz"
        URL_HASH SHA256=355b4cbbed880b0381a04c46617b7656e362585d52e9cf84a67e2009b749ff11
        PREFIX "${CMAKE_BINARY_DIR}/ncurses-build"
        CONFIGURE_COMMAND <SOURCE_DIR>/configure
            --prefix=${NCURSES_PREFIX}
            --with-shared
            --without-debug
            --without-ada
            --enable-widec
            --disable-stripping
        BUILD_COMMAND ${CMAKE_MAKE_PROGRAM}
        INSTALL_COMMAND ${CMAKE_MAKE_PROGRAM} install
        BUILD_IN_SOURCE 1
    )

    # Set variables for the built ncurses
    set(NCURSES_FOUND TRUE)
    set(CURSES_INCLUDE_DIR "${NCURSES_PREFIX}/include;${NCURSES_PREFIX}/include/ncursesw")
    set(CURSES_LIBRARIES "${NCURSES_PREFIX}/lib/libncursesw.a")
    set(NCURSES_BUILT_FROM_SOURCE TRUE)
endif()

# =============================================================================
# Unit tests (dependency-free, platform-neutral core only)
# =============================================================================
option(BUILD_TESTS "Build unit tests (pex_tests)" ON)
if(BUILD_TESTS)
    enable_testing()
    add_executable(pex_tests tests/test_core.cpp)
    target_link_libraries(pex_tests PRIVATE pex_core)
    add_test(NAME pex_tests COMMAND pex_tests)
endif()

if(BUILD_TUI AND NCURSES_FOUND)
    message(STATUS "Building TUI version (pexc)")

    # pex_ui_tui - TUI UI library
    add_library(pex_ui_tui STATIC
        src/ui/tui/tui_app.cpp
        src/ui/tui/tui_app_windows.cpp
        src/ui/tui/tui_app_navigation.cpp
        src/ui/tui/tui_app_helpers.cpp
        src/ui/tui/tui_system_panel.cpp
        src/ui/tui/tui_process_list.cpp
        src/ui/tui/tui_details_panel.cpp
        src/ui/tui/tui_details_tabs.cpp
        src/ui/tui/tui_kill_dialog.cpp
        src/ui/tui/tui_find_file.cpp
        src/ui/tui/tui_input.cpp
        src/ui/tui/tui_input_panels.cpp
        src/ui/tui/tui_colors.cpp
    )
    target_include_directories(pex_ui_tui PUBLIC src ${CURSES_INCLUDE_DIR})
    target_link_libraries(pex_ui_tui PUBLIC pex_core pex_platform ${CURSES_LIBRARIES})

    # Main TUI executable
    add_executable(pexc src/main_tui.cpp)
    target_link_libraries(pexc PRIVATE pex_ui_tui)
    install(TARGETS pexc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    # If ncurses was built from source, add dependency
    if(NCURSES_BUILT_FROM_SOURCE)
        add_dependencies(pex_ui_tui ncurses_external)
        # Set RPATH so the executable can find the shared library at runtime
        set_target_properties(pexc PROPERTIES
            BUILD_RPATH "${NCURSES_PREFIX}/lib"
            INSTALL_RPATH "${NCURSES_PREFIX}/lib"
        )
    endif()
elseif(BUILD_TUI)
    message(WARNING "ncurses not found, TUI version (pexc) will not be built. Use -DBUILD_NCURSES=ON to build ncurses from source.")
endif()
