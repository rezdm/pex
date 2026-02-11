#pragma once

#include "../interfaces/i_system_data_provider.hpp"
#include <string>

namespace pex {

class OpenVMSSystemDataProvider : public ISystemDataProvider {
public:
    OpenVMSSystemDataProvider();
    ~OpenVMSSystemDataProvider() override = default;

    // Non-copyable
    OpenVMSSystemDataProvider(const OpenVMSSystemDataProvider&) = delete;
    OpenVMSSystemDataProvider& operator=(const OpenVMSSystemDataProvider&) = delete;

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
    mutable std::string cached_system_string_;
    mutable unsigned int cached_cpu_count_ = 0;
};

} // namespace pex
