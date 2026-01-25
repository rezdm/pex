#include "solaris_process_data_provider.hpp"
#include <stdexcept>

namespace pex {

[[noreturn]] static void throw_not_implemented(const char* func) {
    throw std::runtime_error(std::string("Solaris: ") + func + " not implemented");
}

std::vector<ProcessInfo> SolarisProcessDataProvider::get_all_processes(int64_t /*total_memory*/) {
    throw_not_implemented(__func__);
}

std::optional<ProcessInfo> SolarisProcessDataProvider::get_process_info(int /*pid*/, int64_t /*total_memory*/) {
    throw_not_implemented(__func__);
}

std::vector<ThreadInfo> SolarisProcessDataProvider::get_threads(int /*pid*/) {
    throw_not_implemented(__func__);
}

std::string SolarisProcessDataProvider::get_thread_stack(int /*pid*/, int /*tid*/) {
    throw_not_implemented(__func__);
}

std::vector<FileHandleInfo> SolarisProcessDataProvider::get_file_handles(int /*pid*/) {
    throw_not_implemented(__func__);
}

std::vector<NetworkConnectionInfo> SolarisProcessDataProvider::get_network_connections(int /*pid*/) {
    throw_not_implemented(__func__);
}

std::vector<MemoryMapInfo> SolarisProcessDataProvider::get_memory_maps(int /*pid*/) {
    throw_not_implemented(__func__);
}

std::vector<EnvironmentVariable> SolarisProcessDataProvider::get_environment_variables(int /*pid*/) {
    throw_not_implemented(__func__);
}

std::vector<LibraryInfo> SolarisProcessDataProvider::get_libraries(int /*pid*/) {
    throw_not_implemented(__func__);
}

std::vector<ParseError> SolarisProcessDataProvider::get_recent_errors() {
    return {};
}

void SolarisProcessDataProvider::clear_errors() {
    // Nothing to clear
}

} // namespace pex
