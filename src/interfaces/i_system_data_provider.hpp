#pragma once

#include "../system_info.hpp"
#include <vector>

namespace pex {

class ISystemDataProvider {
public:
    virtual ~ISystemDataProvider() = default;

    virtual CpuTimes get_cpu_times() = 0;
    virtual std::vector<CpuTimes> get_per_cpu_times() = 0;
    virtual void get_per_cpu_times(std::vector<CpuTimes>& out) = 0;
    virtual MemoryInfo get_memory_info() = 0;
    virtual SwapInfo get_swap_info() = 0;
    virtual LoadAverage get_load_average() = 0;
    virtual UptimeInfo get_uptime() = 0;

    [[nodiscard]] virtual unsigned int get_processor_count() const = 0;
    [[nodiscard]] virtual long get_clock_ticks_per_second() const = 0;
    [[nodiscard]] virtual uint64_t get_boot_time_ticks() const = 0;
};

} // namespace pex
