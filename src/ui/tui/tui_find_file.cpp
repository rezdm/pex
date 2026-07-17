#include "tui_app.hpp"
#include "tui_colors.hpp"
#include "../../core/services/history_store.hpp"
#include "../../core/string_utils.hpp"
#include "../../platform/platform_factory.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <format>

namespace pex {

// ---------------------------------------------------------------------------
// History export (issue #9)
// ---------------------------------------------------------------------------

void TuiApp::export_history() {
    if (!history_) {
        status_message_ = "History recording is not enabled";
        status_message_time_ = std::chrono::steady_clock::now();
        return;
    }

    const char* home = std::getenv("HOME");
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm_val{};
    localtime_r(&now, &tm_val);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm_val);

    const std::string base = std::string(home ? home : ".") + "/pex-history-" + stamp;

    std::string error;
    if (history_->export_csv(base, error)) {
        status_message_ = "History exported to " + base + "-{system,processes}.csv";
    } else {
        status_message_ = "History export failed: " + error;
    }
    status_message_time_ = std::chrono::steady_clock::now();
}

// ---------------------------------------------------------------------------
// Find open file/handle (issue #7)
// ---------------------------------------------------------------------------

void TuiApp::stop_find_scan() {
    find_cancel_ = true;
    if (find_thread_.joinable()) {
        find_thread_.join();
    }
    find_cancel_ = false;
}

void TuiApp::start_find_scan() {
    if (!current_data_) return;
    stop_find_scan();  // Join any previous worker

    const std::string query = to_lower_copy(find_file_input_);
    if (query.empty()) return;

    if (!find_provider_) {
        find_provider_ = make_details_data_provider();
    }

    // Snapshot the PID list on the UI thread; the worker must not touch
    // current_data_ (the UI thread replaces it every tick).
    std::vector<std::pair<int, std::string>> targets;
    targets.reserve(current_data_->process_map.size());
    for (const auto& [pid, node] : current_data_->process_map) {
        targets.emplace_back(pid, node->info.name);
    }
    std::ranges::sort(targets);

    {
        std::lock_guard lock(find_mutex_);
        find_results_.clear();
        find_status_ = std::format("Scanning {} processes...", targets.size());
    }
    find_selected_idx_ = 0;
    find_scroll_ = 0;
    find_results_visible_ = true;
    find_running_ = true;

    find_thread_ = std::thread([this, query, targets = std::move(targets)]() {
        size_t scanned = 0;
        size_t total_matches = 0;

        for (const auto& [pid, name] : targets) {
            if (find_cancel_.load()) break;

            std::vector<FindFileResult> batch;

            for (const auto& fh : find_provider_->get_file_handles(pid)) {
                if (to_lower_copy(fh.path).find(query) != std::string::npos) {
                    batch.push_back({pid, name, fh.type, fh.path});
                }
            }
            for (const auto& lib : find_provider_->get_libraries(pid)) {
                if (to_lower_copy(lib.path).find(query) != std::string::npos) {
                    batch.push_back({pid, name, "library", lib.path});
                }
            }

            scanned++;
            bool full = false;
            {
                std::lock_guard lock(find_mutex_);
                for (auto& r : batch) {
                    if (find_results_.size() >= kMaxFindResults) break;
                    find_results_.push_back(std::move(r));
                }
                total_matches = find_results_.size();
                find_status_ = std::format("Scanning... {}/{} processes, {} matches",
                                           scanned, targets.size(), total_matches);
                if (find_results_.size() >= kMaxFindResults) {
                    find_status_ = std::format("Stopped at {} matches (refine the query)",
                                               kMaxFindResults);
                    full = true;
                }
            }
            find_dirty_ = true;
            if (full) break;
        }

        {
            std::lock_guard lock(find_mutex_);
            if (find_cancel_.load()) {
                find_status_ = std::format("Cancelled: {} matches in {} scanned processes",
                                           total_matches, scanned);
            } else if (total_matches < kMaxFindResults) {
                find_status_ = std::format("Done: {} matches in {} processes",
                                           total_matches, scanned);
            }
        }
        find_running_ = false;
        find_dirty_ = true;
    });
}

void TuiApp::handle_find_file_input(int ch) {
    switch (ch) {
        case 27:  // Escape
            find_file_mode_ = false;
            curs_set(0);
            break;

        case '\n':
        case '\r':
            find_file_mode_ = false;
            curs_set(0);
            if (!find_file_input_.empty()) {
                start_find_scan();
            }
            break;

        case KEY_BACKSPACE:
        case 127:
        case '\b':
            if (!find_file_input_.empty()) {
                find_file_input_.pop_back();
            }
            break;

        default:
            if (ch >= 32 && ch < 127) {
                find_file_input_ += static_cast<char>(ch);
            }
            break;
    }
}

