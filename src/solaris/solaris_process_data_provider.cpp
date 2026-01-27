#include "solaris_process_data_provider.hpp"

#include <sys/types.h>
#include <sys/stat.h>
#include <procfs.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pwd.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <format>
#include <algorithm>
#include <map>
#include <filesystem>
#include <cstdio>
#include <cctype>

namespace fs = std::filesystem;

namespace pex {

struct PfilesParseResult {
    std::vector<FileHandleInfo> handles;
    std::vector<NetworkConnectionInfo> connections;
};

static std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.pop_back();
    }
    return value;
}

static int parse_port_from_line(const std::string& line) {
    auto pos = line.find("port");
    if (pos == std::string::npos) {
        return 0;
    }
    pos = line.find_first_of("0123456789", pos);
    if (pos == std::string::npos) {
        return 0;
    }
    size_t end = pos;
    while (end < line.size() && std::isdigit(static_cast<unsigned char>(line[end])) != 0) {
        ++end;
    }
    return std::stoi(line.substr(pos, end - pos));
}

static bool parse_socket_endpoint(const std::string& line, std::string& endpoint, int& family) {
    std::string rest = trim(line);
    std::istringstream iss(rest);
    std::string fam_token;
    if (!(iss >> fam_token)) {
        return false;
    }
    if (fam_token == "AF_UNSPEC") {
        return false;
    }
    if (fam_token == "AF_INET6") {
        family = AF_INET6;
    } else if (fam_token == "AF_INET") {
        family = AF_INET;
    } else {
        return false;
    }

    std::string addr;
    if (!(iss >> addr)) {
        addr = "*";
    }

    int port = parse_port_from_line(rest);
    if (family == AF_INET6) {
        endpoint = std::format("[{}]:{}", addr, port);
    } else {
        endpoint = std::format("{}:{}", addr, port);
    }
    return true;
}

static PfilesParseResult parse_pfiles(int pid) {
    PfilesParseResult result;

    std::string cmd = std::format("pfiles -F {} 2>/dev/null", pid);
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return result;
    }

    struct SocketState {
        bool active = false;
        bool is_stream = false;
        bool is_dgram = false;
        int family = 0;
        std::string local;
        std::string remote;
        std::string state;
    } socket_state;

    size_t current_index = std::string::npos;

    auto flush_socket = [&]() {
        if (!socket_state.active || socket_state.local.empty()) {
            socket_state = {};
            return;
        }

        NetworkConnectionInfo conn;
        if (socket_state.family == AF_INET6) {
            conn.protocol = socket_state.is_dgram ? "udp6" : "tcp6";
        } else if (socket_state.family == AF_INET) {
            conn.protocol = socket_state.is_dgram ? "udp" : "tcp";
        } else {
            conn.protocol = socket_state.is_dgram ? "udp" : "tcp";
        }
        conn.local_endpoint = socket_state.local;
        conn.remote_endpoint = socket_state.remote.empty() ? "*:*" : socket_state.remote;
        conn.state = socket_state.state.empty() ? "-" : socket_state.state;
        result.connections.push_back(std::move(conn));
        socket_state = {};
    };

    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        std::string line = trim(buffer);
        if (line.empty()) {
            continue;
        }

        size_t pos = 0;
        while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])) != 0) {
            ++pos;
        }
        if (pos > 0 && pos < line.size() && line[pos] == ':') {
            flush_socket();

            int fd_num = std::stoi(line.substr(0, pos));
            std::string rest = trim(line.substr(pos + 1));

            FileHandleInfo fh;
            fh.fd = fd_num;

            if (rest.find("S_IFSOCK") != std::string::npos || rest.find("SOCK") != std::string::npos) {
                fh.type = "socket";
                socket_state.active = true;
                if (rest.find("SOCK_STREAM") != std::string::npos) {
                    socket_state.is_stream = true;
                } else if (rest.find("SOCK_DGRAM") != std::string::npos) {
                    socket_state.is_dgram = true;
                }
            } else if (rest.find("S_IFREG") != std::string::npos) {
                fh.type = "file";
            } else if (rest.find("S_IFDIR") != std::string::npos) {
                fh.type = "dir";
            } else if (rest.find("S_IFCHR") != std::string::npos) {
                fh.type = "char";
            } else if (rest.find("S_IFBLK") != std::string::npos) {
                fh.type = "block";
            } else if (rest.find("S_IFIFO") != std::string::npos || rest.find("FIFO") != std::string::npos) {
                fh.type = "fifo";
            } else if (rest.find("DOOR") != std::string::npos) {
                fh.type = "door";
            } else {
                fh.type = "unknown";
            }

            auto slash = rest.rfind('/');
            if (slash != std::string::npos) {
                auto start = rest.rfind(' ', slash);
                if (start == std::string::npos) {
                    start = 0;
                } else {
                    ++start;
                }
                fh.path = trim(rest.substr(start));
            }

            result.handles.push_back(std::move(fh));
            current_index = result.handles.size() - 1;
            continue;
        }

        if (current_index == std::string::npos) {
            continue;
        }

        if (line.rfind("path:", 0) == 0 || line.rfind("vnode:", 0) == 0) {
            auto path = trim(line.substr(line.find(':') + 1));
            if (!path.empty() && result.handles[current_index].path.empty()) {
                result.handles[current_index].path = std::move(path);
            }
            continue;
        }

        if (socket_state.active) {
            if (line.rfind("sockname:", 0) == 0) {
                std::string endpoint;
                int family = 0;
                if (parse_socket_endpoint(line.substr(9), endpoint, family)) {
                    socket_state.local = std::move(endpoint);
                    socket_state.family = family;
                }
            } else if (line.rfind("peername:", 0) == 0) {
                std::string endpoint;
                int family = 0;
                if (parse_socket_endpoint(line.substr(9), endpoint, family)) {
                    socket_state.remote = std::move(endpoint);
                    socket_state.family = family;
                }
            } else if (line.rfind("state:", 0) == 0) {
                socket_state.state = trim(line.substr(6));
            }
        }
    }

    flush_socket();
    pclose(pipe);
    return result;
}

