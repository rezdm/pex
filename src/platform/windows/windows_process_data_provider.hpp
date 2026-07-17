#pragma once

#include "../interfaces/i_process_data_provider.hpp"
#include "../../core/model/errors.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace pex {

// Windows process data provider (issue #43).
// Process enumeration uses a single NtQuerySystemInformation call; the
// Details tabs (open handles, memory maps, environment, network, threads,
// libraries) are implemented via the native APIs.
class WindowsProcessDataProvider final : public IProcessDataProvider {
public:
    WindowsProcessDataProvider();

    std::vector<ProcessInfo> get_all_processes(int64_t total_memory) override;
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
    void add_error(const std::string& context, const std::string& message);
    std::string get_username_for_pid(void* process_handle);
    void fill_process_details(ProcessInfo& info, void* process_handle, int64_t total_memory);

    std::mutex errors_mutex_;
    std::vector<ParseError> recent_errors_;
    static constexpr size_t kMaxErrors = 10;

    // SID-string -> username cache to avoid repeated LookupAccountSid calls
    std::mutex username_cache_mutex_;
    std::map<std::string, std::string> username_cache_;

    // Per-process cache of the immutable strings (user, exe path, command
    // line), keyed by pid and validated by process creation time. These never
    // change for a process instance, so caching them means get_all_processes
    // opens no per-process handle in steady state. Collection thread only.
    struct ProcStrings {
        uint64_t create_time = 0;  // CreateTime (FILETIME ticks) at capture
        std::string user_name;
        std::string executable_path;
        std::string command_line;
    };
    std::unordered_map<int, ProcStrings> proc_strings_;
};

} // namespace pex
