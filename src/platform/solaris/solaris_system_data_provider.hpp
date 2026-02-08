#pragma once

#include "../interfaces/i_system_data_provider.hpp"
#include <kstat.h>

namespace pex {

class SolarisSystemDataProvider : public ISystemDataProvider {
public:
    SolarisSystemDataProvider();
    ~SolarisSystemDataProvider();

    // Non-copyable due to kstat handle ownership
    SolarisSystemDataProvider(const SolarisSystemDataProvider&) = delete;
    SolarisSystemDataProvider& operator=(const SolarisSystemDataProvider&) = delete;

    CpuTimes get_cpu_times() override;
    std::vector<CpuTimes> get_per_cpu_times() override;
    void get_per_cpu_times(std::vector<CpuTimes>& out) override;
    MemoryInfo get_memory_info() override;
    SwapInfo get_swap_info() override;
    LoadAverage get_load_average() override;
    UptimeInfo get_uptime() override;
    unsigned int get_processor_count() const override;
    long get_clock_ticks_per_second() const override;
    uint64_t get_boot_time_ticks() const override;
    [[nodiscard]] std::string get_system_info_string() const override;

private:
    mutable kstat_ctl_t* kc_ = nullptr;
    void ensure_kstat() const;
};

} // namespace pex
