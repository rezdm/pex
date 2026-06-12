#pragma once

#include "../interfaces/i_process_data_provider.hpp"
#include "../../core/model/errors.hpp"

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace pex {

// Windows process data provider (issue #43, Phase 1).
// Implemented: process tree, CPU/memory/threads/user/start time, I/O
// counters, per-PID network connections, loaded modules.
// Phase 2 (returns empty for now): open handles, memory maps, environment.
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
};

} // namespace pex
