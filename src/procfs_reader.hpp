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

    static std::vector<ThreadInfo> get_threads(int pid);

    static std::vector<FileHandleInfo> get_file_handles(int pid);

    static std::vector<NetworkConnectionInfo> get_network_connections(int pid);

    static std::vector<MemoryMapInfo> get_memory_maps(int pid);

    static std::vector<EnvironmentVariable> get_environment_variables(int pid);

    static std::vector<LibraryInfo> get_libraries(int pid);

private:
    static std::string read_file(const std::string& path);

    static std::string read_symlink(const std::string& path);
    std::string get_username(int uid);
    std::map<int, std::string> uid_cache_;

    static std::map<int, NetworkConnectionInfo> parse_net_file(const std::string& path, const std::string& protocol);
};

} // namespace pex
