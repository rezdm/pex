#include "solaris_process_data_provider.hpp"

#include <sys/types.h>
#include <sys/stat.h>
#include <procfs.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <format>
#include <algorithm>
#include <map>
#include <filesystem>

namespace fs = std::filesystem;

namespace pex {

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
    struct passwd* pw = getpwuid(uid);
    if (pw) {
        return pw->pw_name;
    }
    return std::to_string(uid);
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

    try {
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
            ti.name = lwpinfo.pr_name[0] ? lwpinfo.pr_name : "";
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

            // Read the symlink to get the path
            std::string link_path = entry.path().string();
            try {
                std::string target = fs::read_symlink(link_path).string();
                fh.path = target;

                // Determine type from path
                if (target.find("socket") != std::string::npos) {
                    fh.type = "socket";
                } else if (target.find("pipe") != std::string::npos) {
                    fh.type = "pipe";
                } else if (target.starts_with("/dev/")) {
                    fh.type = "device";
                } else {
                    fh.type = "file";
                }
            } catch (...) {
                fh.path = "[unknown]";
                fh.type = "unknown";
            }

            handles.push_back(std::move(fh));
        }
    } catch (const std::exception& e) {
        add_error("get_file_handles", e.what());
    }

    return handles;
}

std::vector<NetworkConnectionInfo> SolarisProcessDataProvider::get_network_connections([[maybe_unused]] int pid) {
    // Solaris network connections would require parsing /etc/net/* or using libproc
    // This is complex and platform-specific - return empty for now
    return {};
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

    // Read /proc/<pid>/psinfo to get pr_envp pointer, then read from /proc/<pid>/as
    // This is complex - for now, try reading /proc/<pid>/auxv or return empty
    // A simpler approach is to use pargs -e command output, but that requires exec

    // Return empty for now - this could be implemented with libproc
    return env;
}

std::vector<LibraryInfo> SolarisProcessDataProvider::get_libraries(int pid) {
    std::vector<LibraryInfo> libraries;
    std::map<std::string, LibraryInfo> lib_map;

    // Get memory maps and extract library paths
    auto maps = get_memory_maps(pid);

    for (const auto& mm : maps) {
        if (mm.pathname.empty() || mm.pathname[0] == '[') continue;

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

            // Parse size from human-readable format back to bytes
            // (simplified - just store 0 and let UI handle it)
            li.total_size = 0;
            li.resident_size = 0;
            li.is_executable = (mm.permissions.find('x') != std::string::npos);
            lib_map[mm.pathname] = std::move(li);
        } else {
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
