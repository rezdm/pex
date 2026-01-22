#include "procfs_reader.hpp"
#include "system_info.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <algorithm>
#include <format>
#include <set>
#include <cerrno>
#include <cstring>
#include <arpa/inet.h>
#include <netinet/in.h>

namespace fs = std::filesystem;

namespace pex {

// Error tracking methods
void ProcfsReader::add_error(const std::string& message) {
    std::lock_guard lock(errors_mutex_);
    recent_errors_.push_back({std::chrono::steady_clock::now(), message});
    if (recent_errors_.size() > kMaxErrors) {
        recent_errors_.erase(recent_errors_.begin());
    }
}

std::vector<ParseError> ProcfsReader::get_recent_errors() {
    std::lock_guard lock(errors_mutex_);
    // Return errors from the last 10 seconds
    auto cutoff = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    std::vector<ParseError> result;
    for (const auto& err : recent_errors_) {
        if (err.timestamp > cutoff) {
            result.push_back(err);
        }
    }
    return result;
}

void ProcfsReader::clear_errors() {
    std::lock_guard lock(errors_mutex_);
    recent_errors_.clear();
}

std::string ProcfsReader::read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) return {};
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string ProcfsReader::read_symlink(const std::string& path) {
    char buf[4096];
    const ssize_t len = readlink(path.c_str(), buf, sizeof(buf) - 1);
    if (len == -1) return {};
    buf[len] = '\0';
    return buf;
}

std::string ProcfsReader::get_username(const int uid) {
    if (const auto it = uid_cache_.find(uid); it != uid_cache_.end()) {
        return it->second;
    }

    const passwd* pw = getpwuid(uid);
    std::string name = pw ? pw->pw_name : std::to_string(uid);
    uid_cache_[uid] = name;
    return name;
}

std::vector<ProcessInfo> ProcfsReader::get_all_processes() {
    std::vector<ProcessInfo> processes;

    // Fetch memory info once for the entire snapshot
    const auto mem_info = SystemInfo::get_memory_info();
    const int64_t total_memory = mem_info.total;

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
            } catch (...) {
                // Process disappeared mid-read, skip it
                continue;
            }
        }
    } catch (const std::exception& e) {
        add_error(std::format("Failed to iterate /proc: {}", e.what()));
    }

    return processes;
}

std::optional<ProcessInfo> ProcfsReader::get_process_info(int pid) {
    // Convenience overload that fetches memory info (use get_all_processes for bulk)
    const auto mem_info = SystemInfo::get_memory_info();
    return get_process_info(pid, mem_info.total);
}

