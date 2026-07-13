#pragma once

#include "../interfaces/i_process_data_provider.hpp"
#include <cstdint>
#include <vector>
#include <mutex>
#include <map>
#include <unordered_map>

namespace pex {

class FreeBSDProcessDataProvider : public IProcessDataProvider {
public:
    FreeBSDProcessDataProvider();
    ~FreeBSDProcessDataProvider() override;

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

private:
    char map_state(int state);
    std::string get_username(uid_t uid);
    std::string get_executable_path(int pid);
    void add_error(const std::string& context, const std::string& message);

    std::mutex errors_mutex_;
    std::vector<ParseError> recent_errors_;
    static constexpr size_t kMaxErrors = 10;

    // Username cache to avoid repeated getpwuid calls
    mutable std::mutex username_cache_mutex_;
    std::map<uid_t, std::string> username_cache_;

    // Exe path / command line cache keyed by pid and validated by start
    // time: both are stable for a process's lifetime, and fetching them
    // costs a sysctl + procstat round-trip per process per tick otherwise.
    // Only touched from get_all_processes (collection thread).
    struct ProcStrings {
        uint64_t start_us = 0;  // ki_start packed to microseconds
        std::string comm;       // ki_comm at capture: exec() keeps the start
                                // time but replaces comm, invalidating the entry
        std::string executable_path;
        std::string command_line;
    };
    std::unordered_map<int, ProcStrings> proc_strings_cache_;

    // Cached system configuration
    long clock_ticks_ = 100;
};

} // namespace pex
