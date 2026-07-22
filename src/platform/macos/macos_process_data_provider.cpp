#include "macos_process_data_provider.hpp"

#include <libproc.h>
#include <sys/proc_info.h>
#include <sys/sysctl.h>
#include <pwd.h>
#include <unistd.h>

#include <cstring>
#include <chrono>

namespace pex {

namespace {

// proc_bsdinfo.pbi_status values (from <sys/proc.h>, inlined to avoid that
// header clashing with libproc's struct names).
constexpr uint32_t kSIDL = 1;   // being created
constexpr uint32_t kSRUN = 2;   // runnable
constexpr uint32_t kSSLEEP = 3; // sleeping
constexpr uint32_t kSSTOP = 4;  // stopped
constexpr uint32_t kSZOMB = 5;  // zombie

char map_state(uint32_t status) {
    switch (status) {
        case kSIDL:   return 'I';
        case kSRUN:   return 'R';
        case kSSLEEP: return 'S';
        case kSSTOP:  return 'T';
        case kSZOMB:  return 'Z';
        default:      return '?';
    }
}

} // namespace

MacosProcessDataProvider::MacosProcessDataProvider() {
    if (const long t = sysconf(_SC_CLK_TCK); t > 0) {
        clock_ticks_ = t;
        ns_per_tick_ = 1000000000ULL / static_cast<uint64_t>(t);
    }
    int argmax = 0;
    size_t len = sizeof(argmax);
    if (sysctlbyname("kern.argmax", &argmax, &len, nullptr, 0) == 0 && argmax > 0) {
        argmax_ = static_cast<size_t>(argmax);
    }
}

MacosProcessDataProvider::~MacosProcessDataProvider() = default;

void MacosProcessDataProvider::add_error(const std::string& context, const std::string& message) {
    std::lock_guard lock(errors_mutex_);
    recent_errors_.push_back({std::chrono::steady_clock::now(), context + ": " + message});
    if (recent_errors_.size() > kMaxErrors) {
        recent_errors_.erase(recent_errors_.begin());
    }
}

uint64_t MacosProcessDataProvider::ns_to_ticks(uint64_t ns) const {
    return ns_per_tick_ ? ns / ns_per_tick_ : 0;
}

std::string MacosProcessDataProvider::get_username(uint32_t uid) {
    {
        std::lock_guard lock(username_cache_mutex_);
        if (auto it = username_cache_.find(uid); it != username_cache_.end()) {
            return it->second;
        }
    }
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

bool MacosProcessDataProvider::fill_from_libproc(int pid, int64_t total_memory, ProcessInfo& info) {
    struct proc_bsdinfo bi;
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &bi, sizeof(bi)) != static_cast<int>(sizeof(bi))) {
        return false;
    }

    info.pid = pid;
    info.parent_pid = static_cast<int>(bi.pbi_ppid);
    info.name = bi.pbi_name[0] ? std::string(bi.pbi_name) : std::string(bi.pbi_comm);
    info.state_char = map_state(bi.pbi_status);
    info.user_name = get_username(bi.pbi_uid);
    info.is_kernel_thread = (pid == 0);  // kernel_task; macOS has no user-listed kthreads
    info.priority = static_cast<int>(bi.pbi_nice);

    const auto start_us = std::chrono::seconds(bi.pbi_start_tvsec) +
                          std::chrono::microseconds(bi.pbi_start_tvusec);
    info.start_time = std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(start_us));

    // Task info is absent for zombies and kernel_task; leave those fields zero.
    struct proc_taskinfo ti;
    if (proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &ti, sizeof(ti)) == static_cast<int>(sizeof(ti))) {
        info.resident_memory = static_cast<int64_t>(ti.pti_resident_size);
        info.virtual_memory = static_cast<int64_t>(ti.pti_virtual_size);
        info.thread_count = ti.pti_threadnum;
        info.priority = ti.pti_priority;
        info.user_time = ns_to_ticks(ti.pti_total_user);
        info.kernel_time = ns_to_ticks(ti.pti_total_system);
    }

    if (total_memory > 0 && info.resident_memory > 0) {
        info.memory_percent = static_cast<double>(info.resident_memory) / total_memory * 100.0;
    }

    char pathbuf[PROC_PIDPATHINFO_MAXSIZE];
    if (proc_pidpath(pid, pathbuf, sizeof(pathbuf)) > 0) {
        info.executable_path = pathbuf;
    }
    return true;
}

