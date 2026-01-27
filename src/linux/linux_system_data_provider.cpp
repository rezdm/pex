#include "linux_system_data_provider.hpp"
#include "../system_info.hpp"

#include <sys/utsname.h>
#include <fstream>
#include <sstream>

namespace pex {

// Helper to parse /etc/os-release for distribution name
static std::string get_distro_name() {
    std::ifstream file("/etc/os-release");
    if (!file) {
        // Try fallback
        file.open("/usr/lib/os-release");
    }
    if (!file) {
        return "";
    }

    std::string line;
    std::string pretty_name;
    while (std::getline(file, line)) {
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            pretty_name = line.substr(12);
            // Remove quotes if present
            if (pretty_name.size() >= 2 && pretty_name.front() == '"' && pretty_name.back() == '"') {
                pretty_name = pretty_name.substr(1, pretty_name.size() - 2);
            }
            break;
        }
    }
    return pretty_name;
}

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

std::string LinuxSystemDataProvider::get_system_info_string() const {
    struct utsname uts;
    if (uname(&uts) != 0) {
        return "Linux";
    }

    // Try to get distribution name
    std::string distro = get_distro_name();

    // Format: "Linux <kernel> <arch> [<distro>]"
    // Example: "Linux 6.1.0-18-amd64 x86_64 (Debian GNU/Linux 12)"
    std::string result = std::string(uts.sysname) + " " + uts.release + " " + uts.machine;
    if (!distro.empty()) {
        result += " (" + distro + ")";
    }
    return result;
}

} // namespace pex
