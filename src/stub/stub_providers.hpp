#pragma once

#include "../interfaces/i_process_data_provider.hpp"
#include "../interfaces/i_system_data_provider.hpp"
#include "../interfaces/i_process_killer.hpp"

namespace pex {

// Stub implementations for platforms without native support.
// These return empty/default data and are useful for testing UI without real data.

class StubProcessDataProvider : public IProcessDataProvider {
public:
    std::vector<ProcessInfo> get_all_processes(int64_t total_memory = -1) override;
    std::optional<ProcessInfo> get_process_info(int pid, int64_t total_memory) override;
    std::vector<ThreadInfo> get_threads(int pid) override;
    std::string get_thread_stack(int pid, int tid) override;
    std::vector<FileHandleInfo> get_file_handles(int pid) override;
    std::vector<NetworkConnectionInfo> get_network_connections(int pid) override;
    std::vector<MemoryMapInfo> get_memory_maps(int pid) override;
    std::vector<EnvironmentVariable> get_environment_variables(int pid) override;
    std::vector<LibraryInfo> get_libraries(int pid) override;
    std::vector<ParseError> get_recent_errors() override;
    void clear_errors() override;
};

class StubSystemDataProvider : public ISystemDataProvider {
public:
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
};

class StubProcessKiller : public IProcessKiller {
public:
    KillResult kill_process(int pid, bool force) override;
    KillResult kill_process_tree(int pid, bool force) override;
};

} // namespace pex
