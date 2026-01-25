#pragma once

#include <vector>
#include <chrono>
#include <cstdint>

namespace pex {

struct ProcessPopupViewModel {
    // Visibility
    bool is_visible = false;

    // Target process
    int target_pid = -1;

    // Toggle: false = process only, true = process + descendants
    bool include_tree = true;

    // History buffers for charts
    static constexpr size_t kHistorySize = 60;  // 60 data points
    std::vector<float> cpu_user_history;
    std::vector<float> cpu_kernel_history;
    std::vector<float> memory_history;

    // Per-CPU history
    std::vector<std::vector<float>> per_cpu_user_history;
    std::vector<std::vector<float>> per_cpu_kernel_history;

    // Previous values for delta calculation
    uint64_t prev_utime = 0;
    uint64_t prev_stime = 0;

    // Last update timestamp for rate limiting
    std::chrono::steady_clock::time_point last_update;

    // Clear all history when changing target
    void clear_history() {
        cpu_user_history.clear();
        cpu_kernel_history.clear();
        memory_history.clear();
        per_cpu_user_history.clear();
        per_cpu_kernel_history.clear();
        prev_utime = 0;
        prev_stime = 0;
    }
};

} // namespace pex
