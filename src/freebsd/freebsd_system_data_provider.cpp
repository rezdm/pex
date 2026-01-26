#include "freebsd_system_data_provider.hpp"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/vmmeter.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <vm/vm_param.h>
#include <unistd.h>
#include <cstring>
#include <ctime>

#if __has_include(<sys/swap.h>)
#include <sys/swap.h>
#define PEX_HAVE_SWAPCTL 1
#else
#define PEX_HAVE_SWAPCTL 0
#endif

namespace pex {

CpuTimes FreeBSDSystemDataProvider::get_cpu_times() {
    CpuTimes times;

    // Get aggregate CPU times via kern.cp_time
    long cp_time[CPUSTATES];
    size_t len = sizeof(cp_time);

    if (sysctlbyname("kern.cp_time", cp_time, &len, nullptr, 0) == 0) {
        times.user = cp_time[CP_USER];
        times.nice = cp_time[CP_NICE];
        times.system = cp_time[CP_SYS];
        times.idle = cp_time[CP_IDLE];
        times.irq = cp_time[CP_INTR];
        // FreeBSD doesn't have iowait, softirq, steal in cp_time
        times.iowait = 0;
        times.softirq = 0;
        times.steal = 0;
    }

    return times;
}

std::vector<CpuTimes> FreeBSDSystemDataProvider::get_per_cpu_times() {
    std::vector<CpuTimes> result;
    get_per_cpu_times(result);
    return result;
}

void FreeBSDSystemDataProvider::get_per_cpu_times(std::vector<CpuTimes>& out) {
    int ncpu = get_processor_count();
    out.resize(ncpu);

    // Get per-CPU times via kern.cp_times
    size_t len = sizeof(long) * CPUSTATES * ncpu;
    std::vector<long> cp_times(CPUSTATES * ncpu);

    if (sysctlbyname("kern.cp_times", cp_times.data(), &len, nullptr, 0) == 0) {
        for (int i = 0; i < ncpu; ++i) {
            long* cpu = &cp_times[i * CPUSTATES];
            out[i].user = cpu[CP_USER];
            out[i].nice = cpu[CP_NICE];
            out[i].system = cpu[CP_SYS];
            out[i].idle = cpu[CP_IDLE];
            out[i].irq = cpu[CP_INTR];
            out[i].iowait = 0;
            out[i].softirq = 0;
            out[i].steal = 0;
        }
    }
}

MemoryInfo FreeBSDSystemDataProvider::get_memory_info() {
    MemoryInfo info;

    // Total physical memory
    size_t len = sizeof(info.total);
    sysctlbyname("hw.physmem", &info.total, &len, nullptr, 0);

    // vm.stats.* sysctls provide page counts directly

    unsigned int page_size = getpagesize();

    // Free pages
    unsigned int v_free_count = 0;
    len = sizeof(v_free_count);
    sysctlbyname("vm.stats.vm.v_free_count", &v_free_count, &len, nullptr, 0);

    // Inactive pages (considered available)
    unsigned int v_inactive_count = 0;
    len = sizeof(v_inactive_count);
    sysctlbyname("vm.stats.vm.v_inactive_count", &v_inactive_count, &len, nullptr, 0);

    // Cache pages (also available)
    unsigned int v_cache_count = 0;
    len = sizeof(v_cache_count);
    sysctlbyname("vm.stats.vm.v_cache_count", &v_cache_count, &len, nullptr, 0);

    // Available = free + inactive + cache
    info.available = static_cast<int64_t>(v_free_count + v_inactive_count + v_cache_count) * page_size;
    info.used = info.total - info.available;

    return info;
}

SwapInfo FreeBSDSystemDataProvider::get_swap_info() {
    SwapInfo info;

    // Best-effort: try swapctl if available
#if PEX_HAVE_SWAPCTL && defined(SWAP_NSWAP)
    int nswap = swapctl(SWAP_NSWAP, nullptr, 0);
    if (nswap > 0) {
        std::vector<struct swapent> ents(static_cast<size_t>(nswap));
        if (swapctl(SWAP_STATS, ents.data(), nswap) > 0) {
            for (const auto& ent : ents) {
                info.total += static_cast<int64_t>(ent.se_nblks) * getpagesize();
                info.used += static_cast<int64_t>(ent.se_inuse) * getpagesize();
            }
            info.free = info.total - info.used;
            return info;
        }
    }
#endif

    // Fallback: expose swap as zero if we can't query it on this system
    info.total = 0;
    info.used = 0;
    info.free = 0;
    return info;
}

LoadAverage FreeBSDSystemDataProvider::get_load_average() {
    LoadAverage la;

    double loadavg[3];
    if (getloadavg(loadavg, 3) == 3) {
        la.one_min = loadavg[0];
        la.five_min = loadavg[1];
        la.fifteen_min = loadavg[2];
    }

    // Get process counts
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
    size_t len = 0;
    if (sysctl(mib, 3, nullptr, &len, nullptr, 0) == 0) {
        la.total_tasks = len / sizeof(struct kinfo_proc);
    }

    // Running processes
    mib[2] = KERN_PROC_PROC;  // Only actual processes, not threads
    if (sysctl(mib, 3, nullptr, &len, nullptr, 0) == 0) {
        // Count running processes by iterating
        std::vector<char> buf(len * 5 / 4);
        size_t actual_len = buf.size();
        if (sysctl(mib, 3, buf.data(), &actual_len, nullptr, 0) == 0) {
            struct kinfo_proc* kp = reinterpret_cast<struct kinfo_proc*>(buf.data());
            size_t count = actual_len / sizeof(struct kinfo_proc);
            int running = 0;
            for (size_t i = 0; i < count; ++i) {
                if (kp[i].ki_stat == SRUN) {
                    running++;
                }
            }
            la.running_tasks = running;
        }
    }

    return la;
}

UptimeInfo FreeBSDSystemDataProvider::get_uptime() {
    UptimeInfo info;

    // Get boot time
    struct timeval boottime;
    size_t len = sizeof(boottime);
    if (sysctlbyname("kern.boottime", &boottime, &len, nullptr, 0) == 0) {
        time_t now = time(nullptr);
        info.uptime_seconds = now - boottime.tv_sec;
    }

    // FreeBSD doesn't have a direct idle time metric like Linux
    // We can estimate it from CPU idle percentage, but for simplicity return 0
    info.idle_seconds = 0;

    return info;
}

unsigned int FreeBSDSystemDataProvider::get_processor_count() const {
    int ncpu = 0;
    size_t len = sizeof(ncpu);
    if (sysctlbyname("hw.ncpu", &ncpu, &len, nullptr, 0) != 0 || ncpu <= 0) {
        const long fallback = sysconf(_SC_NPROCESSORS_ONLN);
        ncpu = (fallback > 0) ? static_cast<int>(fallback) : 1;
    }
    return static_cast<unsigned int>(ncpu);
}

long FreeBSDSystemDataProvider::get_clock_ticks_per_second() const {
    return sysconf(_SC_CLK_TCK);
}

uint64_t FreeBSDSystemDataProvider::get_boot_time_ticks() const {
    struct timeval boottime;
    size_t len = sizeof(boottime);
    if (sysctlbyname("kern.boottime", &boottime, &len, nullptr, 0) == 0) {
        return static_cast<uint64_t>(boottime.tv_sec) * sysconf(_SC_CLK_TCK);
    }
    return 0;
}

} // namespace pex
