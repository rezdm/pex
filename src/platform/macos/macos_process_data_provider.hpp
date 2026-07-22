#pragma once

#include "../interfaces/i_process_data_provider.hpp"
#include <vector>
#include <mutex>
#include <map>
#include <string>

namespace pex {

// macOS process data via libproc + KERN_PROCARGS2. CPU times are reported in
// CLK_TCK ticks (converted from libproc nanoseconds) so DataStore's percentage
// math matches MacosSystemDataProvider's host_statistics ticks.
class MacosProcessDataProvider : public IProcessDataProvider {
public:
    MacosProcessDataProvider();
    ~MacosProcessDataProvider() override;

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
    bool fill_from_libproc(int pid, int64_t total_memory, ProcessInfo& info);
    std::string get_username(uint32_t uid);
    [[nodiscard]] uint64_t ns_to_ticks(uint64_t ns) const;

    // Parse KERN_PROCARGS2 for one pid. `buf` is a caller-owned scratch buffer
    // (sized >= argmax_) reused across a scan to avoid per-process
    // reallocation. Either output pointer may be null to skip that field.
    // Returns false when the process's args are unreadable (foreign uid without
    // root, or the process exited).
    bool read_proc_args(int pid, std::vector<char>& buf,
                        std::string* out_cmdline,
                        std::vector<EnvironmentVariable>* out_env);

    void add_error(const std::string& context, const std::string& message);

    std::mutex errors_mutex_;
    std::vector<ParseError> recent_errors_;
    static constexpr size_t kMaxErrors = 10;

    mutable std::mutex username_cache_mutex_;
    std::map<uint32_t, std::string> username_cache_;

    long clock_ticks_ = 100;
    uint64_t ns_per_tick_ = 10000000ULL;  // 1e9 / clock_ticks_
    size_t argmax_ = 256 * 1024;
};

} // namespace pex