void TuiApp::handle_find_results_input(int ch) {
    int result_count;
    {
        std::lock_guard lock(find_mutex_);
        result_count = static_cast<int>(find_results_.size());
    }

    switch (ch) {
        case 27:  // Escape - cancel scan / close overlay
        case 'q':
            if (find_running_.load()) {
                find_cancel_ = true;
            }
            find_results_visible_ = false;
            break;

        case KEY_UP:
        case 'k':
            if (find_selected_idx_ > 0) find_selected_idx_--;
            break;

        case KEY_DOWN:
        case 'j':
            if (find_selected_idx_ < result_count - 1) find_selected_idx_++;
            break;

        case KEY_PPAGE:
            find_selected_idx_ = std::max(0, find_selected_idx_ - 10);
            break;

        case KEY_NPAGE:
            if (result_count > 0) {
                find_selected_idx_ = std::min(result_count - 1, find_selected_idx_ + 10);
            }
            break;

        case '\n':
        case '\r':
            // Jump to the owning process in the main view
            if (find_selected_idx_ >= 0 && find_selected_idx_ < result_count) {
                int pid;
                {
                    std::lock_guard lock(find_mutex_);
                    pid = find_results_[find_selected_idx_].pid;
                }
                if (current_data_ && current_data_->process_map.contains(pid)) {
                    view_model_.process_list.selected_pid = pid;
                    expand_ancestors(pid);
                    scroll_to_selection();
                    details_needs_refresh_ = true;
                }
                find_results_visible_ = false;
            }
            break;

        case KEY_MOUSE: {
            MEVENT event;
            getmouse(&event);  // Consume; overlay has no mouse support
            break;
        }
        default: ;
    }
}

void TuiApp::render_find_file_bar() const {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    const int bar_y = max_y - 2;
    const int bar_width = std::min(60, max_x - 4);
    const int bar_x = (max_x - bar_width) / 2;

    WINDOW* win = newwin(3, bar_width, bar_y, bar_x);
    if (!win) return;

    wbkgd(win, COLOR_PAIR(COLOR_PAIR_DIALOG));
    box(win, 0, 0);

    mvwprintw(win, 0, 2, " Find open file (path substring) ");
    mvwprintw(win, 1, 2, "> %s", find_file_input_.c_str());

    curs_set(1);
    wmove(win, 1, 4 + static_cast<int>(find_file_input_.length()));

    wrefresh(win);
    delwin(win);
}

void TuiApp::render_find_results_overlay() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    const int width = std::min(100, max_x - 4);
    const int height = std::min(30, max_y - 4);
    if (width < 30 || height < 6) return;
    const int x = (max_x - width) / 2;
    const int y = (max_y - height) / 2;

    WINDOW* win = newwin(height, width, y, x);
    if (!win) return;

    wbkgd(win, COLOR_PAIR(COLOR_PAIR_DIALOG));
    box(win, 0, 0);

    wattron(win, A_BOLD);
    mvwprintw(win, 0, 2, " Find: %s ", find_file_input_.c_str());
    wattroff(win, A_BOLD);

    std::lock_guard lock(find_mutex_);

    // Status line
    mvwprintw(win, 1, 2, "%.*s", width - 4, find_status_.c_str());

    const int list_top = 2;
    const int list_rows = height - list_top - 2;
    const int count = static_cast<int>(find_results_.size());

    // Keep selection visible
    if (find_selected_idx_ >= count) find_selected_idx_ = std::max(0, count - 1);
    if (find_selected_idx_ < find_scroll_) find_scroll_ = find_selected_idx_;
    if (find_selected_idx_ >= find_scroll_ + list_rows) {
        find_scroll_ = find_selected_idx_ - list_rows + 1;
    }

    for (int i = 0; i < list_rows && find_scroll_ + i < count; i++) {
        const int idx = find_scroll_ + i;
        const auto& r = find_results_[idx];

        char line[512];
        snprintf(line, sizeof(line), "%-16.16s %7d %-8.8s %s",
                 r.process_name.c_str(), r.pid, r.type.c_str(), r.path.c_str());

        if (idx == find_selected_idx_) {
            wattron(win, COLOR_PAIR(COLOR_PAIR_SELECTED));
            mvwhline(win, list_top + i, 1, ' ', width - 2);
        }
        mvwprintw(win, list_top + i, 2, "%.*s", width - 4, line);
        if (idx == find_selected_idx_) {
            wattroff(win, COLOR_PAIR(COLOR_PAIR_SELECTED));
        }
    }

    wattron(win, COLOR_PAIR(COLOR_PAIR_DIALOG_BUTTON));
    mvwprintw(win, height - 1, 2, " Enter:Go to process  Esc:%s ",
              find_running_.load() ? "Cancel scan" : "Close");
    wattroff(win, COLOR_PAIR(COLOR_PAIR_DIALOG_BUTTON));

    wrefresh(win);
    delwin(win);
}

} // namespace pex
