#pragma once

#include "../../../core/model/system_info.hpp"
#include <string>
#include <vector>

namespace pex {

struct SystemPanelViewModel {
    // Visibility
    bool is_visible = true;

    // Per-CPU usage (percentages)
    std::vector<double> per_cpu_usage;
    std::vector<double> per_cpu_user;
    std::vector<double> per_cpu_system;

    // Memory stats
    int64_t memory_used = 0;
    int64_t memory_total = 0;

    // Swap stats
    SwapInfo swap_info;

    // Load average
    LoadAverage load_average;

    // Uptime
    UptimeInfo uptime_info;

    // Process counts
    int process_count = 0;
    int thread_count = 0;
    int running_count = 0;

    // Per-tick process churn from the kernel event feed (issue #60)
    bool events_active = false;
    int fork_events = 0;
    int exec_events = 0;
    int exit_events = 0;
    int short_lived_exits = 0;

    // Overall CPU usage
    double cpu_usage = 0.0;
};

// Compact per-tick churn string for the TUI status line, e.g.
// "Churn: +12/-11 (2 unseen)". Single source so the two panel layouts
// (expanded/collapsed) cannot drift.
inline std::string format_churn_compact(const SystemPanelViewModel& sp) {
    std::string s = "Churn: +" + std::to_string(sp.fork_events) +
                    "/-" + std::to_string(sp.exit_events);
    if (sp.short_lived_exits > 0) {
        s += " (" + std::to_string(sp.short_lived_exits) + " unseen)";
    }
    return s;
}

} // namespace pex
