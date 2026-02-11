#pragma once

#include "../interfaces/i_process_data_provider.hpp"
#include "vms_mutex.hpp"
#include <vector>
#include <map>

namespace pex {

class OpenVMSProcessDataProvider : public IProcessDataProvider {
public:
    OpenVMSProcessDataProvider();
    ~OpenVMSProcessDataProvider() override;

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
    static char map_state(unsigned int vms_state);
    static std::string trim_vms_string(const char* str, int len);

    void add_error(const std::string& context, const std::string& message);

    PEX_MUTEX errors_mutex_;
    std::vector<ParseError> recent_errors_;
};

} // namespace pex