std::optional<ProcessInfo> ProcfsReader::get_process_info(int pid, int64_t total_memory) {
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
    if (comm_start == std::string::npos || comm_end == std::string::npos || comm_end <= comm_start) {
        add_error(std::format("PID {}: malformed stat (missing comm)", pid));
        return std::nullopt;
    }

    info.name = stat_content.substr(comm_start + 1, comm_end - comm_start - 1);

    // Check if there's content after comm
    if (comm_end + 2 >= stat_content.size()) {
        add_error(std::format("PID {}: truncated stat (no fields after comm)", pid));
        return std::nullopt;
    }

    // Parse fields after comm
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

    // Check if we got at least the essential fields
    if (iss.fail() && state.empty()) {
        add_error(std::format("PID {}: failed to parse stat fields", pid));
        return std::nullopt;
    }

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
    if (ticks > 0) {
        uint64_t start_seconds = boot_time + (starttime / ticks);
        info.start_time = std::chrono::system_clock::from_time_t(static_cast<time_t>(start_seconds));
    }

    // Read statm for memory info
    if (std::string statm = read_file(proc_path + "/statm"); !statm.empty()) {
        std::istringstream statm_iss(statm);
        uint64_t size = 0, resident = 0;
        statm_iss >> size >> resident;
        if (!statm_iss.fail()) {
            long page_size = sysconf(_SC_PAGESIZE);
            info.virtual_memory = static_cast<int64_t>(size * page_size);
            info.resident_memory = static_cast<int64_t>(resident * page_size);

            if (total_memory > 0) {
                info.memory_percent = static_cast<double>(info.resident_memory) / static_cast<double>(total_memory) * 100.0;
            }
        }
    }

    // Read cmdline
    std::string cmdline = read_file(proc_path + "/cmdline");
    std::ranges::replace(cmdline, '\0', ' ');
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
            int uid = 0;
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
    std::string proc_path = "/proc/" + std::to_string(pid);

    // Build address range to library mapping from /proc/<pid>/maps
    struct AddressRange {
        uint64_t start;
        uint64_t end;
        std::string library;
    };
    std::vector<AddressRange> address_map;

    {
        std::ifstream maps_file(proc_path + "/maps");
        std::string line;
        while (std::getline(maps_file, line)) {
            std::istringstream iss(line);
            std::string address, perms, offset, dev, inode_str, pathname;
            iss >> address >> perms >> offset >> dev >> inode_str;
            std::getline(iss >> std::ws, pathname);

            // Skip deleted mappings
            if (pathname.find("(deleted)") != std::string::npos) continue;

            // Only include executable mappings with a pathname
            if (perms.size() >= 3 && perms[2] == 'x' && !pathname.empty() && pathname[0] == '/') {
                size_t dash = address.find('-');
                if (dash != std::string::npos) {
                    uint64_t start_addr = 0, end_addr = 0;
                    auto [ptr1, ec1] = std::from_chars(address.data(), address.data() + dash, start_addr, 16);
                    auto [ptr2, ec2] = std::from_chars(address.data() + dash + 1, address.data() + address.size(), end_addr, 16);

                    if (ec1 == std::errc{} && ec2 == std::errc{} && start_addr < end_addr) {
                        // Extract just the library name
                        std::string lib_name = pathname;
                        if (size_t pos = pathname.rfind('/'); pos != std::string::npos) {
                            lib_name = pathname.substr(pos + 1);
                        }
                        address_map.push_back({start_addr, end_addr, lib_name});
                    }
                }
            }
        }
    }

    // Helper to find library for an address
    auto find_library = [&](uint64_t addr) -> std::string {
        for (const auto& range : address_map) {
            if (addr >= range.start && addr < range.end) {
                return range.library;
            }
        }
        return {};
    };

    try {
        for (const auto& entry : fs::directory_iterator(task_path)) {
            try {
                if (!entry.is_directory()) continue;

                const auto& name = entry.path().filename().string();
                int tid = 0;
                if (auto [ptr, ec] = std::from_chars(name.data(), name.data() + name.size(), tid); ec != std::errc{}) continue;

                ThreadInfo thread;
                thread.tid = tid;

                // Read thread stat
                if (std::string stat = read_file(entry.path().string() + "/stat"); !stat.empty()) {
                    size_t comm_start = stat.find('(');
                    size_t comm_end = stat.rfind(')');

                    if (comm_start != std::string::npos && comm_end != std::string::npos && comm_end > comm_start) {
                        thread.name = stat.substr(comm_start + 1, comm_end - comm_start - 1);

                        // Check for content after comm
                        if (comm_end + 2 < stat.size()) {
                            std::istringstream iss(stat.substr(comm_end + 2));
                            std::string state;
                            int ppid = 0, pgrp = 0, session = 0, tty_nr = 0, tpgid = 0;
                            unsigned int flags = 0;
                            uint64_t minflt = 0, cminflt = 0, majflt = 0, cmajflt = 0, utime = 0, stime = 0;
                            int64_t cutime = 0, cstime = 0, priority = 0, nice = 0, num_threads = 0, itrealvalue = 0, starttime = 0;
                            uint64_t vsize = 0, rss = 0;
                            uint64_t dummy[15] = {};
                            int processor = 0;

                            iss >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags
                                >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime
                                >> cutime >> cstime >> priority >> nice >> num_threads >> itrealvalue >> starttime
                                >> vsize >> rss;

                            // Skip to processor field (field 39, 0-indexed 38)
                            for (auto& d : dummy) iss >> d;
                            iss >> processor;

                            thread.state = state.empty() ? '?' : state[0];
                            thread.priority = static_cast<int>(priority);
                            thread.processor = iss.fail() ? -1 : processor;
                        } else {
                            thread.state = '?';
                            thread.priority = 0;
                            thread.processor = -1;
                        }
                    } else {
                        // Malformed stat - use defaults
                        thread.name = "???";
                        thread.state = '?';
                        thread.priority = 0;
                        thread.processor = -1;
                    }
                }

                // Stack is read on-demand when thread is selected (see get_thread_stack)

                // Read instruction pointer from syscall file to determine current library
                // Format: syscall_num arg1 arg2 arg3 arg4 arg5 arg6 sp pc
                // Or: "running" if in user space
                if (std::string syscall = read_file(entry.path().string() + "/syscall"); !syscall.empty()) {
                    std::istringstream iss(syscall);
                    std::string syscall_num;
                    iss >> syscall_num;

                    if (syscall_num != "running" && !syscall_num.empty()) {
                        // Need to read 6 args, then sp, then pc
                        std::string hex;
                        int field_count = 0;
                        uint64_t pc = 0;

                        // Skip 6 args + sp (7 fields)
                        for (int i = 0; i < 7 && iss >> hex; i++) {
                            field_count++;
                        }

                        // Read pc
                        if (field_count == 7 && iss >> hex) {
                            if (hex.starts_with("0x") && hex.size() > 2) {
                                auto [ptr, ec] = std::from_chars(hex.data() + 2, hex.data() + hex.size(), pc, 16);
                                if (ec == std::errc{} && pc > 0) {
                                    thread.current_library = find_library(pc);
                                }
                            }
                        }
                    }
                }

                threads.push_back(std::move(thread));
            } catch (...) {
                // Thread disappeared mid-read, skip it
                continue;
            }
        }
    } catch (...) {
        // Directory iteration failed (process gone or permission denied)
    }

    return threads;
}

