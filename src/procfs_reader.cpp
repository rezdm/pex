#include "procfs_reader.hpp"
#include "system_info.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>
#include <algorithm>
#include <format>
#include <set>
#include <arpa/inet.h>

namespace fs = std::filesystem;

namespace pex {

std::string ProcfsReader::read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) return {};
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string ProcfsReader::read_symlink(const std::string& path) {
    char buf[4096];
    ssize_t len = readlink(path.c_str(), buf, sizeof(buf) - 1);
    if (len == -1) return {};
    buf[len] = '\0';
    return buf;
}

std::string ProcfsReader::get_username(int uid) {
    auto it = uid_cache_.find(uid);
    if (it != uid_cache_.end()) {
        return it->second;
    }

    struct passwd* pw = getpwuid(uid);
    std::string name = pw ? pw->pw_name : std::to_string(uid);
    uid_cache_[uid] = name;
    return name;
}

std::vector<ProcessInfo> ProcfsReader::get_all_processes() {
    std::vector<ProcessInfo> processes;

    for (const auto& entry : fs::directory_iterator("/proc")) {
        if (!entry.is_directory()) continue;

        const auto& name = entry.path().filename().string();
        int pid = 0;
        auto [ptr, ec] = std::from_chars(name.data(), name.data() + name.size(), pid);
        if (ec != std::errc{} || ptr != name.data() + name.size()) continue;

        auto info = get_process_info(pid);
        if (info) {
            processes.push_back(std::move(*info));
        }
    }

    return processes;
}

std::optional<ProcessInfo> ProcfsReader::get_process_info(int pid) {
    std::string proc_path = "/proc/" + std::to_string(pid);

    // Read stat file
    std::string stat_content = read_file(proc_path + "/stat");
    if (stat_content.empty()) return std::nullopt;

    ProcessInfo info;
    info.pid = pid;

    // Parse stat - format: pid (comm) state ppid ...
    // comm can contain spaces and parentheses, so find the last ')'
    size_t comm_start = stat_content.find('(');
    size_t comm_end = stat_content.rfind(')');
    if (comm_start == std::string::npos || comm_end == std::string::npos) {
        return std::nullopt;
    }

    info.name = stat_content.substr(comm_start + 1, comm_end - comm_start - 1);

    // Parse fields after comm
    std::istringstream iss(stat_content.substr(comm_end + 2));
    std::string state;
    int ppid, pgrp, session, tty_nr, tpgid;
    unsigned int flags;
    uint64_t minflt, cminflt, majflt, cmajflt, utime, stime;
    int64_t cutime, cstime, priority, nice;
    int64_t num_threads, itrealvalue;
    uint64_t starttime;

    iss >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags
        >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime
        >> cutime >> cstime >> priority >> nice >> num_threads >> itrealvalue >> starttime;

    info.state_char = state.empty() ? '?' : state[0];
    info.parent_pid = ppid;
    info.user_time = utime;
    info.kernel_time = stime;
    info.priority = static_cast<int>(priority);
    info.thread_count = static_cast<int>(num_threads);
    info.start_time_ticks = starttime;

    // Calculate start time
    auto& sys = SystemInfo::instance();
    uint64_t boot_time = sys.get_boot_time_ticks();
    long ticks = sys.get_clock_ticks_per_second();
    uint64_t start_seconds = boot_time + (starttime / ticks);
    info.start_time = std::chrono::system_clock::from_time_t(static_cast<time_t>(start_seconds));

    // Read statm for memory info
    std::string statm = read_file(proc_path + "/statm");
    if (!statm.empty()) {
        std::istringstream statm_iss(statm);
        uint64_t size, resident;
        statm_iss >> size >> resident;
        long page_size = sysconf(_SC_PAGESIZE);
        info.virtual_memory = static_cast<int64_t>(size * page_size);
        info.resident_memory = static_cast<int64_t>(resident * page_size);

        auto mem_info = sys.get_memory_info();
        if (mem_info.total > 0) {
            info.memory_percent = static_cast<double>(info.resident_memory) / mem_info.total * 100.0;
        }
    }

    // Read cmdline
    std::string cmdline = read_file(proc_path + "/cmdline");
    std::replace(cmdline.begin(), cmdline.end(), '\0', ' ');
    if (!cmdline.empty() && cmdline.back() == ' ') {
        cmdline.pop_back();
    }
    info.command_line = cmdline;

    // Read exe symlink
    info.executable_path = read_symlink(proc_path + "/exe");

    // Get user from status file
    std::string status = read_file(proc_path + "/status");
    std::istringstream status_iss(status);
    std::string line;
    while (std::getline(status_iss, line)) {
        if (line.starts_with("Uid:")) {
            std::istringstream uid_iss(line);
            std::string key;
            int uid;
            uid_iss >> key >> uid;
            info.user_name = get_username(uid);
            break;
        }
    }

    return info;
}

