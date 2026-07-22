#include "macos_system_data_provider.hpp"

#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/vm_statistics.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace pex {

namespace {

template <typename T>
bool sysctl_by_name(const char* name, T& out) {
    size_t len = sizeof(out);
    return sysctlbyname(name, &out, &len, nullptr, 0) == 0;
}

} // namespace

MacosSystemDataProvider::MacosSystemDataProvider() {
    int ncpu = 0;
    if (sysctl_by_name("hw.logicalcpu", ncpu) && ncpu > 0) {
        processor_count_ = static_cast<unsigned int>(ncpu);
    } else if (const long n = sysconf(_SC_NPROCESSORS_ONLN); n > 0) {
        processor_count_ = static_cast<unsigned int>(n);
    }
    if (const long t = sysconf(_SC_CLK_TCK); t > 0) clock_ticks_ = t;
}

CpuTimes MacosSystemDataProvider::get_cpu_times() {
    CpuTimes times;
    host_cpu_load_info_data_t info{};
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                        reinterpret_cast<host_info_t>(&info), &count) == KERN_SUCCESS) {
        times.user = info.cpu_ticks[CPU_STATE_USER];
        times.nice = info.cpu_ticks[CPU_STATE_NICE];
        times.system = info.cpu_ticks[CPU_STATE_SYSTEM];
        times.idle = info.cpu_ticks[CPU_STATE_IDLE];
        // macOS has no iowait/irq/softirq/steal split.
    }
    return times;
}

std::vector<CpuTimes> MacosSystemDataProvider::get_per_cpu_times() {
    std::vector<CpuTimes> result;
    get_per_cpu_times(result);
    return result;
}

void MacosSystemDataProvider::get_per_cpu_times(std::vector<CpuTimes>& out) {
    natural_t cpu_count = 0;
    processor_info_array_t info_array = nullptr;
    mach_msg_type_number_t info_count = 0;
    if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &cpu_count,
                            &info_array, &info_count) != KERN_SUCCESS) {
        return;
    }
    out.assign(cpu_count, CpuTimes{});
    const auto* loads = reinterpret_cast<processor_cpu_load_info_t>(info_array);
    for (natural_t i = 0; i < cpu_count; ++i) {
        out[i].user = loads[i].cpu_ticks[CPU_STATE_USER];
        out[i].nice = loads[i].cpu_ticks[CPU_STATE_NICE];
        out[i].system = loads[i].cpu_ticks[CPU_STATE_SYSTEM];
        out[i].idle = loads[i].cpu_ticks[CPU_STATE_IDLE];
    }
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(info_array),
                  info_count * sizeof(int));
}

MemoryInfo MacosSystemDataProvider::get_memory_info() {
    MemoryInfo info;
    uint64_t total = 0;
    if (sysctl_by_name("hw.memsize", total)) {
        info.total = static_cast<int64_t>(total);
    }

    vm_statistics64_data_t vm{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vm), &count) == KERN_SUCCESS) {
        const int64_t page = sysconf(_SC_PAGESIZE);
        // Free + inactive is the memory the system can reclaim cheaply.
        info.available = (static_cast<int64_t>(vm.free_count) +
                          static_cast<int64_t>(vm.inactive_count)) * page;
    }
    info.used = info.total - info.available;
    return info;
}

SwapInfo MacosSystemDataProvider::get_swap_info() {
    SwapInfo info;
    struct xsw_usage sw{};
    size_t len = sizeof(sw);
    if (sysctlbyname("vm.swapusage", &sw, &len, nullptr, 0) == 0) {
        info.total = static_cast<int64_t>(sw.xsu_total);
        info.used = static_cast<int64_t>(sw.xsu_used);
        info.free = static_cast<int64_t>(sw.xsu_avail);
    }
    return info;
}

LoadAverage MacosSystemDataProvider::get_load_average() {
    LoadAverage la;
    double loads[3];
    if (getloadavg(loads, 3) == 3) {
        la.one_min = loads[0];
        la.five_min = loads[1];
        la.fifteen_min = loads[2];
    }
    // total/running task counts are populated from the snapshot by the UI.
    return la;
}

UptimeInfo MacosSystemDataProvider::get_uptime() {
    UptimeInfo info;
    struct timeval boot{};
    size_t len = sizeof(boot);
    int mib[2] = {CTL_KERN, KERN_BOOTTIME};
    if (sysctl(mib, 2, &boot, &len, nullptr, 0) == 0 && boot.tv_sec != 0) {
        info.uptime_seconds = static_cast<uint64_t>(::time(nullptr) - boot.tv_sec);
    }
    return info;
}

unsigned int MacosSystemDataProvider::get_processor_count() const {
    return processor_count_;
}

long MacosSystemDataProvider::get_clock_ticks_per_second() const {
    return clock_ticks_;
}

uint64_t MacosSystemDataProvider::get_boot_time_seconds() const {
    struct timeval boot{};
    size_t len = sizeof(boot);
    int mib[2] = {CTL_KERN, KERN_BOOTTIME};
    if (sysctl(mib, 2, &boot, &len, nullptr, 0) == 0) {
        return static_cast<uint64_t>(boot.tv_sec);
    }
    return 0;
}

std::string MacosSystemDataProvider::get_system_info_string() const {
    struct utsname uts{};
    std::string result = "macOS";
    if (uname(&uts) == 0) {
        result = std::string(uts.sysname) + " " + uts.release + " " + uts.machine;
    }
    char ver[64];
    size_t vlen = sizeof(ver);
    if (sysctlbyname("kern.osproductversion", ver, &vlen, nullptr, 0) == 0 && vlen > 1) {
        result += " (macOS " + std::string(ver) + ")";
    }
    return result;
}

} // namespace pex
