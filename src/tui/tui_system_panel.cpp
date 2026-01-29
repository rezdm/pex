#include "tui_app.hpp"
#include "tui_colors.hpp"
#include <sstream>
#include <iomanip>
#include <numeric>

namespace pex {

void TuiApp::render_system_panel() {
    if (!system_win_) return;

    const auto& sp = view_model_.system_panel;
    int max_x = getmaxx(system_win_);
    int max_y = getmaxy(system_win_);

    if (system_panel_expanded_) {
        // Expanded view: show all CPU bars
        size_t num_cpus = sp.per_cpu_usage.size();
        int bar_width = 15;
        int cpu_section_width = bar_width + 12;
        int cpus_per_row = std::max(1, (max_x - 2) / cpu_section_width);

        int row = 0;
        for (size_t i = 0; i < num_cpus && row < max_y - 2; ++i) {
            int col = static_cast<int>(i % cpus_per_row);
            if (i > 0 && col == 0) row++;

            int x = 1 + col * cpu_section_width;

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

        // Memory row
        row = (num_cpus > 0) ? static_cast<int>((num_cpus - 1) / cpus_per_row) + 1 : 0;
        if (row < max_y - 1) {
            double mem_pct = (sp.memory_total > 0)
                ? (static_cast<double>(sp.memory_used) / sp.memory_total * 100.0) : 0.0;

            std::ostringstream mem_label;
            mem_label << format_bytes(sp.memory_used) << "/" << format_bytes(sp.memory_total);

            mvwprintw(system_win_, row, 1, "Mem");
            draw_progress_bar(system_win_, row, 5, 20, mem_pct, COLOR_PAIR_MEM_BAR, mem_label.str());

            if (sp.swap_info.total > 0) {
                int swap_x = 5 + 20 + static_cast<int>(mem_label.str().length()) + 4;
                double swap_pct = static_cast<double>(sp.swap_info.used) / sp.swap_info.total * 100.0;

                std::ostringstream swap_label;
                swap_label << format_bytes(sp.swap_info.used) << "/" << format_bytes(sp.swap_info.total);

                mvwprintw(system_win_, row, swap_x, "Swap");
                draw_progress_bar(system_win_, row, swap_x + 5, 20, swap_pct, COLOR_PAIR_SWAP_BAR, swap_label.str());
            }
        }

        // Tasks row
        row++;
        if (row < max_y) {
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

            // Collapse hint
            wattron(system_win_, A_DIM);
            mvwprintw(system_win_, row, max_x - 14, "[c] collapse");
            wattroff(system_win_, A_DIM);
        }
    } else {
        // Collapsed view: compact summary
        int row = 0;

        // Row 0: Average CPU + Memory
        double avg_cpu = 0.0;
        if (!sp.per_cpu_usage.empty()) {
            avg_cpu = std::accumulate(sp.per_cpu_usage.begin(), sp.per_cpu_usage.end(), 0.0)
                      / sp.per_cpu_usage.size();
        }
        double avg_user = 0.0, avg_system = 0.0;
        if (!sp.per_cpu_user.empty()) {
            avg_user = std::accumulate(sp.per_cpu_user.begin(), sp.per_cpu_user.end(), 0.0)
                       / sp.per_cpu_user.size();
        }
        if (!sp.per_cpu_system.empty()) {
            avg_system = std::accumulate(sp.per_cpu_system.begin(), sp.per_cpu_system.end(), 0.0)
                         / sp.per_cpu_system.size();
        }

        std::ostringstream cpu_label;
        cpu_label << "CPU(" << sp.per_cpu_usage.size() << ")";
        mvwprintw(system_win_, row, 1, "%s", cpu_label.str().c_str());
        draw_cpu_bar(system_win_, row, 9, 20, avg_user, avg_system, "");

        std::ostringstream cpu_pct;
        cpu_pct << std::fixed << std::setprecision(0) << std::setw(3) << avg_cpu << "%";
        mvwprintw(system_win_, row, 30, "%s", cpu_pct.str().c_str());

        // Memory
        double mem_pct = (sp.memory_total > 0)
            ? (static_cast<double>(sp.memory_used) / sp.memory_total * 100.0) : 0.0;

        std::ostringstream mem_label;
        mem_label << format_bytes(sp.memory_used) << "/" << format_bytes(sp.memory_total);

        mvwprintw(system_win_, row, 38, "Mem");
        draw_progress_bar(system_win_, row, 42, 20, mem_pct, COLOR_PAIR_MEM_BAR, mem_label.str());

        // Swap (if exists)
        if (sp.swap_info.total > 0) {
            int swap_x = 42 + 20 + static_cast<int>(mem_label.str().length()) + 3;
            double swap_pct = static_cast<double>(sp.swap_info.used) / sp.swap_info.total * 100.0;

            std::ostringstream swap_label;
            swap_label << format_bytes(sp.swap_info.used) << "/" << format_bytes(sp.swap_info.total);

            mvwprintw(system_win_, row, swap_x, "Swap");
            draw_progress_bar(system_win_, row, swap_x + 5, 15, swap_pct, COLOR_PAIR_SWAP_BAR, swap_label.str());
        }

        // Row 1: Tasks, load, uptime
        row = 1;
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

        // Expand hint
        wattron(system_win_, A_DIM);
        mvwprintw(system_win_, row, max_x - 12, "[c] expand");
        wattroff(system_win_, A_DIM);

        // Row 2: Optional - show min/max CPU if space
        row = 2;
        if (!sp.per_cpu_usage.empty() && max_y > 2) {
            auto minmax = std::minmax_element(sp.per_cpu_usage.begin(), sp.per_cpu_usage.end());
            int min_idx = static_cast<int>(std::distance(sp.per_cpu_usage.begin(), minmax.first));
            int max_idx = static_cast<int>(std::distance(sp.per_cpu_usage.begin(), minmax.second));

            std::ostringstream minmax_str;
            minmax_str << std::fixed << std::setprecision(0)
                       << "CPU min: " << *minmax.first << "% (CPU" << min_idx << ")  "
                       << "max: " << *minmax.second << "% (CPU" << max_idx << ")";
            wattron(system_win_, A_DIM);
            mvwprintw(system_win_, row, 1, "%s", minmax_str.str().c_str());
            wattroff(system_win_, A_DIM);
        }
    }
}

} // namespace pex
