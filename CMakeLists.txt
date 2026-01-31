cmake_minimum_required(VERSION 3.20)
project(pex VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Platform selection - user can override with -DPEX_PLATFORM=<platform>
# Auto-detect default based on OS, but allow explicit override
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PEX_PLATFORM_DEFAULT "linux")
elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    set(PEX_PLATFORM_DEFAULT "freebsd")
elseif(CMAKE_SYSTEM_NAME STREQUAL "SunOS")
    set(PEX_PLATFORM_DEFAULT "solaris")
else()
    set(PEX_PLATFORM_DEFAULT "stub")
endif()
set(PEX_PLATFORM "${PEX_PLATFORM_DEFAULT}" CACHE STRING "Platform backend (linux, freebsd, solaris, stub)")
set_property(CACHE PEX_PLATFORM PROPERTY STRINGS linux freebsd solaris stub)

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

# Solaris needs feature test macros for clock_gettime prototype
if(CMAKE_SYSTEM_NAME STREQUAL "SunOS")
    add_compile_definitions(_POSIX_C_SOURCE=199309L __EXTENSIONS__)
endif()

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

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
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

# Find OpenGL
find_package(OpenGL REQUIRED)

# Shared sources (used by both GUI and TUI)
set(PEX_SHARED_SOURCES
    src/data_store.cpp
    src/name_resolver.cpp
    src/single_instance.cpp
)

# GUI-specific sources
set(PEX_GUI_SOURCES
    src/main.cpp
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

# TUI-specific sources
set(PEX_TUI_SOURCES
    src/main_tui.cpp
    src/tui/tui_app.cpp
    src/tui/tui_system_panel.cpp
    src/tui/tui_process_list.cpp
    src/tui/tui_details_panel.cpp
    src/tui/tui_kill_dialog.cpp
    src/tui/tui_input.cpp
    src/tui/tui_colors.cpp
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
elseif(PEX_PLATFORM STREQUAL "freebsd")
    set(PEX_PLATFORM_SOURCES
        src/freebsd/freebsd_process_data_provider.cpp
        src/freebsd/freebsd_system_data_provider.cpp
        src/freebsd/freebsd_process_killer.cpp
        src/freebsd/platform_factory_freebsd.cpp
    )
    set(PEX_PLATFORM_DEFINE "PEX_PLATFORM_FREEBSD")
elseif(PEX_PLATFORM STREQUAL "solaris")
    set(PEX_PLATFORM_SOURCES
        src/solaris/solaris_process_data_provider.cpp
        src/solaris/solaris_system_data_provider.cpp
        src/solaris/solaris_process_killer.cpp
        src/solaris/platform_factory_solaris.cpp
    )
    set(PEX_PLATFORM_DEFINE "PEX_PLATFORM_SOLARIS")
elseif(PEX_PLATFORM STREQUAL "stub")
    set(PEX_PLATFORM_SOURCES
        src/stub/stub_providers.cpp
        src/stub/platform_factory_stub.cpp
    )
    set(PEX_PLATFORM_DEFINE "PEX_PLATFORM_STUB")
else()
    message(FATAL_ERROR "Unknown PEX_PLATFORM: ${PEX_PLATFORM}. Use 'linux', 'freebsd', 'solaris', or 'stub'.")
endif()

# Main GUI executable
add_executable(pex
    ${PEX_SHARED_SOURCES}
    ${PEX_GUI_SOURCES}
    ${PEX_PLATFORM_SOURCES}
)

target_compile_definitions(pex PRIVATE ${PEX_PLATFORM_DEFINE})
target_include_directories(pex PRIVATE src ${stb_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(pex PRIVATE imgui OpenGL::GL)

# Platform-specific libraries for GUI
if(PEX_PLATFORM STREQUAL "freebsd")
    target_link_libraries(pex PRIVATE procstat kvm)
elseif(PEX_PLATFORM STREQUAL "solaris")
    target_link_libraries(pex PRIVATE kstat rt)
    target_link_libraries(glfw PUBLIC rt)
endif()

# TUI executable (pexc)
find_package(Threads REQUIRED)
option(BUILD_TUI "Build TUI version (pexc)" ON)
option(BUILD_NCURSES "Build ncurses from source if not found" OFF)

# Allow manual override of curses paths, otherwise use find_package
set(NCURSES_FOUND FALSE)
if(CURSES_INCLUDE_DIR AND CURSES_LIBRARY)
    message(STATUS "Using manually specified curses: ${CURSES_INCLUDE_DIR}, ${CURSES_LIBRARY}")
    set(CURSES_FOUND TRUE)
    set(NCURSES_FOUND TRUE)
    set(CURSES_LIBRARIES ${CURSES_LIBRARY})
else()
    find_package(Curses)
    # Check if we actually have ncurses (not just old SVR4 curses like on Solaris)
    if(CURSES_FOUND)
        include(CheckIncludeFile)
        set(CMAKE_REQUIRED_INCLUDES ${CURSES_INCLUDE_DIR})
        check_include_file("ncurses.h" HAVE_NCURSES_H)
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
        BUILD_COMMAND make 
        INSTALL_COMMAND make install
        BUILD_IN_SOURCE 1
    )

    # Set variables for the built ncurses
    set(NCURSES_FOUND TRUE)
    set(CURSES_INCLUDE_DIR "${NCURSES_PREFIX}/include;${NCURSES_PREFIX}/include/ncursesw")
    set(CURSES_LIBRARIES "${NCURSES_PREFIX}/lib/libncursesw.a")
    set(NCURSES_BUILT_FROM_SOURCE TRUE)
endif()

if(BUILD_TUI AND NCURSES_FOUND)
    message(STATUS "Building TUI version (pexc)")

    add_executable(pexc
        ${PEX_SHARED_SOURCES}
        ${PEX_TUI_SOURCES}
        ${PEX_PLATFORM_SOURCES}
    )

    target_compile_definitions(pexc PRIVATE ${PEX_PLATFORM_DEFINE})
    target_include_directories(pexc PRIVATE src ${CURSES_INCLUDE_DIR})
    target_link_libraries(pexc PRIVATE ${CURSES_LIBRARIES} Threads::Threads)

    # Platform-specific libraries for TUI
    if(PEX_PLATFORM STREQUAL "freebsd")
        target_link_libraries(pexc PRIVATE procstat kvm)
    elseif(PEX_PLATFORM STREQUAL "solaris")
        target_link_libraries(pexc PRIVATE kstat rt)
    endif()

    # If ncurses was built from source, add dependency
    if(NCURSES_BUILT_FROM_SOURCE)
        add_dependencies(pexc ncurses_external)
        # Set RPATH so the executable can find the shared library at runtime
        set_target_properties(pexc PROPERTIES
            BUILD_RPATH "${NCURSES_PREFIX}/lib"
            INSTALL_RPATH "${NCURSES_PREFIX}/lib"
        )
    endif()
elseif(BUILD_TUI)
    message(WARNING "ncurses not found, TUI version (pexc) will not be built. Use -DBUILD_NCURSES=ON to build ncurses from source.")
endif()
