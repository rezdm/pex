#include "stub_providers.hpp"

namespace pex {

// StubProcessDataProvider - returns empty/minimal data

std::vector<ProcessInfo> StubProcessDataProvider::get_all_processes(int64_t /*total_memory*/) {
    // Return a single dummy process for testing
    std::vector<ProcessInfo> result;
    ProcessInfo info;
    info.pid = 1;
    info.parent_pid = 0;
    info.name = "stub_init";
    info.command_line = "/sbin/stub_init";
    info.executable_path = "/sbin/stub_init";
    info.state_char = 'S';
    info.user_name = "root";
    info.cpu_percent = 0.0;
    info.total_cpu_percent = 0.0;
    info.resident_memory = 1024 * 1024;
    info.virtual_memory = 4 * 1024 * 1024;
    info.memory_percent = 0.1;
    info.thread_count = 1;
    info.priority = 20;
    info.start_time = std::chrono::system_clock::now();
    info.user_time = 0;
    info.kernel_time = 0;
    result.push_back(info);
    return result;
}

std::optional<ProcessInfo> StubProcessDataProvider::get_process_info(int pid, int64_t total_memory) {
    auto processes = get_all_processes(total_memory);
    for (const auto& p : processes) {
        if (p.pid == pid) return p;
    }
    return std::nullopt;
}

std::vector<ThreadInfo> StubProcessDataProvider::get_threads(int /*pid*/) {
    return {};
}

std::string StubProcessDataProvider::get_thread_stack(int /*pid*/, int /*tid*/) {
    return "(Stack trace not available on this platform)";
}

std::vector<FileHandleInfo> StubProcessDataProvider::get_file_handles(int /*pid*/) {
    return {};
}

std::vector<NetworkConnectionInfo> StubProcessDataProvider::get_network_connections(int /*pid*/) {
    return {};
}

std::vector<MemoryMapInfo> StubProcessDataProvider::get_memory_maps(int /*pid*/) {
    return {};
}

std::vector<EnvironmentVariable> StubProcessDataProvider::get_environment_variables(int /*pid*/) {
    return {};
}

std::vector<LibraryInfo> StubProcessDataProvider::get_libraries(int /*pid*/) {
    return {};
}

std::vector<ParseError> StubProcessDataProvider::get_recent_errors() {
    return {};
}

void StubProcessDataProvider::clear_errors() {
    // Nothing to clear
}

// StubSystemDataProvider - returns minimal system data

CpuTimes StubSystemDataProvider::get_cpu_times() {
    return CpuTimes{};
}

std::vector<CpuTimes> StubSystemDataProvider::get_per_cpu_times() {
    // Return one CPU with zero times
    return std::vector<CpuTimes>(1);
}

void StubSystemDataProvider::get_per_cpu_times(std::vector<CpuTimes>& out) {
    out.resize(1);
    out[0] = CpuTimes{};
}

MemoryInfo StubSystemDataProvider::get_memory_info() {
    MemoryInfo info;
    info.total = 8ULL * 1024 * 1024 * 1024;  // 8 GB
    info.used = 1ULL * 1024 * 1024 * 1024;   // 1 GB
    info.available = info.total - info.used;
    return info;
}

SwapInfo StubSystemDataProvider::get_swap_info() {
    SwapInfo info;
    info.total = 2ULL * 1024 * 1024 * 1024;
    info.used = 0;
    info.free = info.total;
    return info;
}

LoadAverage StubSystemDataProvider::get_load_average() {
    return LoadAverage{0.0, 0.0, 0.0};
}

UptimeInfo StubSystemDataProvider::get_uptime() {
    return UptimeInfo{0, 0};
}

unsigned int StubSystemDataProvider::get_processor_count() const {
    return 1;
}

long StubSystemDataProvider::get_clock_ticks_per_second() const {
    return 100;  // Common default
}

uint64_t StubSystemDataProvider::get_boot_time_ticks() const {
    return 0;
}

// StubProcessKiller - always returns success (no-op)

KillResult StubProcessKiller::kill_process(int /*pid*/, bool /*force*/) {
    KillResult result;
    result.success = true;
    result.process_still_running = false;
    result.error_message = "";
    return result;
}

KillResult StubProcessKiller::kill_process_tree(int /*pid*/, bool /*force*/) {
    KillResult result;
    result.success = true;
    result.process_still_running = false;
    result.error_message = "";
    return result;
}

} // namespace pex
