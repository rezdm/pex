#pragma once

#include "../process_info.hpp"
#include "../errors.hpp"
#include <vector>
#include <optional>
#include <string>

namespace pex {

class IProcessDataProvider {
public:
    virtual ~IProcessDataProvider() = default;

    virtual std::vector<ProcessInfo> get_all_processes(int64_t total_memory = -1) = 0;
    virtual std::optional<ProcessInfo> get_process_info(int pid, int64_t total_memory) = 0;

    virtual std::vector<ThreadInfo> get_threads(int pid) = 0;
    virtual std::string get_thread_stack(int pid, int tid) = 0;

    virtual std::vector<FileHandleInfo> get_file_handles(int pid) = 0;
    virtual std::vector<NetworkConnectionInfo> get_network_connections(int pid) = 0;
    virtual std::vector<MemoryMapInfo> get_memory_maps(int pid) = 0;
    virtual std::vector<EnvironmentVariable> get_environment_variables(int pid) = 0;
    virtual std::vector<LibraryInfo> get_libraries(int pid) = 0;

    virtual std::vector<ParseError> get_recent_errors() = 0;
    virtual void clear_errors() = 0;
};

} // namespace pex
