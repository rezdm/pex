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

    // History buffers for charts. Matches HistoryStore::kDefaultMaxSamples so
    // the backfill shows the full recorded depth (~10 min at 1 s refresh).
    static constexpr size_t kHistorySize = 600;
    std::vector<float> cpu_user_history;
    std::vector<float> cpu_kernel_history;
    std::vector<float> memory_history;

    // Per-CPU history
    std::vector<std::vector<float>> per_cpu_user_history;
    std::vector<std::vector<float>> per_cpu_kernel_history;

    // Timestamp of the snapshot last appended to the charts. A new sample is
    // taken only when current_data_->timestamp advances — sampling on a fixed
    // wall-clock interval instead produced alternating ~2x spikes and zeros,
    // because the snapshot only changes once per refresh interval (issue #85).
    std::chrono::steady_clock::time_point last_sampled_time{};

    // Set when the target changes; the next update seeds the charts from the
    // HistoryStore so past data is visible immediately (issue #9)
    bool needs_backfill = false;

    // Clear all history when changing target
    void clear_history() {
        cpu_user_history.clear();
        cpu_kernel_history.clear();
        memory_history.clear();
        per_cpu_user_history.clear();
        per_cpu_kernel_history.clear();
        last_sampled_time = {};
        needs_backfill = true;
    }
};

} // namespace pex