static int open_fd_dup(const std::string& path) {
    const int flags[] = {O_RDONLY | O_NONBLOCK, O_WRONLY | O_NONBLOCK, O_RDWR | O_NONBLOCK};
    for (const int flag : flags) {
        int fd = open(path.c_str(), flag);
        if (fd >= 0) {
            return fd;
        }
    }
    return -1;
}

SolarisProcessDataProvider::SolarisProcessDataProvider() = default;
SolarisProcessDataProvider::~SolarisProcessDataProvider() = default;

void SolarisProcessDataProvider::add_error(const std::string& context, const std::string& message) {
    std::lock_guard lock(errors_mutex_);
    recent_errors_.push_back({std::chrono::steady_clock::now(), context + ": " + message});
    if (recent_errors_.size() > 100) {
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

    // Lookup and cache
    std::string name;
    struct passwd* pw = getpwuid(uid);
    if (pw) {
        name = pw->pw_name;
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
    info.user_name = get_username(psinfo.pr_uid);
    info.priority = psinfo.pr_lwp.pr_nice;
    info.thread_count = psinfo.pr_nlwp;

    // Memory info
    info.resident_memory = psinfo.pr_rssize * 1024;  // rssize is in KB
    info.virtual_memory = psinfo.pr_size * 1024;     // size is in KB
    if (total_memory > 0) {
        info.memory_percent = (static_cast<double>(info.resident_memory) / total_memory) * 100.0;
    }

    // CPU times (in clock ticks)
    long ticks = sysconf(_SC_CLK_TCK);
    info.user_time = psinfo.pr_time.tv_sec * ticks +
                     psinfo.pr_time.tv_nsec * ticks / 1000000000;
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
    } catch (...) {
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
            } catch (...) {
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

std::vector<ThreadInfo> SolarisProcessDataProvider::get_threads(int pid) {
    std::vector<ThreadInfo> threads;
    std::string lwp_path = "/proc/" + std::to_string(pid) + "/lwp";
    std::string process_name;

    try {
        std::string psinfo_path = "/proc/" + std::to_string(pid) + "/psinfo";
        int ps_fd = open(psinfo_path.c_str(), O_RDONLY);
        if (ps_fd >= 0) {
            psinfo_t psinfo;
            if (read(ps_fd, &psinfo, sizeof(psinfo)) == sizeof(psinfo)) {
                process_name = psinfo.pr_fname;
            }
            close(ps_fd);
        }

        for (const auto& entry : fs::directory_iterator(lwp_path)) {
            if (!entry.is_directory()) continue;

            std::string lwpid_str = entry.path().filename().string();
            int lwpid = 0;
            try {
                lwpid = std::stoi(lwpid_str);
            } catch (...) {
                continue;
            }

            // Read lwpsinfo
            std::string lwpsinfo_path = entry.path().string() + "/lwpsinfo";
            int fd = open(lwpsinfo_path.c_str(), O_RDONLY);
            if (fd < 0) continue;

            lwpsinfo_t lwpinfo;
            ssize_t n = read(fd, &lwpinfo, sizeof(lwpinfo));
            close(fd);

            if (n != sizeof(lwpinfo)) continue;

            ThreadInfo ti;
            ti.tid = lwpinfo.pr_lwpid;
            if (lwpinfo.pr_name[0]) {
                ti.name = lwpinfo.pr_name;
            } else if (!process_name.empty()) {
                ti.name = process_name;
            } else {
                ti.name = std::format("LWP {}", lwpid);
            }
            ti.state = map_state(lwpinfo.pr_sname);
            ti.priority = lwpinfo.pr_nice;
            ti.processor = lwpinfo.pr_onpro;
            threads.push_back(std::move(ti));
        }
    } catch (const std::exception& e) {
        add_error("get_threads", e.what());
    }

    return threads;
}

std::string SolarisProcessDataProvider::get_thread_stack([[maybe_unused]] int pid, [[maybe_unused]] int tid) {
    // Would require pstack or dtrace - return empty for now
    return "";
}

std::vector<FileHandleInfo> SolarisProcessDataProvider::get_file_handles(int pid) {
    std::vector<FileHandleInfo> handles;
    std::string fd_path = "/proc/" + std::to_string(pid) + "/fd";
    std::string path_path = "/proc/" + std::to_string(pid) + "/path";

    try {
        for (const auto& entry : fs::directory_iterator(fd_path)) {
            std::string fd_str = entry.path().filename().string();
            int fd_num = 0;
            try {
                fd_num = std::stoi(fd_str);
            } catch (...) {
                continue;
            }

            FileHandleInfo fh;
            fh.fd = fd_num;

            // Prefer /proc/<pid>/path/<fd> for readable paths
            std::string link_path = path_path + "/" + fd_str;
            try {
                std::string target = fs::read_symlink(link_path).string();
                fh.path = target;
            } catch (...) {
                // Fallback: try /proc/<pid>/fd/<fd>
                try {
                    std::string target = fs::read_symlink(entry.path()).string();
                    fh.path = target;
                } catch (...) {
                    fh.path = "[unknown]";
                }
            }

            // Determine type via fstat on duplicated fd when possible
            int dup_fd = open_fd_dup(entry.path().string());
            if (dup_fd >= 0) {
                struct stat st{};
                if (fstat(dup_fd, &st) == 0) {
                    if (S_ISREG(st.st_mode)) fh.type = "file";
                    else if (S_ISDIR(st.st_mode)) fh.type = "dir";
                    else if (S_ISCHR(st.st_mode)) fh.type = "char";
                    else if (S_ISBLK(st.st_mode)) fh.type = "block";
                    else if (S_ISFIFO(st.st_mode)) fh.type = "fifo";
                    else if (S_ISSOCK(st.st_mode)) fh.type = "socket";
                    else fh.type = "unknown";
                } else {
                    fh.type = "unknown";
                }
                close(dup_fd);
            } else {
                // Heuristic fallback based on path
                if (fh.path.find("socket") != std::string::npos) {
                    fh.type = "socket";
                } else if (fh.path.find("pipe") != std::string::npos) {
                    fh.type = "pipe";
                } else if (fh.path.starts_with("/dev/")) {
                    fh.type = "device";
                } else if (fh.path != "[unknown]") {
                    fh.type = "file";
                } else {
                    fh.type = "unknown";
                }
            }

            handles.push_back(std::move(fh));
        }
    } catch (const std::exception& e) {
        add_error("get_file_handles", e.what());
    }

    bool needs_fallback = handles.empty();
    if (!needs_fallback) {
        needs_fallback = std::all_of(handles.begin(), handles.end(), [](const FileHandleInfo& fh) {
            return fh.path.empty() || fh.path == "[unknown]" || fh.type == "unknown";
        });
    }

    if (needs_fallback) {
        auto parsed = parse_pfiles(pid);
        if (!parsed.handles.empty()) {
            return parsed.handles;
        }
    } else {
        auto parsed = parse_pfiles(pid);
        if (!parsed.handles.empty()) {
            std::map<int, FileHandleInfo> parsed_map;
            for (auto& fh : parsed.handles) {
                parsed_map[fh.fd] = fh;
            }
            for (auto& fh : handles) {
                auto it = parsed_map.find(fh.fd);
                if (it == parsed_map.end()) continue;
                if (fh.path.empty() || fh.path == "[unknown]") {
                    fh.path = it->second.path;
                }
                if (fh.type.empty() || fh.type == "unknown") {
                    fh.type = it->second.type;
                }
            }
        }
    }

    return handles;
}

std::vector<NetworkConnectionInfo> SolarisProcessDataProvider::get_network_connections([[maybe_unused]] int pid) {
    std::vector<NetworkConnectionInfo> connections;
    std::string fd_path = "/proc/" + std::to_string(pid) + "/fd";

    try {
        for (const auto& entry : fs::directory_iterator(fd_path)) {
            std::string fd_str = entry.path().filename().string();
            int fd_num = 0;
            try {
                fd_num = std::stoi(fd_str);
            } catch (...) {
                continue;
            }

            int fd = open(entry.path().string().c_str(), O_RDONLY);
            if (fd < 0) {
                fd = open_fd_dup(entry.path().string());
            }
            if (fd < 0) continue;

            int sock_type = 0;
            socklen_t opt_len = sizeof(sock_type);
            if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &sock_type, &opt_len) != 0) {
                close(fd);
                continue;
            }

            sockaddr_storage local{};
            socklen_t local_len = sizeof(local);
            if (getsockname(fd, reinterpret_cast<sockaddr*>(&local), &local_len) != 0) {
                close(fd);
                continue;
            }

            sockaddr_storage remote{};
            socklen_t remote_len = sizeof(remote);
            bool has_peer = (getpeername(fd, reinterpret_cast<sockaddr*>(&remote), &remote_len) == 0);

            NetworkConnectionInfo conn;

            if (local.ss_family == AF_INET) {
                conn.protocol = (sock_type == SOCK_STREAM) ? "tcp" : "udp";
                auto* sin_local = reinterpret_cast<sockaddr_in*>(&local);
                char local_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &sin_local->sin_addr, local_ip, sizeof(local_ip));
                conn.local_endpoint = std::format("{}:{}", local_ip, ntohs(sin_local->sin_port));

                if (has_peer) {
                    auto* sin_remote = reinterpret_cast<sockaddr_in*>(&remote);
                    char remote_ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &sin_remote->sin_addr, remote_ip, sizeof(remote_ip));
                    conn.remote_endpoint = std::format("{}:{}", remote_ip, ntohs(sin_remote->sin_port));
                } else {
                    conn.remote_endpoint = "*:*";
                }
            } else if (local.ss_family == AF_INET6) {
                conn.protocol = (sock_type == SOCK_STREAM) ? "tcp6" : "udp6";
                auto* sin6_local = reinterpret_cast<sockaddr_in6*>(&local);
                char local_ip[INET6_ADDRSTRLEN];
                inet_ntop(AF_INET6, &sin6_local->sin6_addr, local_ip, sizeof(local_ip));
                conn.local_endpoint = std::format("[{}]:{}", local_ip, ntohs(sin6_local->sin6_port));

                if (has_peer) {
                    auto* sin6_remote = reinterpret_cast<sockaddr_in6*>(&remote);
                    char remote_ip[INET6_ADDRSTRLEN];
                    inet_ntop(AF_INET6, &sin6_remote->sin6_addr, remote_ip, sizeof(remote_ip));
                    conn.remote_endpoint = std::format("[{}]:{}", remote_ip, ntohs(sin6_remote->sin6_port));
                } else {
                    conn.remote_endpoint = "*:*";
                }
            } else {
                close(fd);
                continue;
            }

            conn.state = (sock_type == SOCK_STREAM && has_peer) ? "ESTABLISHED" : "-";
            connections.push_back(std::move(conn));
            close(fd);
        }
    } catch (const std::exception& e) {
        add_error("get_network_connections", e.what());
    }

    if (connections.empty()) {
        auto parsed = parse_pfiles(pid);
        if (!parsed.connections.empty()) {
            return parsed.connections;
        }
    }

    return connections;
}

