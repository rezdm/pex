#include "freebsd_process_data_provider.hpp"

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/param.h>
#include <libprocstat.h>
#include <pwd.h>
#include <unistd.h>
#include <limits.h>

#include <cstring>
#include <sstream>
#include <unordered_set>

namespace pex {

FreeBSDProcessDataProvider::FreeBSDProcessDataProvider() {
    // Cache clock ticks per second for CPU time calculations
    clock_ticks_ = sysconf(_SC_CLK_TCK);
}

FreeBSDProcessDataProvider::~FreeBSDProcessDataProvider() = default;

void FreeBSDProcessDataProvider::add_error(const std::string& context, const std::string& message) {
    std::lock_guard lock(errors_mutex_);
    recent_errors_.push_back({std::chrono::steady_clock::now(), context + ": " + message});
    if (recent_errors_.size() > kMaxErrors) {
        recent_errors_.erase(recent_errors_.begin());
    }
}

std::string FreeBSDProcessDataProvider::get_executable_path(int pid) {
    // Ask the kernel for the true vnode path of the executable.
    // Unlike argv[0], this cannot be falsified by the process.
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, pid };
    char pathbuf[PATH_MAX];
    size_t len = sizeof(pathbuf);
    if (sysctl(mib, 4, pathbuf, &len, nullptr, 0) == 0 && len > 1) {
        pathbuf[len - 1] = '\0';  // Ensure NUL termination
        return pathbuf;
    }
    return {};
}

char FreeBSDProcessDataProvider::map_state(int state) {
    // FreeBSD process states from sys/proc.h
    switch (state) {
        case SIDL:    return 'I';  // Process being created
        case SRUN:    return 'R';  // Running
        case SSLEEP:  return 'S';  // Sleeping on an address
        case SSTOP:   return 'T';  // Stopped
        case SZOMB:   return 'Z';  // Zombie
        case SWAIT:   return 'D';  // Waiting for interrupt
        case SLOCK:   return 'D';  // Blocked on a lock
        default:      return '?';
    }
}

std::string FreeBSDProcessDataProvider::get_username(uid_t uid) {
    // Check cache first
    {
        std::lock_guard lock(username_cache_mutex_);
        auto it = username_cache_.find(uid);
        if (it != username_cache_.end()) {
            return it->second;
        }
    }

    // Lookup using reentrant version and cache
    std::string name;
    struct passwd pwd_buf;
    struct passwd* result = nullptr;
    char buf[1024];
    if (getpwuid_r(uid, &pwd_buf, buf, sizeof(buf), &result) == 0 && result) {
        name = result->pw_name;
    } else {
        name = std::to_string(uid);
    }

    {
        std::lock_guard lock(username_cache_mutex_);
        username_cache_[uid] = name;
    }
    return name;
}

