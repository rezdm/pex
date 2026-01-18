#pragma once

#include <cstdint>

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

    uint64_t total() const {
        return user + nice + system + idle + iowait + irq + softirq + steal;
    }

    uint64_t active() const {
        return user + nice + system + irq + softirq + steal;
    }
};

struct MemoryInfo {
    int64_t total = 0;
    int64_t available = 0;
    int64_t used = 0;
};

class SystemInfo {
public:
    static SystemInfo& instance();

    static CpuTimes get_cpu_times();

    static MemoryInfo get_memory_info();
    [[nodiscard]] int get_processor_count() const;
    [[nodiscard]] long get_clock_ticks_per_second() const;
    [[nodiscard]] uint64_t get_boot_time_ticks() const;

private:
    SystemInfo();
    int processor_count_ = 1;
    long clock_ticks_ = 100;
    uint64_t boot_time_ticks_ = 0;
};

} // namespace pex
