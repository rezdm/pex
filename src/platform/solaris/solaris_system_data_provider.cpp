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
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <map>
#include <utility>
#include <vector>

namespace pex {

SolarisSystemDataProvider::SolarisSystemDataProvider() {
    kc_ = kstat_open();
}

SolarisSystemDataProvider::~SolarisSystemDataProvider() {
    if (kc_) {
        kstat_close(kc_);
        kc_ = nullptr;
    }
}

void SolarisSystemDataProvider::ensure_kstat() const {
    if (!kc_) {
        kc_ = kstat_open();
        return;
    }
    // Update the kstat chain to pick up new/removed kstats
    if (kstat_chain_update(kc_) == -1) {
        // Chain update failed, reopen
        kstat_close(kc_);
        kc_ = kstat_open();
    }
}

CpuTimes SolarisSystemDataProvider::get_cpu_times() {
    CpuTimes times;

    ensure_kstat();
    if (!kc_) return times;

    // Aggregate CPU times from all cpu_stat instances
    for (kstat_t* ksp = kc_->kc_chain; ksp != nullptr; ksp = ksp->ks_next) {
        if (strcmp(ksp->ks_module, "cpu_stat") != 0) continue;

        if (kstat_read(kc_, ksp, nullptr) < 0) continue;

        cpu_stat_t* cs = reinterpret_cast<cpu_stat_t*>(ksp->ks_data);
        if (!cs) continue;

        times.user += cs->cpu_sysinfo.cpu[CPU_USER];
        times.system += cs->cpu_sysinfo.cpu[CPU_KERNEL];
        times.idle += cs->cpu_sysinfo.cpu[CPU_IDLE];
        times.iowait += cs->cpu_sysinfo.cpu[CPU_WAIT];
        // Solaris doesn't have nice, irq, softirq, steal in cpu_stat
    }

    return times;
}

std::vector<CpuTimes> SolarisSystemDataProvider::get_per_cpu_times() {
    std::vector<CpuTimes> result;
    get_per_cpu_times(result);
    return result;
}

void SolarisSystemDataProvider::get_per_cpu_times(std::vector<CpuTimes>& out) {
    ensure_kstat();
    if (!kc_) {
        out.assign(cpu_slot_instances_.empty()
                       ? static_cast<size_t>(get_processor_count())
                       : cpu_slot_instances_.size(),
                   CpuTimes{});
        return;
    }

    // Collect the counters present this tick, keyed by kstat instance id.
    std::map<int, CpuTimes> by_instance;
    for (kstat_t* ksp = kc_->kc_chain; ksp != nullptr; ksp = ksp->ks_next) {
        if (strcmp(ksp->ks_module, "cpu_stat") != 0) continue;
        if (ksp->ks_instance < 0) continue;

        if (kstat_read(kc_, ksp, nullptr) < 0) continue;

        cpu_stat_t* cs = reinterpret_cast<cpu_stat_t*>(ksp->ks_data);
        if (!cs) continue;

        CpuTimes times;
        times.user = cs->cpu_sysinfo.cpu[CPU_USER];
        times.system = cs->cpu_sysinfo.cpu[CPU_KERNEL];
        times.idle = cs->cpu_sysinfo.cpu[CPU_IDLE];
        times.iowait = cs->cpu_sysinfo.cpu[CPU_WAIT];
        by_instance.emplace(ksp->ks_instance, times);
    }

    // Assign new instances to fresh slots (sorted, appended — never reordered),
    // so an instance keeps its slot for the life of this provider. The delta
    // math in DataStore compares out[i] this tick to out[i] last tick, so the
    // slot for a given CPU must not move even as other CPUs come and go.
    for (const auto& [instance, times] : by_instance) {
        if (std::find(cpu_slot_instances_.begin(), cpu_slot_instances_.end(), instance)
            == cpu_slot_instances_.end()) {
            cpu_slot_instances_.push_back(instance);
        }
    }

    // Emit one entry per known slot; a CPU absent this tick (offlined) yields
    // zeros in its slot rather than shifting every later CPU down.
    out.assign(cpu_slot_instances_.size(), CpuTimes{});
    for (size_t slot = 0; slot < cpu_slot_instances_.size(); ++slot) {
        if (const auto it = by_instance.find(cpu_slot_instances_[slot]); it != by_instance.end()) {
            out[slot] = it->second;
        }
    }
}

MemoryInfo SolarisSystemDataProvider::get_memory_info() {
    MemoryInfo info;

    // Total physical memory
    long pages = sysconf(_SC_PHYS_PAGES);
    long pagesize = sysconf(_SC_PAGESIZE);
    info.total = pages * pagesize;

    // Available memory - use kstat for freemem
    // Note: kstat API uses char* not const char*, but doesn't modify the strings
    ensure_kstat();
    if (kc_) {
        kstat_t* ksp = kstat_lookup(kc_, const_cast<char*>("unix"), 0, const_cast<char*>("system_pages"));
        if (ksp && kstat_read(kc_, ksp, nullptr) >= 0) {
            kstat_named_t* kn;

            // Free memory
            kn = reinterpret_cast<kstat_named_t*>(kstat_data_lookup(ksp, const_cast<char*>("freemem")));
            if (kn) {
                info.available = kn->value.ul * pagesize;
            }

            // Could also check availrmem, lotsfree, etc. for better estimation
        }
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

    // total_tasks/running_tasks stay 0: nothing displays them, and counting
    // them here re-read every /proc/<pid>/psinfo on every tick — a full
    // second scan on top of get_all_processes. The snapshot already carries
    // process_count/running_count computed from the process list.

    return la;
}

UptimeInfo SolarisSystemDataProvider::get_uptime() {
    UptimeInfo info;

    // Use kstat to get boot time
    ensure_kstat();
    if (kc_) {
        kstat_t* ksp = kstat_lookup(kc_, const_cast<char*>("unix"), 0, const_cast<char*>("system_misc"));
        if (ksp && kstat_read(kc_, ksp, nullptr) >= 0) {
            kstat_named_t* kn = reinterpret_cast<kstat_named_t*>(
                kstat_data_lookup(ksp, const_cast<char*>("boot_time")));
            if (kn) {
                time_t boot_time = kn->value.ul;
                time_t now = time(nullptr);
                info.uptime_seconds = now - boot_time;
            }
        }
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

uint64_t SolarisSystemDataProvider::get_boot_time_seconds() const {
    uint64_t boot_time = 0;

    ensure_kstat();
    if (kc_) {
        kstat_t* ksp = kstat_lookup(kc_, const_cast<char*>("unix"), 0, const_cast<char*>("system_misc"));
        if (ksp && kstat_read(kc_, ksp, nullptr) >= 0) {
            kstat_named_t* kn = reinterpret_cast<kstat_named_t*>(
                kstat_data_lookup(ksp, const_cast<char*>("boot_time")));
            if (kn) {
                boot_time = kn->value.ul;
            }
        }
    }

    return boot_time;
}

std::string SolarisSystemDataProvider::get_system_info_string() const {
    std::string result = "SunOS";

    struct utsname uts = {};

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
    if (FILE* f = fopen("/etc/release", "r")) {
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
