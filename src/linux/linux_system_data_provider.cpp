#include "linux_system_data_provider.hpp"
#include "../system_info.hpp"

namespace pex {

LinuxSystemDataProvider::LinuxSystemDataProvider() {
    auto& sys_info = SystemInfo::instance();
    processor_count_ = sys_info.get_processor_count();
    clock_ticks_per_second_ = sys_info.get_clock_ticks_per_second();
    boot_time_ticks_ = sys_info.get_boot_time_ticks();
}

CpuTimes LinuxSystemDataProvider::get_cpu_times() {
    return SystemInfo::get_cpu_times();
}

std::vector<CpuTimes> LinuxSystemDataProvider::get_per_cpu_times() {
    return SystemInfo::get_per_cpu_times();
}

void LinuxSystemDataProvider::get_per_cpu_times(std::vector<CpuTimes>& out) {
    SystemInfo::get_per_cpu_times(out);
}

MemoryInfo LinuxSystemDataProvider::get_memory_info() {
    return SystemInfo::get_memory_info();
}

SwapInfo LinuxSystemDataProvider::get_swap_info() {
    return SystemInfo::get_swap_info();
}

LoadAverage LinuxSystemDataProvider::get_load_average() {
    return SystemInfo::get_load_average();
}

UptimeInfo LinuxSystemDataProvider::get_uptime() {
    return SystemInfo::get_uptime();
}

unsigned int LinuxSystemDataProvider::get_processor_count() const {
    return processor_count_;
}

long LinuxSystemDataProvider::get_clock_ticks_per_second() const {
    return clock_ticks_per_second_;
}

uint64_t LinuxSystemDataProvider::get_boot_time_ticks() const {
    return boot_time_ticks_;
}

} // namespace pex
