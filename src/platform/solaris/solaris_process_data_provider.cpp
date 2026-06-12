#include "solaris_process_data_provider.hpp"

#include <sys/types.h>
#include <sys/proc.h>  // SSYS flag
#include <procfs.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>

#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace pex {

SolarisProcessDataProvider::SolarisProcessDataProvider() {
    // Cache clock ticks per second for CPU time calculations
    clock_ticks_ = sysconf(_SC_CLK_TCK);
}

SolarisProcessDataProvider::~SolarisProcessDataProvider() = default;

void SolarisProcessDataProvider::add_error(const std::string& context, const std::string& message) {
    std::lock_guard lock(errors_mutex_);
    recent_errors_.push_back({std::chrono::steady_clock::now(), context + ": " + message});
    if (recent_errors_.size() > 10) {
        recent_errors_.erase(recent_errors_.begin());
    }
}

char SolarisProcessDataProvider::map_state(char state) {
    // Solaris process states from procfs
    switch (state) {
        case 'O': return 'R';  // On processor (running)
        case 'S': return 'S';  // Sleeping
        case 'R': return 'R';  // Runnable
        case 'Z': return 'Z';  // Zombie
        case 'T': return 'T';  // Stopped
        case 'I': return 'I';  // Idle
        case 'W': return 'D';  // Waiting
        default:  return '?';
    }
}

std::string SolarisProcessDataProvider::get_username(uid_t uid) {
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

std::optional<ProcessInfo> SolarisProcessDataProvider::read_process_info(int pid, int64_t total_memory) {
    std::string proc_path = "/proc/" + std::to_string(pid);

    // Read psinfo
    std::string psinfo_path = proc_path + "/psinfo";
    int fd = open(psinfo_path.c_str(), O_RDONLY);
    if (fd < 0) {
        return std::nullopt;
    }

    psinfo_t psinfo;
    ssize_t n = read(fd, &psinfo, sizeof(psinfo));
    close(fd);

    if (n != sizeof(psinfo)) {
        return std::nullopt;
    }

    ProcessInfo info;
    info.pid = psinfo.pr_pid;
    info.parent_pid = psinfo.pr_ppid;
    info.name = psinfo.pr_fname;
    info.state_char = map_state(psinfo.pr_lwp.pr_sname);
    info.is_kernel_thread = (psinfo.pr_flag & SSYS) != 0;  // System process (sched, pageout, ...)
    info.user_name = get_username(psinfo.pr_uid);
    info.priority = psinfo.pr_lwp.pr_pri;  // Dynamic priority, consistent with other platforms
    info.thread_count = psinfo.pr_nlwp;

    // Memory info
    info.resident_memory = psinfo.pr_rssize * 1024;  // rssize is in KB
    info.virtual_memory = psinfo.pr_size * 1024;     // size is in KB
    if (total_memory > 0) {
        info.memory_percent = (static_cast<double>(info.resident_memory) / total_memory) * 100.0;
    }

    // CPU times (in clock ticks)
    info.user_time = psinfo.pr_time.tv_sec * clock_ticks_ +
                     psinfo.pr_time.tv_nsec * clock_ticks_ / 1000000000;
    // Solaris pr_time includes both user and system time
    // We'll split it based on pr_pctcpu later or just use combined
    info.kernel_time = 0;

    // Start time
    auto start_sec = std::chrono::seconds(psinfo.pr_start.tv_sec);
    info.start_time = std::chrono::system_clock::time_point(start_sec);

    // Command line from pr_psargs
    info.command_line = psinfo.pr_psargs;
    if (info.command_line.empty()) {
        info.command_line = info.name;
    }

    // Executable path - try to read from /proc/<pid>/path/a.out
    std::string exe_path = proc_path + "/path/a.out";
    try {
        info.executable_path = fs::read_symlink(exe_path).string();
    } catch (const std::exception&) {
        info.executable_path = info.name;
    }

    return info;
}

std::vector<ProcessInfo> SolarisProcessDataProvider::get_all_processes(int64_t total_memory) {
    std::vector<ProcessInfo> processes;

    // Get total memory if not provided
    if (total_memory < 0) {
        total_memory = sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE);
    }

    try {
        for (const auto& entry : fs::directory_iterator("/proc")) {
            if (!entry.is_directory()) continue;

            std::string name = entry.path().filename().string();
            int pid = 0;
            try {
                pid = std::stoi(name);
            } catch (const std::exception&) {
                continue;  // Not a PID directory
            }

            auto info = read_process_info(pid, total_memory);
            if (info) {
                processes.push_back(std::move(*info));
            }
        }
    } catch (const std::exception& e) {
        add_error("get_all_processes", e.what());
    }

    return processes;
}

std::optional<ProcessInfo> SolarisProcessDataProvider::get_process_info(int pid, int64_t total_memory) {
    if (total_memory < 0) {
        total_memory = sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE);
    }
    return read_process_info(pid, total_memory);
}

} // namespace pex
