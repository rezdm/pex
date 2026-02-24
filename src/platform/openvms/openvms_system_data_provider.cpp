#include "openvms_system_data_provider.hpp"

#include <cstring>
#include <ctime>

// OpenVMS system headers
#include <starlet.h>
#include <lib$routines.h>
#include <descrip.h>
#include <syidef.h>
#include <jpidef.h>
#include <pscandef.h>
#include <ssdef.h>
#include <stsdef.h>
#include <efndef.h>
#include <iosbdef.h>
#include <iledef.h>
#include <gen64def.h>

// OpenVMS scheduler state constants (schdef.h may not be available)
#ifndef SCH$C_CUR
#define SCH$C_CUR   1   // Currently executing
#define SCH$C_COM   3   // Computable
#define SCH$C_COMO  4   // Computable, outswapped
#endif

namespace pex {

// VMS epoch offset: difference between VMS epoch (Nov 17, 1858) and
// Unix epoch (Jan 1, 1970) in 100-nanosecond intervals.
static constexpr int64_t VMS_TO_UNIX_OFFSET = 35067168000000000LL;

// Pagelet size on Alpha/x86-64
static constexpr int VMS_PAGELET_SIZE = 512;

// VMS CPUTIM is in 10-millisecond ticks, so 100 ticks per second
static constexpr long VMS_TICKS_PER_SECOND = 100;

OpenVMSSystemDataProvider::OpenVMSSystemDataProvider() = default;

CpuTimes OpenVMSSystemDataProvider::get_cpu_times() {
    CpuTimes times;

    // OpenVMS does not provide a simple system-wide CPU time breakdown
    // (user/system/idle/iowait) through a single system service.
    //
    // Approach: Iterate all processes with $PROCESS_SCAN + $GETJPI, summing
    // JPI$_CPUTIM to get total CPU time consumed. The idle time is inferred
    // from the difference between wall-clock elapsed time * CPU count and
    // total consumed CPU time.
    //
    // This is an approximation. A more accurate approach would use the
    // OpenVMS Monitor facility or SDA to read kernel scheduler counters,
    // but those require elevated privileges or are not publicly documented.

    unsigned int scan_ctx = 0;
    struct {
        unsigned short length;
        unsigned short code;
        unsigned int value;
        unsigned int flags;
    } scan_items[] = {
        { 0, 0, 0, 0 }
    };

    unsigned int status = sys$process_scan(&scan_ctx, scan_items);
    if (!$VMS_STATUS_SUCCESS(status)) {
        return times;
    }

    unsigned int proc_cputim = 0;
    ILE3 jpi_items[] = {
        { sizeof(proc_cputim), JPI$_CPUTIM, &proc_cputim, nullptr },
        { 0, 0, nullptr, nullptr }
    };

    IOSB iosb;
    uint64_t total_cputim = 0;

    while (true) {
        proc_cputim = 0;
        status = sys$getjpiw(EFN$C_ENF, &scan_ctx, nullptr,
                             jpi_items, &iosb, nullptr, 0);

        if (iosb.iosb$l_getxxi_status == SS$_NOMOREPROC) {
            break;
        }
        if (!$VMS_STATUS_SUCCESS(status) || !$VMS_STATUS_SUCCESS(iosb.iosb$l_getxxi_status)) {
            continue;
        }

        total_cputim += proc_cputim;
    }

    // Report total consumed CPU time as "user" time (VMS doesn't split per-process).
    // Idle time would require knowing uptime and CPU count for meaningful delta computation.
    // The DataStore computes deltas between snapshots, so absolute values work here.
    times.user = total_cputim;
    times.system = 0;
    times.idle = 0;  // Will be calculated by DataStore from delta vs wall-clock
    times.iowait = 0;

    return times;
}

std::vector<CpuTimes> OpenVMSSystemDataProvider::get_per_cpu_times() {
    std::vector<CpuTimes> result;
    get_per_cpu_times(result);
    return result;
}

void OpenVMSSystemDataProvider::get_per_cpu_times(std::vector<CpuTimes>& out) {
    unsigned int ncpu = get_processor_count();
    out.clear();
    out.resize(ncpu);

    // OpenVMS does not provide per-CPU time breakdowns through standard
    // system services. The aggregate CPU time from get_cpu_times() is
    // distributed evenly across CPUs as an approximation.
    //
    // A more accurate implementation would use:
    // - $GETSYI with SYI$_ACTIVE_CPU_MASK to identify active CPUs
    // - CPU-specific performance counters if available
    // - The Monitor facility's per-CPU data
    //
    // For now, distribute the aggregate evenly.
    CpuTimes aggregate = get_cpu_times();

    if (ncpu == 0) {
        return;
    }

    for (unsigned int i = 0; i < ncpu; ++i) {
        out[i].user = aggregate.user / ncpu;
        out[i].system = 0;
        out[i].idle = 0;
        out[i].iowait = 0;
    }
}

MemoryInfo OpenVMSSystemDataProvider::get_memory_info() {
    MemoryInfo info;

    unsigned int memsize = 0;         // Total physical memory in pagelets
    unsigned int free_pages = 0;      // Free global pages

    ILE3 syi_items[] = {
        { sizeof(memsize),    SYI$_MEMSIZE,         &memsize,    nullptr },
        { sizeof(free_pages), SYI$_FREE_GBLPAGES,   &free_pages, nullptr },
        { 0, 0, nullptr, nullptr }
    };

    IOSB iosb;
    unsigned int status = sys$getsyiw(EFN$C_ENF, nullptr, nullptr,
                                      syi_items, &iosb, nullptr, 0);

    if ($VMS_STATUS_SUCCESS(status) && $VMS_STATUS_SUCCESS(iosb.iosb$l_getxxi_status)) {
        info.total = static_cast<int64_t>(memsize) * VMS_PAGELET_SIZE;
        info.available = static_cast<int64_t>(free_pages) * VMS_PAGELET_SIZE;
        info.used = info.total - info.available;
        if (info.used < 0) {
            info.used = 0;
        }
    }

    return info;
}

SwapInfo OpenVMSSystemDataProvider::get_swap_info() {
    SwapInfo info;

    unsigned int pagefile_total = 0;
    unsigned int pagefile_free = 0;
    unsigned int swapfile_total = 0;
    unsigned int swapfile_free = 0;

    ILE3 syi_items[] = {
        { sizeof(pagefile_total), SYI$_PAGEFILE_PAGE, &pagefile_total, nullptr },
        { sizeof(pagefile_free),  SYI$_PAGEFILE_FREE, &pagefile_free,  nullptr },
        { sizeof(swapfile_total), SYI$_SWAPFILE_PAGE, &swapfile_total, nullptr },
        { sizeof(swapfile_free),  SYI$_SWAPFILE_FREE, &swapfile_free,  nullptr },
        { 0, 0, nullptr, nullptr }
    };

    IOSB iosb;
    unsigned int status = sys$getsyiw(EFN$C_ENF, nullptr, nullptr,
                                      syi_items, &iosb, nullptr, 0);

    if ($VMS_STATUS_SUCCESS(status) && $VMS_STATUS_SUCCESS(iosb.iosb$l_getxxi_status)) {
        // Combine page file and swap file into the swap info.
        // On VMS, the page file is the primary virtual memory backing store
        // (equivalent to Unix swap). The swap file is used for outswapping
        // entire processes (less common on modern systems).
        info.total = static_cast<int64_t>(pagefile_total + swapfile_total) * VMS_PAGELET_SIZE;
        info.free = static_cast<int64_t>(pagefile_free + swapfile_free) * VMS_PAGELET_SIZE;
        info.used = info.total - info.free;
        if (info.used < 0) {
            info.used = 0;
        }
    }

    return info;
}

LoadAverage OpenVMSSystemDataProvider::get_load_average() {
    LoadAverage la;

    // OpenVMS does not have a Unix-style load average concept.
    // Instead, we count processes by state to populate running_tasks and total_tasks.

    unsigned int scan_ctx = 0;
    struct {
        unsigned short length;
        unsigned short code;
        unsigned int value;
        unsigned int flags;
    } scan_items[] = {
        { 0, 0, 0, 0 }
    };

    unsigned int status = sys$process_scan(&scan_ctx, scan_items);
    if (!$VMS_STATUS_SUCCESS(status)) {
        return la;
    }

    unsigned int proc_state = 0;
    ILE3 jpi_items[] = {
        { sizeof(proc_state), JPI$_STATE, &proc_state, nullptr },
        { 0, 0, nullptr, nullptr }
    };

    IOSB iosb;
    int total = 0;
    int running = 0;

    while (true) {
        proc_state = 0;
        status = sys$getjpiw(EFN$C_ENF, &scan_ctx, nullptr,
                             jpi_items, &iosb, nullptr, 0);

        if (iosb.iosb$l_getxxi_status == SS$_NOMOREPROC) {
            break;
        }
        if (!$VMS_STATUS_SUCCESS(status) || !$VMS_STATUS_SUCCESS(iosb.iosb$l_getxxi_status)) {
            continue;
        }

        total++;
        if (proc_state == SCH$C_CUR || proc_state == SCH$C_COM ||
            proc_state == SCH$C_COMO) {
            running++;
        }
    }

    la.total_tasks = total;
    la.running_tasks = running;

    // Approximate load averages from running task count.
    // Since VMS has no real load average, use the current running count
    // for all three intervals.
    la.one_min = static_cast<double>(running);
    la.five_min = static_cast<double>(running);
    la.fifteen_min = static_cast<double>(running);

    return la;
}

UptimeInfo OpenVMSSystemDataProvider::get_uptime() {
    UptimeInfo info;

    int64_t boottime = 0;

    ILE3 syi_items[] = {
        { sizeof(boottime), SYI$_BOOTTIME, &boottime, nullptr },
        { 0, 0, nullptr, nullptr }
    };

    IOSB iosb;
    unsigned int status = sys$getsyiw(EFN$C_ENF, nullptr, nullptr,
                                      syi_items, &iosb, nullptr, 0);

    if ($VMS_STATUS_SUCCESS(status) && $VMS_STATUS_SUCCESS(iosb.iosb$l_getxxi_status)) {
        // Get current VMS time (sys$gettim expects _generic_64* with __NEW_STARLET)
        _generic_64 now_vms_gq;
        now_vms_gq.gen64$q_quadword = 0;
        sys$gettim(&now_vms_gq);
        int64_t now_vms = now_vms_gq.gen64$q_quadword;

        // Difference in 100ns intervals, convert to seconds
        int64_t delta_100ns = now_vms - boottime;
        info.uptime_seconds = static_cast<uint64_t>(delta_100ns / 10000000LL);
    }

    info.idle_seconds = 0;  // Not available on VMS

    return info;
}

unsigned int OpenVMSSystemDataProvider::get_processor_count() const {
    if (cached_cpu_count_ > 0) {
        return cached_cpu_count_;
    }

    unsigned int cpu_count = 0;

    ILE3 syi_items[] = {
        { sizeof(cpu_count), SYI$_AVAILCPU_CNT, &cpu_count, nullptr },
        { 0, 0, nullptr, nullptr }
    };

    IOSB iosb;
    unsigned int status = sys$getsyiw(EFN$C_ENF, nullptr, nullptr,
                                      syi_items, &iosb, nullptr, 0);

    if ($VMS_STATUS_SUCCESS(status) && $VMS_STATUS_SUCCESS(iosb.iosb$l_getxxi_status) &&
        cpu_count > 0) {
        cached_cpu_count_ = cpu_count;
    } else {
        cached_cpu_count_ = 1;
    }

    return cached_cpu_count_;
}

long OpenVMSSystemDataProvider::get_clock_ticks_per_second() const {
    // JPI$_CPUTIM returns CPU time in 10-millisecond ticks = 100 ticks/second
    return VMS_TICKS_PER_SECOND;
}

uint64_t OpenVMSSystemDataProvider::get_boot_time_ticks() const {
    int64_t boottime = 0;

    ILE3 syi_items[] = {
        { sizeof(boottime), SYI$_BOOTTIME, &boottime, nullptr },
        { 0, 0, nullptr, nullptr }
    };

    IOSB iosb;
    unsigned int status = sys$getsyiw(EFN$C_ENF, nullptr, nullptr,
                                      syi_items, &iosb, nullptr, 0);

    if ($VMS_STATUS_SUCCESS(status) && $VMS_STATUS_SUCCESS(iosb.iosb$l_getxxi_status)) {
        // Convert VMS time to ticks (VMS time is 100ns intervals, ticks are 10ms)
        int64_t unix_100ns = boottime - VMS_TO_UNIX_OFFSET;
        return static_cast<uint64_t>(unix_100ns / 100000);  // 100ns -> 10ms ticks
    }

    return 0;
}

std::string OpenVMSSystemDataProvider::get_system_info_string() const {
    if (!cached_system_string_.empty()) {
        return cached_system_string_;
    }

    char version[9] = {};
    unsigned short version_len = 0;
    char hw_name[64] = {};
    unsigned short hw_name_len = 0;
    char arch_name[16] = {};
    unsigned short arch_name_len = 0;

    ILE3 syi_items[] = {
        { sizeof(version),   SYI$_VERSION,   version,   &version_len },
        { sizeof(hw_name),   SYI$_HW_NAME,   hw_name,   &hw_name_len },
        { sizeof(arch_name), SYI$_ARCH_NAME, arch_name, &arch_name_len },
        { 0, 0, nullptr, nullptr }
    };

    IOSB iosb;
    unsigned int status = sys$getsyiw(EFN$C_ENF, nullptr, nullptr,
                                      syi_items, &iosb, nullptr, 0);

    std::string result = "OpenVMS";

    if ($VMS_STATUS_SUCCESS(status) && $VMS_STATUS_SUCCESS(iosb.iosb$l_getxxi_status)) {
        // Trim trailing blanks from VMS strings
        auto trim = [](const char* s, int len) -> std::string {
            while (len > 0 && s[len - 1] == ' ') --len;
            return std::string(s, len);
        };

        std::string ver = trim(version, version_len);
        std::string hw = trim(hw_name, hw_name_len);
        std::string arch = trim(arch_name, arch_name_len);

        result = "OpenVMS";
        if (!ver.empty()) {
            result += " " + ver;
        }
        if (!arch.empty()) {
            result += " " + arch;
        }
        if (!hw.empty()) {
            result += " (" + hw + ")";
        }
    }

    cached_system_string_ = result;
    return cached_system_string_;
}

} // namespace pex
