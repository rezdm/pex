#include "tui_app.hpp"
#include "tui_colors.hpp"
#include <cassert>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace pex {

#ifndef _WIN32
// Signal handler for terminal resize - use sig_atomic_t for signal safety
// (Windows/PDCurses delivers resizes as KEY_RESIZE through getch instead)
static volatile sig_atomic_t g_resize_requested = 0;

static void handle_resize([[maybe_unused]] int sig) {
    g_resize_requested = 1;
}
#endif

TuiApp::TuiApp(DataStore* data_store,
               ISystemDataProvider* system_provider,
               IProcessDataProvider* details_provider,
               IProcessKiller* killer,
               HistoryStore* history)
    : data_store_(data_store)
    , system_provider_(system_provider)
    , details_provider_(details_provider)
    , killer_(killer)
    , history_(history)
{
    assert(data_store_ != nullptr);
    assert(system_provider_ != nullptr);
    assert(details_provider_ != nullptr);
    assert(killer_ != nullptr);
}

TuiApp::~TuiApp() {
    stop_find_scan();
    cleanup_windows();
}

void TuiApp::run() {
    // Load persisted settings (issue #1)
    settings_.load();
    view_model_.process_list.show_kernel_threads = settings_.get_bool("show_kernel_threads", true);
    view_model_.system_panel.is_visible = settings_.get_bool("tui.system_panel", true);
    system_panel_expanded_ = settings_.get_bool("tui.system_panel_expanded", false);
    diff_highlight_enabled_ = settings_.get_bool("diff_highlight", true);

    // Initialize ncurses
    if (!initscr()) {
        fprintf(stderr, "pexc: initscr() failed\n");
        return;
    }

    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);  // Hide cursor
    timeout(50);  // Blocking input with 50 ms timeout (paces the main loop)
    mouseinterval(200);  // Double-click timeout in milliseconds

    // Enable mouse support: only the button/wheel events we act on, never
    // movement reporting (would flood the input queue).
    mmask_t wanted = BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON1_CLICKED |
                     BUTTON1_DOUBLE_CLICKED | BUTTON4_PRESSED;
#ifdef BUTTON5_PRESSED
    wanted |= BUTTON5_PRESSED;
#endif
    mousemask(wanted, nullptr);

#ifdef _WIN32
    // Disable console QuickEdit mode: with it on (the Windows default), a
    // stray click/drag puts the console into text-selection mode, which
    // FREEZES the application until Esc/Enter. Keep mouse + window input.
    if (HANDLE hin = GetStdHandle(STD_INPUT_HANDLE); hin != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hin, &mode)) {
            mode &= ~ENABLE_QUICK_EDIT_MODE;
            mode |= ENABLE_EXTENDED_FLAGS | ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT;
            SetConsoleMode(hin, mode);
        }
    }
#endif

    // Initialize colors
    init_colors();

    // Set terminal title (like GUI version: "PEX: uname-info")
    const std::string title = "PEX: " + system_provider_->get_system_info_string();
#ifdef _WIN32
    SetConsoleTitleA(title.c_str());
#else
    printf("\033[?1000h");  // xterm mouse tracking (ncurses path)
    printf("\033]0;%s\007", title.c_str());
    fflush(stdout);

    // Set up resize handler using sigaction for portable behavior
    struct sigaction sa_resize{};
    sa_resize.sa_handler = handle_resize;
    sigemptyset(&sa_resize.sa_mask);
    sa_resize.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa_resize, nullptr);
