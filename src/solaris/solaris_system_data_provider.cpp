#include "solaris_system_data_provider.hpp"

#include <sys/types.h>
#include <sys/sysinfo.h>
#include <sys/loadavg.h>
#include <sys/swap.h>
#include <procfs.h>
#include <kstat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sys/utsname.h>
#include <cstdio>
#include <ctime>

namespace pex {

CpuTimes SolarisSystemDataProvider::get_cpu_times() {
    CpuTimes times;

    kstat_ctl_t* kc = kstat_open();
    if (!kc) return times;

    // Aggregate CPU times from all cpu_stat instances
    for (kstat_t* ksp = kc->kc_chain; ksp != nullptr; ksp = ksp->ks_next) {
        if (strcmp(ksp->ks_module, "cpu_stat") != 0) continue;

        if (kstat_read(kc, ksp, nullptr) < 0) continue;

        cpu_stat_t* cs = reinterpret_cast<cpu_stat_t*>(ksp->ks_data);
        if (!cs) continue;

        times.user += cs->cpu_sysinfo.cpu[CPU_USER];
        times.system += cs->cpu_sysinfo.cpu[CPU_KERNEL];
        times.idle += cs->cpu_sysinfo.cpu[CPU_IDLE];
        times.iowait += cs->cpu_sysinfo.cpu[CPU_WAIT];
        // Solaris doesn't have nice, irq, softirq, steal in cpu_stat
    }

    kstat_close(kc);
    return times;
}

std::vector<CpuTimes> SolarisSystemDataProvider::get_per_cpu_times() {
    std::vector<CpuTimes> result;
    get_per_cpu_times(result);
    return result;
}

void SolarisSystemDataProvider::get_per_cpu_times(std::vector<CpuTimes>& out) {
    int ncpu = get_processor_count();
    out.clear();
    out.resize(ncpu);

    kstat_ctl_t* kc = kstat_open();
    if (!kc) return;

    for (kstat_t* ksp = kc->kc_chain; ksp != nullptr; ksp = ksp->ks_next) {
        if (strcmp(ksp->ks_module, "cpu_stat") != 0) continue;

        int cpu_id = ksp->ks_instance;
        if (cpu_id < 0 || cpu_id >= ncpu) continue;

        if (kstat_read(kc, ksp, nullptr) < 0) continue;

        cpu_stat_t* cs = reinterpret_cast<cpu_stat_t*>(ksp->ks_data);
        if (!cs) continue;

        out[cpu_id].user = cs->cpu_sysinfo.cpu[CPU_USER];
        out[cpu_id].system = cs->cpu_sysinfo.cpu[CPU_KERNEL];
        out[cpu_id].idle = cs->cpu_sysinfo.cpu[CPU_IDLE];
        out[cpu_id].iowait = cs->cpu_sysinfo.cpu[CPU_WAIT];
    }

    kstat_close(kc);
}

MemoryInfo SolarisSystemDataProvider::get_memory_info() {
    MemoryInfo info;

    // Total physical memory
    long pages = sysconf(_SC_PHYS_PAGES);
    long pagesize = sysconf(_SC_PAGESIZE);
    info.total = pages * pagesize;

    // Available memory - use kstat for freemem
    // Note: kstat API uses char* not const char*, but doesn't modify the strings
    kstat_ctl_t* kc = kstat_open();
    if (kc) {
        kstat_t* ksp = kstat_lookup(kc, const_cast<char*>("unix"), 0, const_cast<char*>("system_pages"));
        if (ksp && kstat_read(kc, ksp, nullptr) >= 0) {
            kstat_named_t* kn;

            // Free memory
            kn = reinterpret_cast<kstat_named_t*>(kstat_data_lookup(ksp, const_cast<char*>("freemem")));
            if (kn) {
                info.available = kn->value.ul * pagesize;
            }

            // Could also check availrmem, lotsfree, etc. for better estimation
        }
        kstat_close(kc);
    }

    info.used = info.total - info.available;
    return info;
}

SwapInfo SolarisSystemDataProvider::get_swap_info() {
    SwapInfo info;

    // Use swapctl to get swap info
    int num = swapctl(SC_GETNSWP, nullptr);
    if (num <= 0) return info;

    size_t size = sizeof(swaptbl_t) + (num - 1) * sizeof(swapent_t);
    std::vector<char> buf(size + num * MAXPATHLEN);
    swaptbl_t* st = reinterpret_cast<swaptbl_t*>(buf.data());
    st->swt_n = num;

    // Set up path pointers
    char* paths = buf.data() + sizeof(swaptbl_t) + (num - 1) * sizeof(swapent_t);
    for (int i = 0; i < num; ++i) {
        st->swt_ent[i].ste_path = paths + i * MAXPATHLEN;
    }

    num = swapctl(SC_LIST, st);
    if (num > 0) {
        long pagesize = sysconf(_SC_PAGESIZE);
        for (int i = 0; i < num; ++i) {
            info.total += st->swt_ent[i].ste_pages * pagesize;
            info.free += st->swt_ent[i].ste_free * pagesize;
        }
        info.used = info.total - info.free;
    }

    return info;
}

LoadAverage SolarisSystemDataProvider::get_load_average() {
    LoadAverage la;

    double loadavg[3];
    if (getloadavg(loadavg, 3) == 3) {
        la.one_min = loadavg[0];
        la.five_min = loadavg[1];
        la.fifteen_min = loadavg[2];
    }

    // Count processes by iterating /proc
    int total = 0;
    int running = 0;

    DIR* dir = opendir("/proc");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            // Skip non-numeric entries
            if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;

            char psinfo_path[64];
            snprintf(psinfo_path, sizeof(psinfo_path), "/proc/%s/psinfo", entry->d_name);

            int fd = open(psinfo_path, O_RDONLY);
            if (fd < 0) continue;

            psinfo_t psinfo;
            ssize_t n = read(fd, &psinfo, sizeof(psinfo));
            close(fd);

            if (n == sizeof(psinfo)) {
                total++;
                // Check if running (O = on processor, R = runnable)
                if (psinfo.pr_lwp.pr_sname == 'O' || psinfo.pr_lwp.pr_sname == 'R') {
                    running++;
                }
            }
        }
        closedir(dir);
    }

    la.total_tasks = total;
    la.running_tasks = running;

    return la;
}