std::vector<MemoryMapInfo> SolarisProcessDataProvider::get_memory_maps(int pid) {
    std::vector<MemoryMapInfo> maps;
    std::string map_path = "/proc/" + std::to_string(pid) + "/map";

    int fd = open(map_path.c_str(), O_RDONLY);
    if (fd < 0) {
        return maps;
    }

    prmap_t pmap;
    while (read(fd, &pmap, sizeof(pmap)) == sizeof(pmap)) {
        MemoryMapInfo mm;

        mm.address = std::format("{:016x}-{:016x}",
            reinterpret_cast<uintptr_t>(pmap.pr_vaddr),
            reinterpret_cast<uintptr_t>(pmap.pr_vaddr) + pmap.pr_size);

        uint64_t size = pmap.pr_size;
        if (size >= 1024 * 1024 * 1024) {
            mm.size = std::format("{:.1f} GB", size / (1024.0 * 1024.0 * 1024.0));
        } else if (size >= 1024 * 1024) {
            mm.size = std::format("{:.1f} MB", size / (1024.0 * 1024.0));
        } else if (size >= 1024) {
            mm.size = std::format("{:.1f} KB", size / 1024.0);
        } else {
            mm.size = std::format("{} B", size);
        }

        // Permissions
        std::string perms;
        perms += (pmap.pr_mflags & MA_READ) ? 'r' : '-';
        perms += (pmap.pr_mflags & MA_WRITE) ? 'w' : '-';
        perms += (pmap.pr_mflags & MA_EXEC) ? 'x' : '-';
        perms += (pmap.pr_mflags & MA_SHARED) ? 's' : 'p';
        mm.permissions = perms;

        mm.pathname = pmap.pr_mapname[0] ? pmap.pr_mapname : "[anon]";

        maps.push_back(std::move(mm));
    }

    close(fd);
    return maps;
}

