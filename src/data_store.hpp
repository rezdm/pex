#pragma once

#include "process_info.hpp"
#include "procfs_reader.hpp"
#include "system_info.hpp"
#include <vector>
#include <map>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <functional>

namespace pex {

struct ProcessNode {
    ProcessInfo info;
    std::vector<std::unique_ptr<ProcessNode>> children;
    bool is_expanded = true;

    // Tree aggregate values
    int64_t tree_working_set = 0;
    double tree_memory_percent = 0.0;
    double tree_cpu_percent = 0.0;
    double tree_total_cpu_percent = 0.0;

    // Deep copy for thread-safe snapshot
    [[nodiscard]] std::unique_ptr<ProcessNode> clone() const;
};

// Snapshot of all system data - returned to UI
struct DataSnapshot {
    std::vector<std::unique_ptr<ProcessNode>> process_tree;
    std::map<int, ProcessNode*> process_map;  // Points into process_tree

    // System stats
    int process_count = 0;
    int thread_count = 0;
    int running_count = 0;
    double cpu_usage = 0.0;
    int64_t memory_used = 0;
    int64_t memory_total = 0;

    // Per-CPU usage (total, user, system percentages)
    std::vector<double> per_cpu_usage;
    std::vector<double> per_cpu_user;
    std::vector<double> per_cpu_system;

    // Additional system info
    SwapInfo swap_info;
    LoadAverage load_average;
    UptimeInfo uptime_info;

    // Timestamp of this snapshot
    std::chrono::steady_clock::time_point timestamp;
};

class DataStore {
public:
    DataStore();
    ~DataStore();

    // Start/stop the background collection thread
    void start();
    void stop();

    // Set refresh interval in milliseconds
    void set_refresh_interval(int ms);
    [[nodiscard]] int get_refresh_interval() const;

    // Get a snapshot of current data (thread-safe)
    [[nodiscard]] std::shared_ptr<DataSnapshot> get_snapshot() const;

    // Force an immediate refresh
    void refresh_now();

    // Pause/resume data collection
    void pause();
    void resume();
    [[nodiscard]] bool is_paused() const;

    // Register callback for when new data is available
    void set_on_data_updated(std::function<void()> callback);

    // Get recent parse errors for status bar display
    [[nodiscard]] std::vector<ParseError> get_recent_errors();

private:
    void collection_thread_func();
    void collect_data();
    static void calculate_tree_totals(ProcessNode& node);
    static void build_process_map(ProcessNode* node, std::map<int, ProcessNode*>& map);

    // Background thread
    std::thread collection_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<int> refresh_interval_ms_{1000};
    std::condition_variable cv_;
    std::mutex cv_mutex_;

    // Data storage with mutex protection
    mutable std::mutex data_mutex_;
    std::shared_ptr<DataSnapshot> current_snapshot_;

    // For CPU delta calculations (pre-allocated, reused each tick)
    CpuTimes previous_system_cpu_times_;
    std::vector<CpuTimes> previous_per_cpu_times_;
    std::vector<CpuTimes> current_per_cpu_times_;  // Reused buffer
    std::vector<double> per_cpu_usage_buffer_;     // Reused buffer
    std::vector<double> per_cpu_user_buffer_;      // Reused buffer
    std::vector<double> per_cpu_system_buffer_;    // Reused buffer
    std::map<int, std::pair<uint64_t, uint64_t>> previous_cpu_times_;

    // Callback
    std::function<void()> on_data_updated_;

    // Reader instance
    ProcfsReader reader_;
};

} // namespace pex
