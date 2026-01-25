#include "freebsd_process_data_provider.hpp"
#include <stdexcept>

namespace pex {

[[noreturn]] static void throw_not_implemented(const char* func) {
    throw std::runtime_error(std::string("FreeBSD: ") + func + " not implemented");
}

std::vector<ProcessInfo> FreeBSDProcessDataProvider::get_all_processes(int64_t /*total_memory*/) {
    throw_not_implemented(__func__);
}

std::optional<ProcessInfo> FreeBSDProcessDataProvider::get_process_info(int /*pid*/, int64_t /*total_memory*/) {
    throw_not_implemented(__func__);
}

std::vector<ThreadInfo> FreeBSDProcessDataProvider::get_threads(int /*pid*/) {
    throw_not_implemented(__func__);
}

std::string FreeBSDProcessDataProvider::get_thread_stack(int /*pid*/, int /*tid*/) {
    throw_not_implemented(__func__);
}

std::vector<FileHandleInfo> FreeBSDProcessDataProvider::get_file_handles(int /*pid*/) {
    throw_not_implemented(__func__);
}

std::vector<NetworkConnectionInfo> FreeBSDProcessDataProvider::get_network_connections(int /*pid*/) {
    throw_not_implemented(__func__);
}

std::vector<MemoryMapInfo> FreeBSDProcessDataProvider::get_memory_maps(int /*pid*/) {
    throw_not_implemented(__func__);
}

std::vector<EnvironmentVariable> FreeBSDProcessDataProvider::get_environment_variables(int /*pid*/) {
    throw_not_implemented(__func__);
}

std::vector<LibraryInfo> FreeBSDProcessDataProvider::get_libraries(int /*pid*/) {
    throw_not_implemented(__func__);
}

std::vector<ParseError> FreeBSDProcessDataProvider::get_recent_errors() {
    return {};
}

void FreeBSDProcessDataProvider::clear_errors() {
    // Nothing to clear
}

} // namespace pex