std::vector<EnvironmentVariable> SolarisProcessDataProvider::get_environment_variables(int pid) {
    std::vector<EnvironmentVariable> env;

    // Best-effort: use pargs -e to read environment (requires privileges for other users)
    std::string cmd = std::format("pargs -e {} 2>/dev/null", pid);
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        add_error("get_environment_variables", "popen failed");
        return env;
    }

    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        std::string line(buffer);
        // Trim whitespace/newlines
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        if (line.empty()) continue;

        // pargs -e outputs lines like "envp[0]: PATH=/usr/bin:..."
        // Strip the "envp[N]: " prefix if present
        if (line.rfind("envp[", 0) == 0) {
            auto colon = line.find("]: ");
            if (colon != std::string::npos) {
                line = line.substr(colon + 3);
            }
        }

        // Look for NAME=VALUE
        const auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;

        EnvironmentVariable ev;
        ev.name = line.substr(0, eq);
        ev.value = line.substr(eq + 1);
        env.push_back(std::move(ev));
    }

    pclose(pipe);
    return env;
}

// Helper to parse size back from human-readable format (e.g., "4.0 KB" -> 4096)
static uint64_t parse_size_string(const std::string& size_str) {
    double value = 0;
    char unit[8] = {};
    if (sscanf(size_str.c_str(), "%lf %7s", &value, unit) >= 1) {
        std::string u = unit;
        if (u == "GB") return static_cast<uint64_t>(value * 1024.0 * 1024.0 * 1024.0);
        if (u == "MB") return static_cast<uint64_t>(value * 1024.0 * 1024.0);
        if (u == "KB") return static_cast<uint64_t>(value * 1024.0);
        if (u == "B" || u.empty()) return static_cast<uint64_t>(value);
    }
    return 0;
}