std::vector<ThreadInfo> ProcfsReader::get_threads(int pid) {
    std::vector<ThreadInfo> threads;
    std::string task_path = "/proc/" + std::to_string(pid) + "/task";

    try {
        for (const auto& entry : fs::directory_iterator(task_path)) {
            if (!entry.is_directory()) continue;

            const auto& name = entry.path().filename().string();
            int tid = 0;
            auto [ptr, ec] = std::from_chars(name.data(), name.data() + name.size(), tid);
            if (ec != std::errc{}) continue;

            ThreadInfo thread;
            thread.tid = tid;

            // Read thread stat
            std::string stat = read_file(entry.path().string() + "/stat");
            if (!stat.empty()) {
                size_t comm_start = stat.find('(');
                size_t comm_end = stat.rfind(')');
                if (comm_start != std::string::npos && comm_end != std::string::npos) {
                    thread.name = stat.substr(comm_start + 1, comm_end - comm_start - 1);

                    std::istringstream iss(stat.substr(comm_end + 2));
                    std::string state;
                    int ppid, pgrp, session, tty_nr, tpgid;
                    unsigned int flags;
                    uint64_t minflt, cminflt, majflt, cmajflt, utime, stime;
                    int64_t cutime, cstime, priority, nice, num_threads, itrealvalue, starttime;
                    uint64_t vsize, rss;
                    uint64_t dummy[10];
                    int processor;

                    iss >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags
                        >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime
                        >> cutime >> cstime >> priority >> nice >> num_threads >> itrealvalue >> starttime
                        >> vsize >> rss;
                    // Skip to processor field (field 39, 0-indexed 38)
                    for (int i = 0; i < 15; i++) iss >> dummy[i];
                    iss >> processor;

                    thread.state = state.empty() ? '?' : state[0];
                    thread.priority = static_cast<int>(priority);
                    thread.processor = processor;
                }
            }

            // Read thread stack
            std::string stack = read_file(entry.path().string() + "/stack");
            thread.stack = stack;

            threads.push_back(std::move(thread));
        }
    } catch (...) {
        // Permission denied or process gone
    }

    return threads;
}