UptimeInfo SolarisSystemDataProvider::get_uptime() {
    UptimeInfo info;

    // Use kstat to get boot time
    kstat_ctl_t* kc = kstat_open();
    if (kc) {
        kstat_t* ksp = kstat_lookup(kc, const_cast<char*>("unix"), 0, const_cast<char*>("system_misc"));
        if (ksp && kstat_read(kc, ksp, nullptr) >= 0) {
            kstat_named_t* kn = reinterpret_cast<kstat_named_t*>(
                kstat_data_lookup(ksp, const_cast<char*>("boot_time")));
            if (kn) {
                time_t boot_time = kn->value.ul;
                time_t now = time(nullptr);
                info.uptime_seconds = now - boot_time;
            }
        }
        kstat_close(kc);
    }

    // Idle time would need to be calculated from CPU idle percentage
    info.idle_seconds = 0;

    return info;
}

unsigned int SolarisSystemDataProvider::get_processor_count() const {
    return sysconf(_SC_NPROCESSORS_ONLN);
}

long SolarisSystemDataProvider::get_clock_ticks_per_second() const {
    return sysconf(_SC_CLK_TCK);
}

uint64_t SolarisSystemDataProvider::get_boot_time_ticks() const {
    uint64_t boot_time = 0;

    kstat_ctl_t* kc = kstat_open();
    if (kc) {
        kstat_t* ksp = kstat_lookup(kc, const_cast<char*>("unix"), 0, const_cast<char*>("system_misc"));
        if (ksp && kstat_read(kc, ksp, nullptr) >= 0) {
            kstat_named_t* kn = reinterpret_cast<kstat_named_t*>(
                kstat_data_lookup(ksp, const_cast<char*>("boot_time")));
            if (kn) {
                boot_time = kn->value.ul * sysconf(_SC_CLK_TCK);
            }
        }
        kstat_close(kc);
    }

    return boot_time;
}

std::string SolarisSystemDataProvider::get_system_info_string() const {
    std::string result = "SunOS";

    struct utsname uts;
    memset(&uts, 0, sizeof(uts));

    // Note: On Solaris, uname() returns non-negative on success (not necessarily 0)
    if (uname(&uts) >= 0) {
        // Build from utsname fields
        result = std::string(uts.sysname[0] ? uts.sysname : "SunOS");

        if (uts.release[0]) {
            result += " ";
            result += uts.release;
        }

        if (uts.machine[0]) {
            result += " ";
            result += uts.machine;
        }

        if (uts.version[0]) {
            result += " ";
            result += uts.version;
        }
    }

    // Try to get friendly release name from /etc/release
    FILE* f = fopen("/etc/release", "r");
    if (f) {
        char line[256] = {0};
        if (fgets(line, sizeof(line), f)) {
            // Trim trailing whitespace/newlines
            size_t len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' ||
                               line[len-1] == ' ' || line[len-1] == '\t')) {
                line[--len] = '\0';
            }
            // Find first non-whitespace
            const char* start = line;
            while (*start == ' ' || *start == '\t') {
                start++;
            }
            if (*start) {
                result += " (";
                result += start;
                result += ")";
            }
        }
        fclose(f);
    }

    return result;
}

} // namespace pex