std::vector<LibraryInfo> SolarisProcessDataProvider::get_libraries(int pid) {
    std::vector<LibraryInfo> libraries;
    std::map<std::string, LibraryInfo> lib_map;

    // Get memory maps and extract library paths
    auto maps = get_memory_maps(pid);

    for (const auto& mm : maps) {
        if (mm.pathname.empty() || mm.pathname[0] == '[') continue;

        uint64_t region_size = parse_size_string(mm.size);

        auto it = lib_map.find(mm.pathname);
        if (it == lib_map.end()) {
            LibraryInfo li;
            li.path = mm.pathname;
            auto slash = mm.pathname.rfind('/');
            li.name = (slash != std::string::npos) ? mm.pathname.substr(slash + 1) : mm.pathname;

            // Extract base address from mm.address (first part before '-')
            auto dash = mm.address.find('-');
            if (dash != std::string::npos) {
                li.base_address = mm.address.substr(0, dash);
            }

            li.total_size = region_size;
            li.resident_size = region_size;  // Solaris doesn't separate resident from total in prmap
            li.is_executable = (mm.permissions.find('x') != std::string::npos);
            lib_map[mm.pathname] = std::move(li);
        } else {
            // Accumulate sizes for same library
            it->second.total_size += region_size;
            it->second.resident_size += region_size;
            if (mm.permissions.find('x') != std::string::npos) {
                it->second.is_executable = true;
            }
        }
    }

    for (auto& [_, lib] : lib_map) {
        libraries.push_back(std::move(lib));
    }

    std::sort(libraries.begin(), libraries.end(), [](const LibraryInfo& a, const LibraryInfo& b) {
        return a.base_address < b.base_address;
    });

    return libraries;
}

std::vector<ParseError> SolarisProcessDataProvider::get_recent_errors() {
    std::lock_guard lock(errors_mutex_);
    return recent_errors_;
}

void SolarisProcessDataProvider::clear_errors() {
    std::lock_guard lock(errors_mutex_);
    recent_errors_.clear();
}

} // namespace pex
