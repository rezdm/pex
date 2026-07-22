#pragma once

#include "../interfaces/i_system_data_provider.hpp"

namespace pex {

// macOS system data via Mach (host_statistics) + sysctl. CPU times are kept in
// CLK_TCK ticks; the process provider converts its nanosecond CPU times to the
// same unit so DataStore's percentage math is consistent across both.
class MacosSystemDataProvider : public ISystemDataProvider {
public:
    MacosSystemDataProvider();

    CpuTimes get_cpu_times() override;
    std::vector<CpuTimes> get_per_cpu_times() override;
    void get_per_cpu_times(std::vector<CpuTimes>& out) override;
    MemoryInfo get_memory_info() override;
    SwapInfo get_swap_info() override;
    LoadAverage get_load_average() override;
    UptimeInfo get_uptime() override;

    [[nodiscard]] unsigned int get_processor_count() const override;
    [[nodiscard]] long get_clock_ticks_per_second() const override;
    [[nodiscard]] uint64_t get_boot_time_seconds() const override;
    [[nodiscard]] std::string get_system_info_string() const override;

private:
    unsigned int processor_count_ = 1;
    long clock_ticks_ = 100;
};

} // namespace pex
