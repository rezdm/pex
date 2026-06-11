#include "../procfs_reader.hpp"
#include "../../../core/model/system_info.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <format>
#include <charconv>
#include <string_view>
#include <algorithm>

namespace fs = std::filesystem;

namespace pex {

std::vector<ProcessInfo> ProcfsReader::get_all_processes(const int64_t total_memory_input) {
    std::vector<ProcessInfo> processes;

    // Fetch memory info once for the entire snapshot if not provided
    int64_t total_memory = total_memory_input;
    if (total_memory <= 0) {
        const auto mem_info = SystemInfo::get_memory_info();
        total_memory = mem_info.total;
    }

    try {
        for (const auto& entry : fs::directory_iterator("/proc")) {
            try {
                if (!entry.is_directory()) continue;

                const auto& name = entry.path().filename().string();
                int pid = 0;
                if (auto [ptr, ec] = std::from_chars(name.data(), name.data() + name.size(), pid); ec != std::errc{} || ptr != name.data() + name.size()) continue;

                if (auto info = get_process_info(pid, total_memory)) {
                    processes.push_back(std::move(*info));
                }
            } catch (const std::exception&) {
                continue;
            }
        }
    } catch (const std::exception& e) {
        add_error(std::format("Failed to iterate /proc: {}", e.what()));
    }

    return processes;
}

std::optional<ProcessInfo> ProcfsReader::get_process_info(const int pid) {
    const auto mem_info = SystemInfo::get_memory_info();
    return get_process_info(pid, mem_info.total);
}

std::optional<ProcessInfo> ProcfsReader::get_process_info(int pid, int64_t total_memory) {
    std::string proc_path = "/proc/" + std::to_string(pid);

    std::string stat_content = read_file(proc_path + "/stat");
    if (stat_content.empty()) return std::nullopt;

    ProcessInfo info;
    info.pid = pid;

    size_t comm_start = stat_content.find('(');
    size_t comm_end = stat_content.rfind(')');
    if (comm_start == std::string::npos || comm_end == std::string::npos || comm_end <= comm_start) {
        add_error(std::format("PID {}: malformed stat (missing comm)", pid));
        return std::nullopt;
    }

    info.name = stat_content.substr(comm_start + 1, comm_end - comm_start - 1);

    if (comm_end + 2 >= stat_content.size()) {
        add_error(std::format("PID {}: truncated stat (no fields after comm)", pid));
        return std::nullopt;
    }

    std::istringstream iss(stat_content.substr(comm_end + 2));
    std::string state;
    int ppid = 0, pgrp = 0, session = 0, tty_nr = 0, tpgid = 0;
    unsigned int flags = 0;
    uint64_t minflt = 0, cminflt = 0, majflt = 0, cmajflt = 0, utime = 0, stime = 0;
    int64_t cutime = 0, cstime = 0, priority = 0, nice = 0;
    int64_t num_threads = 1, itrealvalue = 0;
    uint64_t starttime = 0;

    iss >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags
        >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime
        >> cutime >> cstime >> priority >> nice >> num_threads >> itrealvalue >> starttime;

    if (iss.fail()) {
        add_error(std::format("PID {}: failed to parse stat fields", pid));
        return std::nullopt;
    }

    info.state_char = state.empty() ? '?' : state[0];
    info.parent_pid = ppid;
    info.user_time = utime;
    info.kernel_time = stime;
    info.priority = static_cast<int>(priority);
    info.thread_count = static_cast<int>(num_threads);

    auto& sys = SystemInfo::instance();
    uint64_t boot_time = sys.get_boot_time_ticks();
    long ticks = sys.get_clock_ticks_per_second();
    if (ticks > 0) {
        uint64_t start_seconds = boot_time + (starttime / ticks);
        info.start_time = std::chrono::system_clock::from_time_t(static_cast<time_t>(start_seconds));
    }

    if (std::string statm = read_file(proc_path + "/statm"); !statm.empty()) {
        std::istringstream statm_iss(statm);
        uint64_t size = 0, resident = 0;
        statm_iss >> size >> resident;
        if (!statm_iss.fail()) {
            static const long page_size = sysconf(_SC_PAGESIZE);
            info.virtual_memory = static_cast<int64_t>(size * page_size);
            info.resident_memory = static_cast<int64_t>(resident * page_size);

            if (total_memory > 0) {
                info.memory_percent = static_cast<double>(info.resident_memory) / static_cast<double>(total_memory) * 100.0;
            }
        }
    }

    std::string cmdline = read_file(proc_path + "/cmdline");
    std::ranges::replace(cmdline, '\0', ' ');
    if (!cmdline.empty() && cmdline.back() == ' ') {
        cmdline.pop_back();
    }
    info.command_line = cmdline;

    info.executable_path = read_symlink(proc_path + "/exe");

    // Storage I/O counters. /proc/<pid>/io needs the same privilege as ptrace
    // read access; unreadable (other users' processes without caps) leaves 0.
    if (std::string io = read_file(proc_path + "/io"); !io.empty()) {
        auto parse_io_field = [&io](const std::string_view key, uint64_t& out) {
            const size_t pos = io.find(key);
            if (pos == std::string::npos) return;
            size_t v = pos + key.size();
            while (v < io.size() && io[v] == ' ') v++;
            std::from_chars(io.data() + v, io.data() + io.size(), out);
        };
        parse_io_field("read_bytes:", info.io_read_bytes);
        parse_io_field("write_bytes:", info.io_write_bytes);
    }

    std::string status = read_file(proc_path + "/status");
    std::istringstream status_iss(status);
    std::string line;
    while (std::getline(status_iss, line)) {
        if (line.starts_with("Uid:")) {
            std::istringstream uid_iss(line);
            std::string key;
            int uid = 0;
            uid_iss >> key >> uid;
            info.user_name = get_username(uid);
            break;
        }
    }

    return info;
}

} // namespace pex
