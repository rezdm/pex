#include "tui_app.hpp"
#include "tui_colors.hpp"
#include <sstream>
#include <iomanip>

namespace pex {

void TuiApp::render_system_panel() {
    if (!system_win_) return;

    const auto& sp = view_model_.system_panel;
    int max_x = getmaxx(system_win_);

    // CPU bars - render in rows of 4 CPUs
    size_t num_cpus = sp.per_cpu_usage.size();
    int bar_width = 20;
    int cpu_section_width = bar_width + 12;  // "CPU XX" + bar + percentage

    // Determine how many CPUs per row fit
    int cpus_per_row = std::max(1, (max_x - 2) / cpu_section_width);

    int row = 0;
    for (size_t i = 0; i < num_cpus; ++i) {
        int col = static_cast<int>(i % cpus_per_row);
        if (i > 0 && col == 0) row++;

        int x = 1 + col * cpu_section_width;

        // Get usage percentages
        double user_pct = (i < sp.per_cpu_user.size()) ? sp.per_cpu_user[i] : 0.0;
        double system_pct = (i < sp.per_cpu_system.size()) ? sp.per_cpu_system[i] : 0.0;
        double total_pct = (i < sp.per_cpu_usage.size()) ? sp.per_cpu_usage[i] : 0.0;

        // Label
        std::ostringstream label;
        label << "CPU" << std::setw(2) << i;
        mvwprintw(system_win_, row, x, "%s", label.str().c_str());

        // Bar
        draw_cpu_bar(system_win_, row, x + 6, bar_width, user_pct, system_pct, "");

        // Percentage
        std::ostringstream pct;
        pct << std::fixed << std::setprecision(0) << std::setw(3) << total_pct << "%";
        mvwprintw(system_win_, row, x + 6 + bar_width + 1, "%s", pct.str().c_str());
    }

    // Memory and swap bar on next row
    row = (num_cpus > 0) ? ((num_cpus - 1) / cpus_per_row) + 1 : 0;

    // Memory bar
    double mem_pct = (sp.memory_total > 0)
        ? (static_cast<double>(sp.memory_used) / sp.memory_total * 100.0)
        : 0.0;

    std::ostringstream mem_label;
    mem_label << format_bytes(sp.memory_used) << "/" << format_bytes(sp.memory_total);

    mvwprintw(system_win_, row, 1, "Mem");
    draw_progress_bar(system_win_, row, 5, bar_width, mem_pct, COLOR_PAIR_MEM_BAR, mem_label.str());

    // Swap bar (if swap exists)
    if (sp.swap_info.total > 0) {
        int swap_x = 5 + bar_width + mem_label.str().length() + 4;
        double swap_pct = static_cast<double>(sp.swap_info.used) / sp.swap_info.total * 100.0;

        std::ostringstream swap_label;
        swap_label << format_bytes(sp.swap_info.used) << "/" << format_bytes(sp.swap_info.total);

        mvwprintw(system_win_, row, swap_x, "Swap");
        draw_progress_bar(system_win_, row, swap_x + 5, bar_width, swap_pct, COLOR_PAIR_SWAP_BAR, swap_label.str());
    }

    // Tasks, load, and uptime on the last row
    row++;

    std::ostringstream tasks;
    tasks << "Tasks: " << sp.process_count << ", " << sp.thread_count << " thr; "
          << sp.running_count << " running";
    mvwprintw(system_win_, row, 1, "%s", tasks.str().c_str());

    std::ostringstream load;
    load << std::fixed << std::setprecision(2)
         << "Load: " << sp.load_average.one_min << " "
         << sp.load_average.five_min << " "
         << sp.load_average.fifteen_min;

    int load_x = static_cast<int>(tasks.str().length()) + 4;
    mvwprintw(system_win_, row, load_x, "%s", load.str().c_str());

    std::ostringstream uptime;
    uptime << "Uptime: " << format_uptime(sp.uptime_info.uptime_seconds);
    int uptime_x = load_x + static_cast<int>(load.str().length()) + 4;
    mvwprintw(system_win_, row, uptime_x, "%s", uptime.str().c_str());
}

} // namespace pex
