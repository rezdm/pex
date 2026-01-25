#include "solaris_system_data_provider.hpp"
#include <stdexcept>

namespace pex {

[[noreturn]] static void throw_not_implemented(const char* func) {
    throw std::runtime_error(std::string("Solaris: ") + func + " not implemented");
}

CpuTimes SolarisSystemDataProvider::get_cpu_times() {
    throw_not_implemented(__func__);
}

std::vector<CpuTimes> SolarisSystemDataProvider::get_per_cpu_times() {
    throw_not_implemented(__func__);
}

void SolarisSystemDataProvider::get_per_cpu_times(std::vector<CpuTimes>& /*out*/) {
    throw_not_implemented(__func__);
}

MemoryInfo SolarisSystemDataProvider::get_memory_info() {
    throw_not_implemented(__func__);
}

SwapInfo SolarisSystemDataProvider::get_swap_info() {
    throw_not_implemented(__func__);
}

LoadAverage SolarisSystemDataProvider::get_load_average() {
    throw_not_implemented(__func__);
}

UptimeInfo SolarisSystemDataProvider::get_uptime() {
    throw_not_implemented(__func__);
}

unsigned int SolarisSystemDataProvider::get_processor_count() const {
    throw_not_implemented(__func__);
}

long SolarisSystemDataProvider::get_clock_ticks_per_second() const {
    throw_not_implemented(__func__);
}

uint64_t SolarisSystemDataProvider::get_boot_time_ticks() const {
    throw_not_implemented(__func__);
}

} // namespace pex
