#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace pex {

struct DataSnapshot;

// Compact per-process sample recorded every collection tick.
struct ProcessSample {
    int pid = 0;
    float cpu_user_percent = 0.0f;    // Share of all cores (0-100)
    float cpu_kernel_percent = 0.0f;  // Share of all cores (0-100)
    float memory_percent = 0.0f;
    int64_t resident_memory = 0;
    float io_read_rate = 0.0f;        // bytes/sec
    float io_write_rate = 0.0f;       // bytes/sec
};

// One tick of recorded history: system aggregates + all process samples.
struct HistorySample {
    std::chrono::system_clock::time_point wall_time;
    float cpu_usage = 0.0f;
    int64_t memory_used = 0;
    int64_t memory_total = 0;
    int process_count = 0;
    int thread_count = 0;
    std::vector<float> per_cpu_user;
    std::vector<float> per_cpu_system;
    std::vector<ProcessSample> processes;
};

// Aligned series for charting a set of PIDs (values summed across the set).
// Vectors run oldest -> newest and all have the same length.
struct ProcessHistorySeries {
    std::vector<float> cpu_user;       // % of all cores
    std::vector<float> cpu_kernel;     // % of all cores
    std::vector<float> memory_percent;
    std::vector<std::vector<float>> per_cpu_user;    // [cpu][tick] system-wide
    std::vector<std::vector<float>> per_cpu_kernel;  // [cpu][tick] system-wide
};

// Metric selector for aggregation/series queries
enum class HistoryMetric {
    Cpu,      // user+kernel, % of all cores
    Memory,   // % of total memory
    IoRead,   // bytes/sec
    IoWrite   // bytes/sec
};

// Per-process aggregate over a window of ticks ("top consumers", issue #9)
struct ProcessAggregate {
    int pid = 0;
    std::string name;       // Last-known process name
    float avg_cpu = 0.0f;   // Averaged over the whole window (absent ticks = 0)
    float peak_cpu = 0.0f;
    float avg_mem = 0.0f;
    float peak_mem = 0.0f;
    float avg_io_read = 0.0f;
    float peak_io_read = 0.0f;
    float avg_io_write = 0.0f;
    float peak_io_write = 0.0f;
    size_t present_ticks = 0;  // Ticks in the window where the PID existed
};

// Ring buffer of system + per-process samples (issue #9).
// record() is called from the DataStore collection thread; all query and
// export methods are safe to call concurrently from the UI thread.
class HistoryStore {
public:
    explicit HistoryStore(size_t max_samples = kDefaultMaxSamples);

    // Record one tick. Called by DataStore after a snapshot is built.
    void record(const DataSnapshot& snapshot);

    [[nodiscard]] size_t sample_count() const;

    // Summed series for a set of PIDs (e.g. a process subtree), most recent
    // max_points ticks. Ticks where none of the PIDs existed contribute 0.
    [[nodiscard]] ProcessHistorySeries get_series(const std::vector<int>& pids,
                                                  size_t max_points) const;

    // Per-process aggregates over the most recent max_samples ticks.
    // Averages divide by the window tick count (a process alive for half the
    // window with 100% CPU averages 50%) - the "who ate my CPU" semantics.
    [[nodiscard]] std::vector<ProcessAggregate> aggregate(size_t max_samples) const;

    // Single-metric series for one PID over the most recent max_points ticks
    // (oldest -> newest, 0 where the PID did not exist). For sparklines.
    [[nodiscard]] std::vector<float> get_metric_series(int pid, HistoryMetric metric,
                                                       size_t max_points) const;

    // Export everything to two CSV files next to each other:
    //   <base>-system.csv    one row per tick (system aggregates)
    //   <base>-processes.csv one row per process per tick
    // Returns true on success; on failure fills 'error'.
    bool export_csv(const std::string& base_path, std::string& error) const;

    static constexpr size_t kDefaultMaxSamples = 600;

private:
    mutable std::mutex mutex_;
    std::deque<HistorySample> samples_;
    size_t max_samples_;
    std::unordered_map<int, std::string> process_names_;  // pid -> last-known name
};

} // namespace pex