std::vector<ProcessInfo> FreeBSDProcessDataProvider::get_all_processes(int64_t total_memory) {
    std::vector<ProcessInfo> processes;

    // Get total memory if not provided
    if (total_memory < 0) {
        size_t len = sizeof(total_memory);
        sysctlbyname("hw.physmem", &total_memory, &len, nullptr, 0);
    }

    // Use sysctl to get process list
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
    size_t len = 0;

    // First call to get size
    if (sysctl(mib, 3, nullptr, &len, nullptr, 0) < 0) {
        add_error("get_all_processes", "sysctl KERN_PROC failed");
        return processes;
    }

    // Allocate buffer with some extra space
    len = len * 5 / 4;  // 25% extra
    std::vector<char> buf(len);

    if (sysctl(mib, 3, buf.data(), &len, nullptr, 0) < 0) {
        add_error("get_all_processes", "sysctl KERN_PROC failed (second call)");
        return processes;
    }

    // Parse kinfo_proc structures
    size_t count = len / sizeof(struct kinfo_proc);
    struct kinfo_proc* kp = reinterpret_cast<struct kinfo_proc*>(buf.data());

    // Indices into 'processes' whose exe path/argv must be fetched (cache miss)
    std::vector<std::pair<size_t, uint64_t>> misses;
    std::unordered_set<int> current_pids;

    for (size_t i = 0; i < count; ++i) {
        // Skip kernel idle processes - these accumulate idle time across all CPUs
        // and don't represent meaningful process activity. The name is typically
        // "idle" with PID 0 or part of the kernel (PPID 0, PID < 10).
        if ((kp[i].ki_pid == 0) ||
            (kp[i].ki_ppid == 0 && kp[i].ki_pid <= 10 &&
             std::string(kp[i].ki_comm) == "idle")) {
            continue;
        }

        ProcessInfo info;
        info.pid = kp[i].ki_pid;
        info.parent_pid = kp[i].ki_ppid;
        info.name = kp[i].ki_comm;
        info.state_char = map_state(kp[i].ki_stat);
        info.is_kernel_thread = (kp[i].ki_flag & P_KPROC) != 0;
        info.user_name = get_username(kp[i].ki_uid);
        info.priority = kp[i].ki_pri.pri_level;
        info.thread_count = kp[i].ki_numthreads;

        // Memory info
        info.resident_memory = kp[i].ki_rssize * getpagesize();
        info.virtual_memory = kp[i].ki_size;
        if (total_memory > 0) {
            info.memory_percent = (static_cast<double>(info.resident_memory) / total_memory) * 100.0;
        }

        // CPU times (in clock ticks)
        info.user_time = kp[i].ki_rusage.ru_utime.tv_sec * clock_ticks_ +
                         kp[i].ki_rusage.ru_utime.tv_usec * clock_ticks_ / 1000000;
        info.kernel_time = kp[i].ki_rusage.ru_stime.tv_sec * clock_ticks_ +
                           kp[i].ki_rusage.ru_stime.tv_usec * clock_ticks_ / 1000000;

        // Start time
        auto start_sec = std::chrono::seconds(kp[i].ki_start.tv_sec);
        info.start_time = std::chrono::system_clock::time_point(start_sec);

        // Exe path and argv are cached per process instance: fetching them
        // costs a sysctl + procstat round-trip per PID per tick otherwise
        // (issue #52). exec() keeps the start time but replaces comm, so the
        // entry is validated by both.
        const uint64_t start_us =
            static_cast<uint64_t>(kp[i].ki_start.tv_sec) * 1000000ULL +
            static_cast<uint64_t>(kp[i].ki_start.tv_usec);
        current_pids.insert(info.pid);
        if (const auto it = proc_strings_cache_.find(info.pid);
            it != proc_strings_cache_.end() &&
            it->second.start_us == start_us && it->second.comm == info.name) {
            info.executable_path = it->second.executable_path;
            info.command_line = it->second.command_line;
        } else {
            misses.emplace_back(processes.size(), start_us);
        }

        processes.push_back(std::move(info));
    }

    // Fetch exe paths and command lines for cache misses only
    if (!misses.empty()) {
        struct procstat* ps = procstat_open_sysctl();
        for (const auto& [idx, start_us] : misses) {
            auto& info = processes[idx];
            info.executable_path = get_executable_path(info.pid);
            if (ps) {
                unsigned int cnt;
                kinfo_proc* kproc = procstat_getprocs(ps, KERN_PROC_PID, info.pid, &cnt);
                if (kproc && cnt > 0) {
                    char** args = procstat_getargv(ps, kproc, 0);
                    if (args) {
                        std::ostringstream cmdline;
                        for (int j = 0; args[j] != nullptr; ++j) {
                            if (j > 0) cmdline << ' ';
                            cmdline << args[j];
                        }
                        info.command_line = cmdline.str();
                        // Fall back to argv[0] only if the kernel path lookup failed
                        if (info.executable_path.empty() && args[0]) {
                            info.executable_path = args[0];
                        }
                        procstat_freeargv(ps);
                    }
                    procstat_freeprocs(ps, kproc);
                }
            }
            proc_strings_cache_[info.pid] =
                {start_us, info.name, info.executable_path, info.command_line};
        }
        if (ps) procstat_close(ps);
    }

    // Drop cache entries for processes that no longer exist
    std::erase_if(proc_strings_cache_, [&current_pids](const auto& entry) {
        return !current_pids.contains(entry.first);
    });

    for (auto& info : processes) {
        if (info.command_line.empty()) {
            info.command_line = info.name;
        }
    }

    return processes;
}

std::optional<ProcessInfo> FreeBSDProcessDataProvider::get_process_info(int pid, int64_t total_memory) {
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, pid };
    struct kinfo_proc kp;
    size_t len = sizeof(kp);

    if (sysctl(mib, 4, &kp, &len, nullptr, 0) < 0 || len == 0) {
        return std::nullopt;
    }

    ProcessInfo info;
    info.pid = kp.ki_pid;
    info.parent_pid = kp.ki_ppid;
    info.name = kp.ki_comm;
    info.state_char = map_state(kp.ki_stat);
    info.is_kernel_thread = (kp.ki_flag & P_KPROC) != 0;
    info.user_name = get_username(kp.ki_uid);
    info.executable_path = get_executable_path(pid);
    info.priority = kp.ki_pri.pri_level;
    info.thread_count = kp.ki_numthreads;
    info.resident_memory = kp.ki_rssize * getpagesize();
    info.virtual_memory = kp.ki_size;

    if (total_memory > 0) {
        info.memory_percent = (static_cast<double>(info.resident_memory) / total_memory) * 100.0;
    }

    info.user_time = kp.ki_rusage.ru_utime.tv_sec * clock_ticks_ +
                     kp.ki_rusage.ru_utime.tv_usec * clock_ticks_ / 1000000;
    info.kernel_time = kp.ki_rusage.ru_stime.tv_sec * clock_ticks_ +
                       kp.ki_rusage.ru_stime.tv_usec * clock_ticks_ / 1000000;

    auto start_sec = std::chrono::seconds(kp.ki_start.tv_sec);
    info.start_time = std::chrono::system_clock::time_point(start_sec);

    return info;
}

} // namespace pex