std::string ProcfsReader::get_thread_stack(int pid, int tid) {
    std::string path = "/proc/" + std::to_string(pid) + "/task/" + std::to_string(tid) + "/stack";

    // Use low-level I/O for procfs files - they need direct read() calls
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        if (errno == EACCES || errno == EPERM) {
            return "(requires root to read kernel stack)";
        }
        return std::format("(cannot read stack: {})", strerror(errno));
    }

    std::string result;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        result.append(buf, n);
    }
    close(fd);

    if (result.empty()) {
        return "(kernel stack requires root privileges)";
    }
    return result;
}

std::vector<FileHandleInfo> ProcfsReader::get_file_handles(const int pid) {
    std::vector<FileHandleInfo> handles;
    const std::string fd_path = "/proc/" + std::to_string(pid) + "/fd";

    try {
        for (const auto& entry : fs::directory_iterator(fd_path)) {
            try {
                FileHandleInfo handle;

                const auto& name = entry.path().filename().string();
                if (auto [ptr, ec] = std::from_chars(name.data(), name.data() + name.size(), handle.fd); ec != std::errc{}) continue;

                handle.path = read_symlink(entry.path().string());

                // Determine type
                if (handle.path.starts_with("socket:")) {
                    handle.type = "socket";
                } else if (handle.path.starts_with("pipe:")) {
                    handle.type = "pipe";
                } else if (handle.path.starts_with("anon_inode:")) {
                    handle.type = "anon_inode";
                } else if (handle.path.starts_with("/")) {
                    struct stat st{};
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
            } catch (...) {
                // FD disappeared mid-read, skip it
                continue;
            }
        }
    } catch (...) {
        // Directory iteration failed (process gone or permission denied)
    }

    std::ranges::sort(handles, [](const auto& a, const auto& b) {
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

    auto parse_address = [](const std::string& hex_addr, const bool is_ipv6) -> std::string {
        const size_t colon_pos = hex_addr.find(':');
        if (colon_pos == std::string::npos) return hex_addr;

        const std::string ip_hex = hex_addr.substr(0, colon_pos);
        const std::string port_hex = hex_addr.substr(colon_pos + 1);

        unsigned int port = 0;
        std::from_chars(port_hex.data(), port_hex.data() + port_hex.size(), port, 16);

        if (is_ipv6) {
            // IPv6: 32 hex chars = 128 bits, stored as 4 little-endian 32-bit words
            if (ip_hex.size() != 32) {
                return "[::]:" + std::to_string(port);
            }

            // Parse 4 32-bit words and convert from host (little-endian) to network byte order
            struct in6_addr addr{};
            for (int i = 0; i < 4; i++) {
                uint32_t word = 0;
                std::from_chars(ip_hex.data() + i * 8, ip_hex.data() + i * 8 + 8, word, 16);
                // Each word in /proc is in host byte order, need to swap for in6_addr
                addr.s6_addr32[i] = htonl(word);
            }

            char buf[INET6_ADDRSTRLEN];
            if (inet_ntop(AF_INET6, &addr, buf, sizeof(buf))) {
                return std::format("[{}]:{}", buf, port);
            }
            return "[::]:" + std::to_string(port);
        } else {
            // IPv4
            unsigned int ip = 0;
            std::from_chars(ip_hex.data(), ip_hex.data() + ip_hex.size(), ip, 16);
            const auto bytes = reinterpret_cast<unsigned char*>(&ip);
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

std::vector<NetworkConnectionInfo> ProcfsReader::get_network_connections(const int pid) {
    std::vector<NetworkConnectionInfo> result;

    // Get all socket inodes for this process
    std::set<int> socket_inodes;
    const std::string fd_path = "/proc/" + std::to_string(pid) + "/fd";

    try {
        for (const auto& entry : fs::directory_iterator(fd_path)) {
            try {
                std::string link = read_symlink(entry.path().string());
                if (link.starts_with("socket:[")) {
                    int inode = 0;
                    const auto start = link.data() + 8;
                    const auto end = link.data() + link.size() - 1;
                    std::from_chars(start, end, inode);
                    if (inode > 0) socket_inodes.insert(inode);
                }
            } catch (...) {
                // FD disappeared, skip it
                continue;
            }
        }
    } catch (...) {
        // Directory iteration failed
        return result;
    }

    if (socket_inodes.empty()) return result;

    // Parse network files
    auto tcp = parse_net_file("/proc/net/tcp", "tcp");
    auto tcp6 = parse_net_file("/proc/net/tcp6", "tcp6");
    auto udp = parse_net_file("/proc/net/udp", "udp");
    auto udp6 = parse_net_file("/proc/net/udp6", "udp6");

    for (int inode : socket_inodes) {
        if (auto itTcp4 = tcp.find(inode); itTcp4 != tcp.end()) {
            result.push_back(itTcp4->second);
        } else if (auto itTcp6 = tcp6.find(inode); itTcp6 != tcp6.end()) {
            result.push_back(itTcp6->second);
        } else if (auto itUdp4 = udp.find(inode); itUdp4 != udp.end()) {
            result.push_back(itUdp4->second);
        } else if (auto itUdp6 = udp6.find(inode); itUdp6 != udp6.end()) {
            result.push_back(itUdp6->second);
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

        // Skip deleted mappings
        if (pathname.find("(deleted)") != std::string::npos) continue;

        // Parse address range to calculate size
        size_t dash = address.find('-');
        uint64_t start_addr = 0, end_addr = 0;
        if (dash != std::string::npos) {
            auto [ptr1, ec1] = std::from_chars(address.data(), address.data() + dash, start_addr, 16);
            auto [ptr2, ec2] = std::from_chars(address.data() + dash + 1, address.data() + address.size(), end_addr, 16);
            if (ec1 != std::errc{} || ec2 != std::errc{}) {
                add_error(std::format("PID {}: malformed address in maps: {}", pid, address));
                continue;
            }
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

std::vector<EnvironmentVariable> ProcfsReader::get_environment_variables(const int pid) {
    std::vector<EnvironmentVariable> vars;
    const std::string env_path = "/proc/" + std::to_string(pid) + "/environ";

    std::string content = read_file(env_path);
    if (content.empty()) return vars;

    size_t start = 0;
    while (start < content.size()) {
        size_t end = content.find('\0', start);
        if (end == std::string::npos) end = content.size();

        std::string entry = content.substr(start, end - start);
        if (const size_t eq = entry.find('='); eq != std::string::npos) {
            EnvironmentVariable var;
            var.name = entry.substr(0, eq);
            var.value = entry.substr(eq + 1);
            vars.push_back(std::move(var));
        }

        start = end + 1;
    }

    std::ranges::sort(vars, [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    return vars;
}

std::vector<LibraryInfo> ProcfsReader::get_libraries(const int pid) {
    std::vector<LibraryInfo> libraries;
    const std::string maps_path = "/proc/" + std::to_string(pid) + "/maps";
    const std::string exe_path = read_symlink("/proc/" + std::to_string(pid) + "/exe");

    std::ifstream file(maps_path);
    if (!file) return libraries;

    // Use a map to aggregate multiple mappings of the same library
    std::map<std::string, LibraryInfo> lib_map;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string address, perms, offset, dev, inode_str;
        std::string pathname;

        iss >> address >> perms >> offset >> dev >> inode_str;
        std::getline(iss >> std::ws, pathname);

        // Skip anonymous mappings and special entries
        if (pathname.empty() || pathname[0] != '/') continue;
        if (pathname.starts_with("/dev/") || pathname.starts_with("/memfd:")) continue;
        if (pathname.find("(deleted)") != std::string::npos) continue;

        // Check if it's a library (.so) or executable
        bool is_library = pathname.find(".so") != std::string::npos;
        bool is_main_exe = (pathname == exe_path);

        if (!is_library && !is_main_exe) continue;

        // Parse address range to calculate size
        size_t dash = address.find('-');
        uint64_t start_addr = 0, end_addr = 0;
        if (dash != std::string::npos) {
            auto [ptr1, ec1] = std::from_chars(address.data(), address.data() + dash, start_addr, 16);
            auto [ptr2, ec2] = std::from_chars(address.data() + dash + 1, address.data() + address.size(), end_addr, 16);
            if (ec1 != std::errc{} || ec2 != std::errc{}) {
                continue;  // Skip malformed entries silently for libraries
            }
        }
        uint64_t size = end_addr - start_addr;

        // Add or update library entry
        auto& lib = lib_map[pathname];
        if (lib.path.empty()) {
            lib.path = pathname;
            // Extract filename
            if (size_t pos = pathname.rfind('/'); pos != std::string::npos) {
                lib.name = pathname.substr(pos + 1);
            } else {
                lib.name = pathname;
            }
            lib.base_address = std::format("{:x}", start_addr);
            lib.is_executable = is_main_exe;
        }
        lib.total_size += static_cast<int64_t>(size);
    }

    // Convert map to vector
    libraries.reserve(lib_map.size());
    for (auto& [path, lib] : lib_map) {
        libraries.push_back(std::move(lib));
    }

    // Sort: executable first, then by name
    std::ranges::sort(libraries, [](const auto& a, const auto& b) {
        if (a.is_executable != b.is_executable) return a.is_executable > b.is_executable;
        return a.name < b.name;
    });

    return libraries;
}

} // namespace pex
