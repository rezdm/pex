#include "system_info.hpp"
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <thread>

namespace pex {

SystemInfo& SystemInfo::instance() {
    static SystemInfo instance;
    return instance;
}

SystemInfo::SystemInfo() {
    processor_count_ = std::thread::hardware_concurrency();
    if (processor_count_ == 0) processor_count_ = 1;

    clock_ticks_ = sysconf(_SC_CLK_TCK);
    if (clock_ticks_ <= 0) clock_ticks_ = 100;

    // Read boot time from /proc/stat
    std::ifstream stat("/proc/stat");
    std::string line;
    while (std::getline(stat, line)) {
        if (line.starts_with("btime ")) {
            std::istringstream iss(line);
            std::string key;
            iss >> key >> boot_time_ticks_;
            break;
        }
    }
}

CpuTimes SystemInfo::get_cpu_times() {
    CpuTimes times;
    std::ifstream stat("/proc/stat");
    std::string line;

    if (std::getline(stat, line) && line.starts_with("cpu ")) {
        std::istringstream iss(line);
        std::string cpu;
        iss >> cpu >> times.user >> times.nice >> times.system >> times.idle
            >> times.iowait >> times.irq >> times.softirq >> times.steal;
    }

    return times;
}

MemoryInfo SystemInfo::get_memory_info() {
    MemoryInfo info;
    std::ifstream meminfo("/proc/meminfo");
    std::string line;

    while (std::getline(meminfo, line)) {
        std::istringstream iss(line);
        std::string key;
        int64_t value;
        std::string unit;

        iss >> key >> value >> unit;

        if (key == "MemTotal:") {
            info.total = value * 1024; // Convert from KB to bytes
        } else if (key == "MemAvailable:") {
            info.available = value * 1024;
        }
    }

    info.used = info.total - info.available;
    return info;
}

int SystemInfo::get_processor_count() const {
    return processor_count_;
}

long SystemInfo::get_clock_ticks_per_second() const {
    return clock_ticks_;
}

uint64_t SystemInfo::get_boot_time_ticks() const {
    return boot_time_ticks_;
}

} // namespace pex
