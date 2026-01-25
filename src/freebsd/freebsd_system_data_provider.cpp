#include "freebsd_system_data_provider.hpp"
#include <stdexcept>

namespace pex {

[[noreturn]] static void throw_not_implemented(const char* func) {
    throw std::runtime_error(std::string("FreeBSD: ") + func + " not implemented");
}

CpuTimes FreeBSDSystemDataProvider::get_cpu_times() {
    throw_not_implemented(__func__);
}

std::vector<CpuTimes> FreeBSDSystemDataProvider::get_per_cpu_times() {
    throw_not_implemented(__func__);
}

void FreeBSDSystemDataProvider::get_per_cpu_times(std::vector<CpuTimes>& /*out*/) {
    throw_not_implemented(__func__);
}

MemoryInfo FreeBSDSystemDataProvider::get_memory_info() {
    throw_not_implemented(__func__);
}

SwapInfo FreeBSDSystemDataProvider::get_swap_info() {
    throw_not_implemented(__func__);
}

LoadAverage FreeBSDSystemDataProvider::get_load_average() {
    throw_not_implemented(__func__);
}

UptimeInfo FreeBSDSystemDataProvider::get_uptime() {
    throw_not_implemented(__func__);
}

unsigned int FreeBSDSystemDataProvider::get_processor_count() const {
    throw_not_implemented(__func__);
}

long FreeBSDSystemDataProvider::get_clock_ticks_per_second() const {
    throw_not_implemented(__func__);
}

uint64_t FreeBSDSystemDataProvider::get_boot_time_ticks() const {
    throw_not_implemented(__func__);
}

} // namespace pex