#endif

    // Clear and refresh stdscr first to initialize the screen properly
    clear();
    refresh();

    // Create windows
    create_windows();

    // Start background services
    name_resolver_.start();
    data_store_->start();

    // Get initial data - wait for actual data (not just empty snapshot)
    int retries = 50;  // 5 seconds max
    current_data_ = data_store_->get_snapshot();
    while (retries-- > 0 && (!current_data_ || current_data_->process_count == 0)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        current_data_ = data_store_->get_snapshot();
    }

    if (!current_data_ || current_data_->process_count == 0) {
        // Restore terminal state before exiting
#ifndef _WIN32
        printf("\033[?1000l");  // Disable mouse tracking
        printf("\033]0;\007");   // Reset terminal title
        fflush(stdout);
#endif
        endwin();
        data_store_->stop();
        name_resolver_.stop();
        fprintf(stderr, "pexc: Failed to get process data from system\n");
        return;
    }

    view_model_.update_from_snapshot(current_data_);

    running_ = true;
    auto last_update = std::chrono::steady_clock::now();
    constexpr auto update_interval = std::chrono::milliseconds(100);

    bool needs_render = true;  // Initial paint

    while (running_) {
#ifndef _WIN32
        // Handle terminal resize (SIGWINCH path)
        if (g_resize_requested) {
            g_resize_requested = 0;
            endwin();
            refresh();
            resize_windows();
            needs_render = true;
        }
#endif

        // Read input: getch() blocks up to 50 ms (idle pacing). If a key
        // arrives, DRAIN the whole pending backlog with a non-blocking read
        // before rendering once - otherwise, when events (e.g. mouse) arrive
        // faster than a full-screen repaint completes, the console input
        // queue grows without bound and keypresses lag then appear dead.
        if (int ch = getch(); ch != ERR) {
            timeout(0);  // non-blocking for the drain
            int drained = 0;
            do {
                if (ch == KEY_RESIZE) {
                    resize_term(0, 0);
                    resize_windows();
                } else {
                    handle_input(ch);
                }
                needs_render = true;
            } while (++drained < 512 && (ch = getch()) != ERR);
            timeout(50);  // restore idle-pacing block
        }

        // Update data periodically
        auto now = std::chrono::steady_clock::now();
        if (now - last_update >= update_interval) {
            const auto new_data = data_store_->get_snapshot();
            if (new_data && (!current_data_ || new_data->timestamp != current_data_->timestamp)) {
                // Diff against the outgoing snapshot (issue #61); highlights
                // are then valid until the next swap = one refresh interval
                snapshot_diff_ = diff_highlight_enabled_
                    ? compute_snapshot_diff(current_data_, new_data.get())
                    : SnapshotDiff{};
                current_data_ = new_data;
                view_model_.update_from_snapshot(current_data_);
                needs_render = true;
            }
            last_update = now;
        }

        // Find-open-file worker progressed (issue #7)
        if (find_dirty_.exchange(false)) {
            needs_render = true;
        }

        // Render only when something changed
        if (needs_render) {
            render();
            needs_render = false;
        }
    }

    // Persist settings (issue #1)
    settings_.set_bool("show_kernel_threads", view_model_.process_list.show_kernel_threads);
    settings_.set_bool("tui.system_panel", view_model_.system_panel.is_visible);
    settings_.set_bool("tui.system_panel_expanded", system_panel_expanded_);
    settings_.set_bool("diff_highlight", diff_highlight_enabled_);
    settings_.save();

    // Cleanup
    stop_find_scan();
    data_store_->stop();
    name_resolver_.stop();
    cleanup_windows();

#ifndef _WIN32
    // Disable mouse tracking
    printf("\033[?1000l");
    fflush(stdout);
#endif

    endwin();

#ifndef _WIN32
    // Reset terminal title
    printf("\033]0;\007");
    fflush(stdout);
#endif
}

void TuiApp::render() {
    // Safety check: verify we have valid windows
    if (!process_win_ || !details_win_ || !status_win_) {
        // Terminal too small - create_windows() already showed message
        // Just refresh so the "terminal too small" message stays visible
        refresh();
        return;
    }

    // Clear all windows
    if (system_win_) werase(system_win_);
    werase(process_win_);
    werase(details_win_);
    werase(status_win_);

    // Render panels
    if (view_model_.system_panel.is_visible && system_win_) {
        render_system_panel();
    }

    render_process_tree();

    render_details_panel();
    render_status_bar();

    // Stage all windows into the virtual screen; the single doupdate() below
    // pushes one batched diff to the terminal. Calling wrefresh() per window
    // did one physical update each - 4-6 full transfers per frame, which is
    // very visible on large terminals (especially on Windows consoles).
    if (system_win_) wnoutrefresh(system_win_);
    wnoutrefresh(process_win_);
    wnoutrefresh(details_win_);
    wnoutrefresh(status_win_);

    // Overlays stage on top of the main windows (they also use wnoutrefresh)
    if (view_model_.kill_dialog.is_visible) {
        render_kill_dialog();
    }

    if (show_help_) {
        render_help_overlay();
    }

    if (search_mode_) {
        render_search_bar();
    }

    if (find_file_mode_) {
        render_find_file_bar();
    }

    if (find_results_visible_) {
        render_find_results_overlay();
    }

    // Single physical screen update for everything staged above
    doupdate();
}

} // namespace pex