bool MacosProcessDataProvider::read_proc_args(int pid, std::vector<char>& buf,
                                              std::string* out_cmdline,
                                              std::vector<EnvironmentVariable>* out_env) {
    if (buf.size() < argmax_) buf.resize(argmax_);

    int mib[3] = {CTL_KERN, KERN_PROCARGS2, pid};
    size_t size = buf.size();
    if (sysctl(mib, 3, buf.data(), &size, nullptr, 0) != 0 || size < sizeof(int)) {
        return false;  // EPERM for foreign processes, ESRCH if it exited
    }

    const char* data = buf.data();
    int argc = 0;
    std::memcpy(&argc, data, sizeof(argc));
    size_t pos = sizeof(argc);

    // The executable path precedes argv[0], padded with trailing NULs.
    while (pos < size && data[pos] != '\0') ++pos;
    while (pos < size && data[pos] == '\0') ++pos;

    if (out_cmdline) {
        std::string cmdline;
        for (int i = 0; i < argc && pos < size; ++i) {
            const char* arg = data + pos;
            while (pos < size && data[pos] != '\0') ++pos;
            if (i > 0) cmdline += ' ';
            cmdline.append(arg, data + pos);
            if (pos < size) ++pos;  // skip NUL
        }
        *out_cmdline = std::move(cmdline);
    } else {
        // Still advance past argv so env parsing (if requested) starts correctly.
        for (int i = 0; i < argc && pos < size; ++i) {
            while (pos < size && data[pos] != '\0') ++pos;
            if (pos < size) ++pos;
        }
    }

    if (out_env) {
        while (pos < size) {
            const char* entry = data + pos;
            while (pos < size && data[pos] != '\0') ++pos;
            std::string kv(entry, data + pos);
            if (pos < size) ++pos;
            if (kv.empty()) continue;
            if (const auto eq = kv.find('='); eq != std::string::npos) {
                out_env->push_back({kv.substr(0, eq), kv.substr(eq + 1)});
            }
        }
    }
    return true;
}

std::vector<ProcessInfo> MacosProcessDataProvider::get_all_processes(int64_t total_memory) {
    std::vector<ProcessInfo> processes;

    if (total_memory < 0) {
        uint64_t mem = 0;
        size_t len = sizeof(mem);
        if (sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0) {
            total_memory = static_cast<int64_t>(mem);
        }
    }

    const int needed = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (needed <= 0) {
        add_error("get_all_processes", "proc_listpids failed");
        return processes;
    }
    std::vector<pid_t> pids(needed / sizeof(pid_t) + 64);
    const int got = proc_listpids(PROC_ALL_PIDS, 0, pids.data(),
                                  static_cast<int>(pids.size() * sizeof(pid_t)));
    const int count = got > 0 ? got / static_cast<int>(sizeof(pid_t)) : 0;

    processes.reserve(count);
    std::vector<char> argbuf(argmax_);
    for (int i = 0; i < count; ++i) {
        const int pid = pids[i];
        if (pid <= 0) continue;  // kernel_task (0) has no bsdinfo; skip like other ports skip idle
        ProcessInfo info;
        if (!fill_from_libproc(pid, total_memory, info)) continue;

        std::string cmd;
        if (read_proc_args(pid, argbuf, &cmd, nullptr) && !cmd.empty()) {
            info.command_line = std::move(cmd);
        } else {
            info.command_line = info.executable_path.empty() ? info.name : info.executable_path;
        }
        processes.push_back(std::move(info));
    }
    return processes;
}

std::optional<ProcessInfo> MacosProcessDataProvider::get_process_info(int pid, int64_t total_memory) {
    ProcessInfo info;
    if (!fill_from_libproc(pid, total_memory, info)) {
        return std::nullopt;
    }
    std::vector<char> argbuf(argmax_);
    std::string cmd;
    if (read_proc_args(pid, argbuf, &cmd, nullptr) && !cmd.empty()) {
        info.command_line = std::move(cmd);
    } else {
        info.command_line = info.executable_path.empty() ? info.name : info.executable_path;
    }
    return info;
}

std::vector<ParseError> MacosProcessDataProvider::get_recent_errors() {
    std::lock_guard lock(errors_mutex_);
    return recent_errors_;
}

void MacosProcessDataProvider::clear_errors() {
    std::lock_guard lock(errors_mutex_);
    recent_errors_.clear();
}

} // namespace pex