std::vector<FileHandleInfo> ProcfsReader::get_file_handles(int pid) {
    std::vector<FileHandleInfo> handles;
    std::string fd_path = "/proc/" + std::to_string(pid) + "/fd";

    try {
        for (const auto& entry : fs::directory_iterator(fd_path)) {
            FileHandleInfo handle;

            const auto& name = entry.path().filename().string();
            auto [ptr, ec] = std::from_chars(name.data(), name.data() + name.size(), handle.fd);
            if (ec != std::errc{}) continue;

            handle.path = read_symlink(entry.path().string());

            // Determine type
            if (handle.path.starts_with("socket:")) {
                handle.type = "socket";
            } else if (handle.path.starts_with("pipe:")) {
                handle.type = "pipe";
            } else if (handle.path.starts_with("anon_inode:")) {
                handle.type = "anon_inode";
            } else if (handle.path.starts_with("/")) {
                struct stat st;
                if (stat(handle.path.c_str(), &st) == 0) {
                    if (S_ISREG(st.st_mode)) handle.type = "file";
                    else if (S_ISDIR(st.st_mode)) handle.type = "dir";
                    else if (S_ISCHR(st.st_mode)) handle.type = "char";
                    else if (S_ISBLK(st.st_mode)) handle.type = "block";
                    else if (S_ISFIFO(st.st_mode)) handle.type = "fifo";
                    else if (S_ISSOCK(st.st_mode)) handle.type = "socket";
                    else handle.type = "unknown";
                } else {
                    handle.type = "file";
                }
            } else {
                handle.type = "unknown";
            }

            handles.push_back(std::move(handle));
        }
    } catch (...) {
        // Permission denied
    }

    std::sort(handles.begin(), handles.end(), [](const auto& a, const auto& b) {
        return a.fd < b.fd;
    });

    return handles;
}

std::map<int, NetworkConnectionInfo> ProcfsReader::parse_net_file(const std::string& path, const std::string& protocol) {
    std::map<int, NetworkConnectionInfo> connections;

    std::ifstream file(path);
    if (!file) return connections;

    std::string line;
    std::getline(file, line); // Skip header

    auto parse_address = [](const std::string& hex_addr, bool is_ipv6) -> std::string {
        size_t colon_pos = hex_addr.find(':');
        if (colon_pos == std::string::npos) return hex_addr;

        std::string ip_hex = hex_addr.substr(0, colon_pos);
        std::string port_hex = hex_addr.substr(colon_pos + 1);

        unsigned int port = 0;
        std::from_chars(port_hex.data(), port_hex.data() + port_hex.size(), port, 16);

        if (is_ipv6) {
            // IPv6 handling (simplified)
            return "[::]:" + std::to_string(port);
        } else {
            // IPv4
            unsigned int ip = 0;
            std::from_chars(ip_hex.data(), ip_hex.data() + ip_hex.size(), ip, 16);
            unsigned char* bytes = reinterpret_cast<unsigned char*>(&ip);
            return std::format("{}.{}.{}.{}:{}", bytes[0], bytes[1], bytes[2], bytes[3], port);
        }
    };

    static const char* tcp_states[] = {
        "", "ESTABLISHED", "SYN_SENT", "SYN_RECV", "FIN_WAIT1", "FIN_WAIT2",
        "TIME_WAIT", "CLOSE", "CLOSE_WAIT", "LAST_ACK", "LISTEN", "CLOSING"
    };

    bool is_ipv6 = protocol.find('6') != std::string::npos;

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string sl_str;  // "0:" format
        std::string local_addr, remote_addr;
        std::string state_hex;
        std::string tx_rx, tr_tm, retrnsmt;
        int uid = 0;
        int timeout = 0;
        int inode = 0;

        iss >> sl_str >> local_addr >> remote_addr >> state_hex
            >> tx_rx >> tr_tm >> retrnsmt >> uid >> timeout >> inode;

        if (inode == 0) continue;  // Skip invalid entries

        int state = 0;
        std::from_chars(state_hex.data(), state_hex.data() + state_hex.size(), state, 16);

        NetworkConnectionInfo conn;
        conn.protocol = protocol;
        conn.local_endpoint = parse_address(local_addr, is_ipv6);
        conn.remote_endpoint = parse_address(remote_addr, is_ipv6);
        conn.inode = inode;

        if (protocol.starts_with("tcp")) {
            conn.state = (state < 12) ? tcp_states[state] : "UNKNOWN";
        } else {
            conn.state = "-";
        }

        connections[inode] = std::move(conn);
    }

    return connections;
}

