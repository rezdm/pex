#include "linux_process_data_provider.hpp"

namespace pex {

LinuxProcessDataProvider::LinuxProcessDataProvider() = default;

std::vector<ProcessInfo> LinuxProcessDataProvider::get_all_processes(int64_t total_memory) {
    return reader_.get_all_processes(total_memory);
}

std::optional<ProcessInfo> LinuxProcessDataProvider::get_process_info(int pid, int64_t total_memory) {
    return reader_.get_process_info(pid, total_memory);
}

std::vector<ThreadInfo> LinuxProcessDataProvider::get_threads(int pid) {
    return ProcfsReader::get_threads(pid);
}

std::string LinuxProcessDataProvider::get_thread_stack(int pid, int tid) {
    return ProcfsReader::get_thread_stack(pid, tid);
}

std::vector<FileHandleInfo> LinuxProcessDataProvider::get_file_handles(int pid) {
    return ProcfsReader::get_file_handles(pid);
}

std::vector<NetworkConnectionInfo> LinuxProcessDataProvider::get_network_connections(int pid) {
    return ProcfsReader::get_network_connections(pid);
}

std::vector<MemoryMapInfo> LinuxProcessDataProvider::get_memory_maps(int pid) {
    return reader_.get_memory_maps(pid);
}

std::vector<EnvironmentVariable> LinuxProcessDataProvider::get_environment_variables(int pid) {
    return ProcfsReader::get_environment_variables(pid);
}

std::vector<LibraryInfo> LinuxProcessDataProvider::get_libraries(int pid) {
    return ProcfsReader::get_libraries(pid);
}

std::vector<ParseError> LinuxProcessDataProvider::get_recent_errors() {
    return reader_.get_recent_errors();
}

void LinuxProcessDataProvider::clear_errors() {
    reader_.clear_errors();
}

} // namespace pex
