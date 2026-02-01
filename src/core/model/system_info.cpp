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

std::vector<CpuTimes> SystemInfo::get_per_cpu_times() {
    std::vector<CpuTimes> result;
    get_per_cpu_times(result);
    return result;
}

void SystemInfo::get_per_cpu_times(std::vector<CpuTimes>& out) {
    std::ifstream stat("/proc/stat");
    std::string line;
    size_t index = 0;

    while (std::getline(stat, line)) {
        // Look for lines starting with "cpu" followed by a digit (cpu0, cpu1, etc.)
        if (line.starts_with("cpu") && line.size() > 3 && std::isdigit(line[3])) {
            CpuTimes times;
            std::istringstream iss(line);
            std::string cpu;
            iss >> cpu >> times.user >> times.nice >> times.system >> times.idle
                >> times.iowait >> times.irq >> times.softirq >> times.steal;

            if (index < out.size()) {
                out[index] = times;
            } else {
                out.push_back(times);
            }
            index++;
        }
    }

    // Shrink if CPU count decreased (unlikely but handle it)
    if (index < out.size()) {
        out.resize(index);
    }
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

SwapInfo SystemInfo::get_swap_info() {
    SwapInfo info;
    std::ifstream meminfo("/proc/meminfo");
    std::string line;

    while (std::getline(meminfo, line)) {
        std::istringstream iss(line);
        std::string key;
        int64_t value;
        std::string unit;

        iss >> key >> value >> unit;

        if (key == "SwapTotal:") {
            info.total = value * 1024;
        } else if (key == "SwapFree:") {
            info.free = value * 1024;
        }
    }

    info.used = info.total - info.free;
    return info;
}

LoadAverage SystemInfo::get_load_average() {
    LoadAverage load;

    if (std::ifstream loadavg("/proc/loadavg"); loadavg) {
        std::string running_total;
        loadavg >> load.one_min >> load.five_min >> load.fifteen_min >> running_total;

        // Parse "running/total" format
        const size_t slash = running_total.find('/');
        if (slash != std::string::npos) {
            load.running_tasks = std::stoi(running_total.substr(0, slash));
            load.total_tasks = std::stoi(running_total.substr(slash + 1));
        }
    }

    return load;
}

UptimeInfo SystemInfo::get_uptime() {
    UptimeInfo info;
    std::ifstream uptime("/proc/uptime");

    if (uptime) {
        double uptime_sec, idle_sec;
        uptime >> uptime_sec >> idle_sec;
        info.uptime_seconds = static_cast<uint64_t>(uptime_sec);
        info.idle_seconds = static_cast<uint64_t>(idle_sec);
    }

    return info;
}

unsigned int SystemInfo::get_processor_count() const {
    return processor_count_;
}

long SystemInfo::get_clock_ticks_per_second() const {
    return clock_ticks_;
}

uint64_t SystemInfo::get_boot_time_ticks() const {
    return boot_time_ticks_;
}

} // namespace pex