std::vector<NetworkConnectionInfo> ProcfsReader::get_network_connections(int pid) {
    std::vector<NetworkConnectionInfo> result;

    // Get all socket inodes for this process
    std::set<int> socket_inodes;
    std::string fd_path = "/proc/" + std::to_string(pid) + "/fd";

    try {
        for (const auto& entry : fs::directory_iterator(fd_path)) {
            std::string link = read_symlink(entry.path().string());
            if (link.starts_with("socket:[")) {
                int inode = 0;
                auto start = link.data() + 8;
                auto end = link.data() + link.size() - 1;
                std::from_chars(start, end, inode);
                if (inode > 0) socket_inodes.insert(inode);
            }
        }
    } catch (...) {
        return result;
    }

    if (socket_inodes.empty()) return result;

    // Parse network files
    auto tcp = parse_net_file("/proc/net/tcp", "tcp");
    auto tcp6 = parse_net_file("/proc/net/tcp6", "tcp6");
    auto udp = parse_net_file("/proc/net/udp", "udp");
    auto udp6 = parse_net_file("/proc/net/udp6", "udp6");

    for (int inode : socket_inodes) {
        if (auto it = tcp.find(inode); it != tcp.end()) {
            result.push_back(it->second);
        } else if (auto it = tcp6.find(inode); it != tcp6.end()) {
            result.push_back(it->second);
        } else if (auto it = udp.find(inode); it != udp.end()) {
            result.push_back(it->second);
        } else if (auto it = udp6.find(inode); it != udp6.end()) {
            result.push_back(it->second);
        }
    }

    return result;
}

std::vector<MemoryMapInfo> ProcfsReader::get_memory_maps(int pid) {
    std::vector<MemoryMapInfo> maps;
    std::string maps_path = "/proc/" + std::to_string(pid) + "/maps";

    std::ifstream file(maps_path);
    if (!file) return maps;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string address, perms, offset, dev, inode;
        std::string pathname;

        iss >> address >> perms >> offset >> dev >> inode;
        std::getline(iss >> std::ws, pathname);

        // Parse address range to calculate size
        size_t dash = address.find('-');
        uint64_t start_addr = 0, end_addr = 0;
        if (dash != std::string::npos) {
            std::from_chars(address.data(), address.data() + dash, start_addr, 16);
            std::from_chars(address.data() + dash + 1, address.data() + address.size(), end_addr, 16);
        }
        uint64_t size = end_addr - start_addr;

        MemoryMapInfo map;
        map.address = address;
        map.permissions = perms;
        map.pathname = pathname;

        // Format size
        if (size < 1024) {
            map.size = std::to_string(size) + " B";
        } else if (size < 1024 * 1024) {
            map.size = std::format("{:.1f} KB", size / 1024.0);
        } else if (size < 1024 * 1024 * 1024) {
            map.size = std::format("{:.1f} MB", size / (1024.0 * 1024));
        } else {
            map.size = std::format("{:.2f} GB", size / (1024.0 * 1024 * 1024));
        }

        maps.push_back(std::move(map));
    }

    return maps;
}

std::vector<EnvironmentVariable> ProcfsReader::get_environment_variables(int pid) {
    std::vector<EnvironmentVariable> vars;
    std::string env_path = "/proc/" + std::to_string(pid) + "/environ";

    std::string content = read_file(env_path);
    if (content.empty()) return vars;

    size_t start = 0;
    while (start < content.size()) {
        size_t end = content.find('\0', start);
        if (end == std::string::npos) end = content.size();

        std::string entry = content.substr(start, end - start);
        size_t eq = entry.find('=');
        if (eq != std::string::npos) {
            EnvironmentVariable var;
            var.name = entry.substr(0, eq);
            var.value = entry.substr(eq + 1);
            vars.push_back(std::move(var));
        }

        start = end + 1;
    }

    std::sort(vars.begin(), vars.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    return vars;
}

} // namespace pex
