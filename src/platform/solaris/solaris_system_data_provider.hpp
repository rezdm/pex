#pragma once

#include "../interfaces/i_system_data_provider.hpp"
#include <kstat.h>
#include <vector>

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
    uint64_t get_boot_time_seconds() const override;
    [[nodiscard]] std::string get_system_info_string() const override;

private:
    mutable kstat_ctl_t* kc_ = nullptr;
    void ensure_kstat() const;

    // Stable slot assignment for per-CPU stats: kstat cpu_stat instance IDs
    // can be sparse and change as CPUs go on/offline. get_per_cpu_times keys
    // each CPU to a fixed slot by instance id so that slot N is the same
    // physical CPU on every tick — the collection thread diffs slot-to-slot,
    // so a shifting mapping would subtract counters of different CPUs.
    // Slots are only ever appended (an offlined CPU keeps its slot, zero-fill)
    // so indices never move under the delta math.
    mutable std::vector<int> cpu_slot_instances_;
};

} // namespace pex
