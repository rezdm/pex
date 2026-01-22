#pragma once

#include "process_info.hpp"
#include <vector>
#include <map>
#include <optional>
#include <string>
#include <chrono>
#include <mutex>

namespace pex {

// Parse error information for status bar display
struct ParseError {
    std::chrono::steady_clock::time_point timestamp;
    std::string message;
};

class ProcfsReader {
public:
    std::vector<ProcessInfo> get_all_processes(int64_t total_memory = -1);
    std::optional<ProcessInfo> get_process_info(int pid);
    std::optional<ProcessInfo> get_process_info(int pid, int64_t total_memory);

    std::vector<ThreadInfo> get_threads(int pid);
    static std::string get_thread_stack(int pid, int tid);

    static std::vector<FileHandleInfo> get_file_handles(int pid);

    static std::vector<NetworkConnectionInfo> get_network_connections(int pid);

    std::vector<MemoryMapInfo> get_memory_maps(int pid);

    std::vector<EnvironmentVariable> get_environment_variables(int pid);

    std::vector<LibraryInfo> get_libraries(int pid);

    // Error reporting
    std::vector<ParseError> get_recent_errors();
    void clear_errors();

private:
    static std::string read_file(const std::string& path);

    static std::string read_symlink(const std::string& path);
    std::string get_username(int uid);
    std::map<int, std::string> uid_cache_;

    static std::map<int, NetworkConnectionInfo> parse_net_file(const std::string& path, const std::string& protocol);

    // Error tracking
    void add_error(const std::string& message);
    mutable std::mutex errors_mutex_;
    std::vector<ParseError> recent_errors_;
    static constexpr size_t kMaxErrors = 10;
};

} // namespace pex
