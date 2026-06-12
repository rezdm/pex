#pragma once

#include "../interfaces/i_system_data_provider.hpp"

namespace pex {

// Windows system data provider (issue #43).
// CPU time unit: FILETIME ticks (100 ns) - consistent across this provider
// and the process provider, which is all DataStore's delta math requires.
class WindowsSystemDataProvider final : public ISystemDataProvider {
public:
    WindowsSystemDataProvider();

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
    [[nodiscard]] std::string get_system_info_string() const override;

private:
    unsigned int processor_count_ = 1;
};

} // namespace pex
