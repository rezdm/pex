#pragma once

#include "../interfaces/i_system_data_provider.hpp"

namespace pex {

class LinuxSystemDataProvider : public ISystemDataProvider {
public:
    LinuxSystemDataProvider();
    ~LinuxSystemDataProvider() override = default;

    CpuTimes get_cpu_times() override;
    std::vector<CpuTimes> get_per_cpu_times() override;
    void get_per_cpu_times(std::vector<CpuTimes>& out) override;
    MemoryInfo get_memory_info() override;
    SwapInfo get_swap_info() override;
    LoadAverage get_load_average() override;
    UptimeInfo get_uptime() override;

    [[nodiscard]] unsigned int get_processor_count() const override;
    [[nodiscard]] long get_clock_ticks_per_second() const override;
    [[nodiscard]] uint64_t get_boot_time_ticks() const override;

private:
    // Cached values from SystemInfo singleton
    unsigned int processor_count_;
    long clock_ticks_per_second_;
    uint64_t boot_time_ticks_;
};

} // namespace pex
