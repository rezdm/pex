#include "smg_app.hpp"
#include "smg_colors.hpp"
#include <sstream>
#include <iomanip>
#include <numeric>
#include <algorithm>

namespace pex {

void SmgApp::render_system_panel() {
    if (!system_display_) return;

    const auto& sp = view_model_.system_panel;

    if (system_panel_expanded_) {
        // Expanded view: show all CPU bars
        size_t num_cpus = sp.per_cpu_usage.size();
        int bar_width = 15;
        int cpu_section_width = bar_width + 12;
        int cpus_per_row = std::max(1, (term_cols_ - 2) / cpu_section_width);

        int row = 1;  // SMG$ 1-based
        for (size_t i = 0; i < num_cpus; ++i) {
            int col_idx = static_cast<int>(i % cpus_per_row);
            if (i > 0 && col_idx == 0) row++;

            int x = 1 + col_idx * cpu_section_width;

            double user_pct = (i < sp.per_cpu_user.size()) ? sp.per_cpu_user[i] : 0.0;
            double system_pct = (i < sp.per_cpu_system.size()) ? sp.per_cpu_system[i] : 0.0;
            double total_pct = (i < sp.per_cpu_usage.size()) ? sp.per_cpu_usage[i] : 0.0;

            // Label
            std::ostringstream label;
            label << "CPU" << std::setw(2) << i;
            smg_put_chars(system_display_, row, x, label.str(), SMG_REND_NORMAL);

            // Bar
            draw_cpu_bar(system_display_, row, x + 6, bar_width, user_pct, system_pct, "");

            // Percentage
            std::ostringstream pct;
            pct << std::fixed << std::setprecision(0) << std::setw(3) << total_pct << "%";
            smg_put_chars(system_display_, row, x + 6 + bar_width + 1, pct.str(), SMG_REND_NORMAL);
        }

        // Memory row
        row = (num_cpus > 0) ? static_cast<int>((num_cpus - 1) / cpus_per_row) + 2 : 1;
        {
            double mem_pct = (sp.memory_total > 0)
                ? (static_cast<double>(sp.memory_used) / sp.memory_total * 100.0) : 0.0;

            std::ostringstream mem_label;
            mem_label << format_bytes(sp.memory_used) << "/" << format_bytes(sp.memory_total);

            smg_put_chars(system_display_, row, 1, "Mem", SMG_REND_NORMAL);
            draw_progress_bar(system_display_, row, 5, 20, mem_pct, SMG_REND_MEM_BAR, mem_label.str());

            if (sp.swap_info.total > 0) {
                int swap_x = 5 + 20 + static_cast<int>(mem_label.str().length()) + 4;
                double swap_pct = static_cast<double>(sp.swap_info.used) / sp.swap_info.total * 100.0;

                std::ostringstream swap_label;
                swap_label << format_bytes(sp.swap_info.used) << "/" << format_bytes(sp.swap_info.total);

                smg_put_chars(system_display_, row, swap_x, "Swap", SMG_REND_NORMAL);
                draw_progress_bar(system_display_, row, swap_x + 5, 20, swap_pct, SMG_REND_SWAP_BAR, swap_label.str());
            }
        }

        // Tasks row
        row++;
        {
            std::ostringstream tasks;
            tasks << "Tasks: " << sp.process_count << ", " << sp.thread_count << " thr; "
                  << sp.running_count << " running";
            smg_put_chars(system_display_, row, 1, tasks.str(), SMG_REND_NORMAL);

            std::ostringstream load;
            load << std::fixed << std::setprecision(2)
                 << "Load: " << sp.load_average.one_min << " "
                 << sp.load_average.five_min << " "
                 << sp.load_average.fifteen_min;

            int load_x = static_cast<int>(tasks.str().length()) + 4;
            smg_put_chars(system_display_, row, load_x, load.str(), SMG_REND_NORMAL);

            std::ostringstream uptime;
            uptime << "Uptime: " << format_uptime(sp.uptime_info.uptime_seconds);
            int uptime_x = load_x + static_cast<int>(load.str().length()) + 4;
            smg_put_chars(system_display_, row, uptime_x, uptime.str(), SMG_REND_NORMAL);

            // Collapse hint
            smg_put_chars(system_display_, row, term_cols_ - 14, "[c] collapse", SMG_REND_DIM);
        }
    } else {
        // Collapsed view: compact summary
        int row = 1;

        // Row 1: Average CPU + Memory
        double avg_cpu = 0.0;
        if (!sp.per_cpu_usage.empty()) {
            avg_cpu = std::accumulate(sp.per_cpu_usage.begin(), sp.per_cpu_usage.end(), 0.0)
                      / static_cast<double>(sp.per_cpu_usage.size());
        }
        double avg_user = 0.0, avg_system = 0.0;
        if (!sp.per_cpu_user.empty()) {
            avg_user = std::accumulate(sp.per_cpu_user.begin(), sp.per_cpu_user.end(), 0.0)
                       / static_cast<double>(sp.per_cpu_user.size());
        }
        if (!sp.per_cpu_system.empty()) {
            avg_system = std::accumulate(sp.per_cpu_system.begin(), sp.per_cpu_system.end(), 0.0)
                         / static_cast<double>(sp.per_cpu_system.size());
        }

        std::ostringstream cpu_label;
        cpu_label << "CPU(" << sp.per_cpu_usage.size() << ")";
        smg_put_chars(system_display_, row, 1, cpu_label.str(), SMG_REND_NORMAL);
        draw_cpu_bar(system_display_, row, 9, 20, avg_user, avg_system, "");

        std::ostringstream cpu_pct;
        cpu_pct << std::fixed << std::setprecision(0) << std::setw(3) << avg_cpu << "%";
        smg_put_chars(system_display_, row, 30, cpu_pct.str(), SMG_REND_NORMAL);

        // Memory
        double mem_pct = (sp.memory_total > 0)
            ? (static_cast<double>(sp.memory_used) / sp.memory_total * 100.0) : 0.0;

        std::ostringstream mem_label;
        mem_label << format_bytes(sp.memory_used) << "/" << format_bytes(sp.memory_total);

        smg_put_chars(system_display_, row, 38, "Mem", SMG_REND_NORMAL);
        draw_progress_bar(system_display_, row, 42, 20, mem_pct, SMG_REND_MEM_BAR, mem_label.str());

        // Swap (if exists)
        if (sp.swap_info.total > 0) {
            int swap_x = 42 + 20 + static_cast<int>(mem_label.str().length()) + 3;
            double swap_pct = static_cast<double>(sp.swap_info.used) / sp.swap_info.total * 100.0;

            std::ostringstream swap_label;
            swap_label << format_bytes(sp.swap_info.used) << "/" << format_bytes(sp.swap_info.total);

            smg_put_chars(system_display_, row, swap_x, "Swap", SMG_REND_NORMAL);
            draw_progress_bar(system_display_, row, swap_x + 5, 15, swap_pct, SMG_REND_SWAP_BAR, swap_label.str());
        }

        // Row 2: Tasks, load, uptime
        row = 2;
        std::ostringstream tasks;
        tasks << "Tasks: " << sp.process_count << ", " << sp.thread_count << " thr; "
              << sp.running_count << " running";
        smg_put_chars(system_display_, row, 1, tasks.str(), SMG_REND_NORMAL);

        std::ostringstream load;
        load << std::fixed << std::setprecision(2)
             << "Load: " << sp.load_average.one_min << " "
             << sp.load_average.five_min << " "
             << sp.load_average.fifteen_min;

        int load_x = static_cast<int>(tasks.str().length()) + 4;
        smg_put_chars(system_display_, row, load_x, load.str(), SMG_REND_NORMAL);

        std::ostringstream uptime;
        uptime << "Uptime: " << format_uptime(sp.uptime_info.uptime_seconds);
        int uptime_x = load_x + static_cast<int>(load.str().length()) + 4;
        smg_put_chars(system_display_, row, uptime_x, uptime.str(), SMG_REND_NORMAL);

        // Expand hint
        smg_put_chars(system_display_, row, term_cols_ - 12, "[c] expand", SMG_REND_DIM);

        // Row 3: CPU min/max
        row = 3;
        if (!sp.per_cpu_usage.empty()) {
            auto minmax = std::minmax_element(sp.per_cpu_usage.begin(), sp.per_cpu_usage.end());
            int min_idx = static_cast<int>(std::distance(sp.per_cpu_usage.begin(), minmax.first));
            int max_idx = static_cast<int>(std::distance(sp.per_cpu_usage.begin(), minmax.second));

            std::ostringstream minmax_str;
            minmax_str << std::fixed << std::setprecision(0)
                       << "CPU min: " << *minmax.first << "% (CPU" << min_idx << ")  "
                       << "max: " << *minmax.second << "% (CPU" << max_idx << ")";
            smg_put_chars(system_display_, row, 1, minmax_str.str(), SMG_REND_DIM);
        }
    }
}

} // namespace pex
