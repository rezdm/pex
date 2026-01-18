#pragma once

#include "process_info.hpp"
#include <vector>
#include <map>
#include <optional>

namespace pex {

class ProcfsReader {
public:
    std::vector<ProcessInfo> get_all_processes();
    std::optional<ProcessInfo> get_process_info(int pid);
    std::vector<ThreadInfo> get_threads(int pid);
    std::vector<FileHandleInfo> get_file_handles(int pid);
    std::vector<NetworkConnectionInfo> get_network_connections(int pid);
    std::vector<MemoryMapInfo> get_memory_maps(int pid);
    std::vector<EnvironmentVariable> get_environment_variables(int pid);

private:
    std::string read_file(const std::string& path);
    std::string read_symlink(const std::string& path);
    std::string get_username(int uid);
    std::map<int, std::string> uid_cache_;
    std::map<int, NetworkConnectionInfo> parse_net_file(const std::string& path, const std::string& protocol);
};

} // namespace pex
