#pragma once

#include <cstdint>
#include <vector>

namespace pex {

struct CpuTimes {
    uint64_t user = 0;
    uint64_t nice = 0;
    uint64_t system = 0;
    uint64_t idle = 0;
    uint64_t iowait = 0;
    uint64_t irq = 0;
    uint64_t softirq = 0;
    uint64_t steal = 0;

    [[nodiscard]] uint64_t total() const {
        return user + nice + system + idle + iowait + irq + softirq + steal;
    }

    [[nodiscard]] uint64_t active() const {
        return user + nice + system + irq + softirq + steal;
    }
};

struct MemoryInfo {
    int64_t total = 0;
    int64_t available = 0;
    int64_t used = 0;
};

struct SwapInfo {
    int64_t total = 0;
    int64_t free = 0;
    int64_t used = 0;
};

struct LoadAverage {
    double one_min = 0.0;
    double five_min = 0.0;
    double fifteen_min = 0.0;
    int running_tasks = 0;
    int total_tasks = 0;
};

struct UptimeInfo {
    uint64_t uptime_seconds = 0;
    uint64_t idle_seconds = 0;
};

class SystemInfo {
public:
    static SystemInfo& instance();

    static CpuTimes get_cpu_times();
    static std::vector<CpuTimes> get_per_cpu_times();
    static MemoryInfo get_memory_info();
    static SwapInfo get_swap_info();
    static LoadAverage get_load_average();
    static UptimeInfo get_uptime();

    [[nodiscard]] unsigned int get_processor_count() const;
    [[nodiscard]] long get_clock_ticks_per_second() const;
    [[nodiscard]] uint64_t get_boot_time_ticks() const;

private:
    SystemInfo();
    unsigned int processor_count_ = 1;
    long clock_ticks_ = 100;
    uint64_t boot_time_ticks_ = 0;
};

} // namespace pex
