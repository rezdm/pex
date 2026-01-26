#include "freebsd_process_data_provider.hpp"

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libprocstat.h>
#include <pwd.h>
#include <unistd.h>
#include <fcntl.h>
#include <kvm.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <format>
#include <algorithm>
#include <map>

namespace pex {

FreeBSDProcessDataProvider::FreeBSDProcessDataProvider() = default;
FreeBSDProcessDataProvider::~FreeBSDProcessDataProvider() = default;

void FreeBSDProcessDataProvider::add_error(const std::string& context, const std::string& message) {
    std::lock_guard lock(errors_mutex_);
    recent_errors_.push_back({std::chrono::steady_clock::now(), context + ": " + message});
    if (recent_errors_.size() > 100) {
        recent_errors_.erase(recent_errors_.begin());
    }
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
    struct passwd* pw = getpwuid(uid);
    if (pw) {
        return pw->pw_name;
    }
    return std::to_string(uid);
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

    for (size_t i = 0; i < count; ++i) {
        ProcessInfo info;
        info.pid = kp[i].ki_pid;
        info.parent_pid = kp[i].ki_ppid;
        info.name = kp[i].ki_comm;
        info.state_char = map_state(kp[i].ki_stat);
        info.user_name = get_username(kp[i].ki_uid);
        info.priority = kp[i].ki_nice;
        info.thread_count = kp[i].ki_numthreads;

        // Memory info
        info.resident_memory = kp[i].ki_rssize * getpagesize();
        info.virtual_memory = kp[i].ki_size;
        if (total_memory > 0) {
            info.memory_percent = (static_cast<double>(info.resident_memory) / total_memory) * 100.0;
        }

        // CPU times (in clock ticks)
        info.user_time = kp[i].ki_rusage.ru_utime.tv_sec * sysconf(_SC_CLK_TCK) +
                         kp[i].ki_rusage.ru_utime.tv_usec * sysconf(_SC_CLK_TCK) / 1000000;
        info.kernel_time = kp[i].ki_rusage.ru_stime.tv_sec * sysconf(_SC_CLK_TCK) +
                           kp[i].ki_rusage.ru_stime.tv_usec * sysconf(_SC_CLK_TCK) / 1000000;

        // Start time
        auto start_sec = std::chrono::seconds(kp[i].ki_start.tv_sec);
        info.start_time = std::chrono::system_clock::time_point(start_sec);

        // Try to get full command line using procstat
        struct procstat* ps = procstat_open_sysctl();
        if (ps) {
            unsigned int cnt;
            struct kinfo_proc* kproc = procstat_getprocs(ps, KERN_PROC_PID, info.pid, &cnt);
            if (kproc && cnt > 0) {
                char** args = procstat_getargv(ps, kproc, 0);
                if (args) {
                    std::ostringstream cmdline;
                    for (int j = 0; args[j] != nullptr; ++j) {
                        if (j > 0) cmdline << ' ';
                        cmdline << args[j];
                    }
                    info.command_line = cmdline.str();
                    if (!info.command_line.empty() && args[0]) {
                        info.executable_path = args[0];
                    }
                    procstat_freeargv(ps);
                }
                procstat_freeprocs(ps, kproc);
            }
            procstat_close(ps);
        }

        if (info.command_line.empty()) {
            info.command_line = info.name;
        }

        processes.push_back(std::move(info));
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
    info.user_name = get_username(kp.ki_uid);
    info.priority = kp.ki_nice;
    info.thread_count = kp.ki_numthreads;
    info.resident_memory = kp.ki_rssize * getpagesize();
    info.virtual_memory = kp.ki_size;

    if (total_memory > 0) {
        info.memory_percent = (static_cast<double>(info.resident_memory) / total_memory) * 100.0;
    }

    info.user_time = kp.ki_rusage.ru_utime.tv_sec * sysconf(_SC_CLK_TCK) +
                     kp.ki_rusage.ru_utime.tv_usec * sysconf(_SC_CLK_TCK) / 1000000;
    info.kernel_time = kp.ki_rusage.ru_stime.tv_sec * sysconf(_SC_CLK_TCK) +
                       kp.ki_rusage.ru_stime.tv_usec * sysconf(_SC_CLK_TCK) / 1000000;

    auto start_sec = std::chrono::seconds(kp.ki_start.tv_sec);
    info.start_time = std::chrono::system_clock::time_point(start_sec);

    return info;
}

std::vector<ThreadInfo> FreeBSDProcessDataProvider::get_threads(int pid) {
    std::vector<ThreadInfo> threads;

    struct procstat* ps = procstat_open_sysctl();
    if (!ps) {
        add_error("get_threads", "procstat_open_sysctl failed");
        return threads;
    }

    unsigned int cnt;
    struct kinfo_proc* procs = procstat_getprocs(ps, KERN_PROC_PID | KERN_PROC_INC_THREAD, pid, &cnt);
    if (!procs) {
        procstat_close(ps);
        return threads;
    }

    for (unsigned int i = 0; i < cnt; ++i) {
        ThreadInfo ti;
        ti.tid = procs[i].ki_tid;
        ti.name = procs[i].ki_tdname[0] ? procs[i].ki_tdname : procs[i].ki_comm;
        ti.state = map_state(procs[i].ki_stat);
        ti.priority = procs[i].ki_pri.pri_level;
        ti.processor = procs[i].ki_lastcpu;
        threads.push_back(std::move(ti));
    }

    procstat_freeprocs(ps, procs);
    procstat_close(ps);
    return threads;
}

std::string FreeBSDProcessDataProvider::get_thread_stack([[maybe_unused]] int pid, [[maybe_unused]] int tid) {
    // Thread stack traces require kernel debugging support
    // Return empty for now - could potentially use ptrace or procfs if available
    return "";
}

std::vector<FileHandleInfo> FreeBSDProcessDataProvider::get_file_handles(int pid) {
    std::vector<FileHandleInfo> handles;

    struct procstat* ps = procstat_open_sysctl();
    if (!ps) {
        add_error("get_file_handles", "procstat_open_sysctl failed");
        return handles;
    }

    unsigned int cnt;
    struct kinfo_proc* proc = procstat_getprocs(ps, KERN_PROC_PID, pid, &cnt);
    if (!proc || cnt == 0) {
        procstat_close(ps);
        return handles;
    }

    unsigned int fcnt;
    struct filestat_list* flist = procstat_getfiles(ps, proc, 0);
    if (flist) {
        struct filestat* fst;
        STAILQ_FOREACH(fst, flist, next) {
            FileHandleInfo fh;
            fh.fd = fst->fs_fd;

            switch (fst->fs_type) {
                case PS_FST_TYPE_VNODE:
                    fh.type = "file";
                    break;
                case PS_FST_TYPE_SOCKET:
                    fh.type = "socket";
                    break;
                case PS_FST_TYPE_PIPE:
                    fh.type = "pipe";
                    break;
                case PS_FST_TYPE_FIFO:
                    fh.type = "fifo";
                    break;
                case PS_FST_TYPE_PTS:
                    fh.type = "pts";
                    break;
                case PS_FST_TYPE_SHM:
                    fh.type = "shm";
                    break;
                case PS_FST_TYPE_SEM:
                    fh.type = "sem";
                    break;
                default:
                    fh.type = "unknown";
            }

            if (fst->fs_path) {
                fh.path = fst->fs_path;
            } else {
                fh.path = std::format("[{}]", fh.type);
            }

            handles.push_back(std::move(fh));
        }
        procstat_freefiles(ps, flist);
    }

    procstat_freeprocs(ps, proc);
    procstat_close(ps);
    return handles;
}

std::vector<NetworkConnectionInfo> FreeBSDProcessDataProvider::get_network_connections(int pid) {
    std::vector<NetworkConnectionInfo> connections;

    struct procstat* ps = procstat_open_sysctl();
    if (!ps) {
        return connections;
    }

    unsigned int cnt;
    struct kinfo_proc* proc = procstat_getprocs(ps, KERN_PROC_PID, pid, &cnt);
    if (!proc || cnt == 0) {
        procstat_close(ps);
        return connections;
    }

    struct filestat_list* flist = procstat_getfiles(ps, proc, 0);
    if (flist) {
        struct filestat* fst;
        STAILQ_FOREACH(fst, flist, next) {
            if (fst->fs_type != PS_FST_TYPE_SOCKET) continue;

            struct sockstat ss;
            if (procstat_get_socket_info(ps, fst, &ss, nullptr) != 0) continue;

            NetworkConnectionInfo conn;

            // Determine protocol
            if (ss.dom_family == AF_INET) {
                conn.protocol = (ss.type == SOCK_STREAM) ? "tcp" : "udp";
            } else if (ss.dom_family == AF_INET6) {
                conn.protocol = (ss.type == SOCK_STREAM) ? "tcp6" : "udp6";
            } else {
                continue;  // Skip non-IP sockets
            }

            // Format addresses
            char local_addr[INET6_ADDRSTRLEN + 10];
            char remote_addr[INET6_ADDRSTRLEN + 10];

            if (ss.dom_family == AF_INET) {
                struct sockaddr_in* sin_local = (struct sockaddr_in*)&ss.sa_local;
                struct sockaddr_in* sin_peer = (struct sockaddr_in*)&ss.sa_peer;
                char local_ip[INET_ADDRSTRLEN], remote_ip[INET_ADDRSTRLEN];

                inet_ntop(AF_INET, &sin_local->sin_addr, local_ip, sizeof(local_ip));
                inet_ntop(AF_INET, &sin_peer->sin_addr, remote_ip, sizeof(remote_ip));

                snprintf(local_addr, sizeof(local_addr), "%s:%d", local_ip, ntohs(sin_local->sin_port));
                snprintf(remote_addr, sizeof(remote_addr), "%s:%d", remote_ip, ntohs(sin_peer->sin_port));
            } else {
                struct sockaddr_in6* sin6_local = (struct sockaddr_in6*)&ss.sa_local;
                struct sockaddr_in6* sin6_peer = (struct sockaddr_in6*)&ss.sa_peer;
                char local_ip[INET6_ADDRSTRLEN], remote_ip[INET6_ADDRSTRLEN];

                inet_ntop(AF_INET6, &sin6_local->sin6_addr, local_ip, sizeof(local_ip));
                inet_ntop(AF_INET6, &sin6_peer->sin6_addr, remote_ip, sizeof(remote_ip));

                snprintf(local_addr, sizeof(local_addr), "[%s]:%d", local_ip, ntohs(sin6_local->sin6_port));
                snprintf(remote_addr, sizeof(remote_addr), "[%s]:%d", remote_ip, ntohs(sin6_peer->sin6_port));
            }

            conn.local_endpoint = local_addr;
            conn.remote_endpoint = remote_addr;

            // TCP state
            if (ss.type == SOCK_STREAM) {
                static const char* tcp_states[] = {
                    "CLOSED", "LISTEN", "SYN_SENT", "SYN_RCVD",
                    "ESTABLISHED", "CLOSE_WAIT", "FIN_WAIT_1", "CLOSING",
                    "LAST_ACK", "FIN_WAIT_2", "TIME_WAIT"
                };
                if (ss.proto == IPPROTO_TCP && ss.so_pcb) {
                    // The state would be in the TCP control block
                    // For now, use a simple heuristic
                    conn.state = "ESTABLISHED";
                } else {
                    conn.state = "-";
                }
            } else {
                conn.state = "-";
            }

            connections.push_back(std::move(conn));
        }
        procstat_freefiles(ps, flist);
    }

    procstat_freeprocs(ps, proc);
    procstat_close(ps);
    return connections;
}

std::vector<MemoryMapInfo> FreeBSDProcessDataProvider::get_memory_maps(int pid) {
    std::vector<MemoryMapInfo> maps;

    struct procstat* ps = procstat_open_sysctl();
    if (!ps) {
        return maps;
    }

    unsigned int cnt;
    struct kinfo_proc* proc = procstat_getprocs(ps, KERN_PROC_PID, pid, &cnt);
    if (!proc || cnt == 0) {
        procstat_close(ps);
        return maps;
    }

    unsigned int vmcnt;
    struct kinfo_vmentry* vmaps = procstat_getvmmap(ps, proc, &vmcnt);
    if (vmaps) {
        for (unsigned int i = 0; i < vmcnt; ++i) {
            MemoryMapInfo mm;

            mm.address = std::format("{:016x}-{:016x}",
                vmaps[i].kve_start, vmaps[i].kve_end);

            uint64_t size = vmaps[i].kve_end - vmaps[i].kve_start;
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
            perms += (vmaps[i].kve_protection & KVME_PROT_READ) ? 'r' : '-';
            perms += (vmaps[i].kve_protection & KVME_PROT_WRITE) ? 'w' : '-';
            perms += (vmaps[i].kve_protection & KVME_PROT_EXEC) ? 'x' : '-';
            perms += (vmaps[i].kve_flags & KVME_FLAG_COW) ? 'c' : 'p';
            mm.permissions = perms;

            if (vmaps[i].kve_path[0]) {
                mm.pathname = vmaps[i].kve_path;
            } else {
                switch (vmaps[i].kve_type) {
                    case KVME_TYPE_NONE:
                        mm.pathname = "[none]";
                        break;
                    case KVME_TYPE_DEFAULT:
                        mm.pathname = "[anon]";
                        break;
                    case KVME_TYPE_VNODE:
                        mm.pathname = "[vnode]";
                        break;
                    case KVME_TYPE_SWAP:
                        mm.pathname = "[swap]";
                        break;
                    case KVME_TYPE_DEVICE:
                        mm.pathname = "[device]";
                        break;
                    case KVME_TYPE_PHYS:
                        mm.pathname = "[phys]";
                        break;
                    case KVME_TYPE_DEAD:
                        mm.pathname = "[dead]";
                        break;
                    case KVME_TYPE_SG:
                        mm.pathname = "[sg]";
                        break;
                    case KVME_TYPE_GUARD:
                        mm.pathname = "[guard]";
                        break;
                    default:
                        mm.pathname = "[unknown]";
                }
            }

            maps.push_back(std::move(mm));
        }
        procstat_freevmmap(ps, vmaps);
    }

    procstat_freeprocs(ps, proc);
    procstat_close(ps);
    return maps;
}

std::vector<EnvironmentVariable> FreeBSDProcessDataProvider::get_environment_variables(int pid) {
    std::vector<EnvironmentVariable> env;

    struct procstat* ps = procstat_open_sysctl();
    if (!ps) {
        return env;
    }

    unsigned int cnt;
    struct kinfo_proc* proc = procstat_getprocs(ps, KERN_PROC_PID, pid, &cnt);
    if (!proc || cnt == 0) {
        procstat_close(ps);
        return env;
    }

    char** envp = procstat_getenvv(ps, proc, 0);
    if (envp) {
        for (int i = 0; envp[i] != nullptr; ++i) {
            std::string entry = envp[i];
            auto pos = entry.find('=');
            if (pos != std::string::npos) {
                EnvironmentVariable ev;
                ev.name = entry.substr(0, pos);
                ev.value = entry.substr(pos + 1);
                env.push_back(std::move(ev));
            }
        }
        procstat_freeenvv(ps);
    }

    procstat_freeprocs(ps, proc);
    procstat_close(ps);
    return env;
}

std::vector<LibraryInfo> FreeBSDProcessDataProvider::get_libraries(int pid) {
    std::vector<LibraryInfo> libraries;
    std::map<std::string, LibraryInfo> lib_map;

    struct procstat* ps = procstat_open_sysctl();
    if (!ps) {
        return libraries;
    }

    unsigned int cnt;
    struct kinfo_proc* proc = procstat_getprocs(ps, KERN_PROC_PID, pid, &cnt);
    if (!proc || cnt == 0) {
        procstat_close(ps);
        return libraries;
    }

    unsigned int vmcnt;
    struct kinfo_vmentry* vmaps = procstat_getvmmap(ps, proc, &vmcnt);
    if (vmaps) {
        for (unsigned int i = 0; i < vmcnt; ++i) {
            if (vmaps[i].kve_path[0] == '\0') continue;
            if (vmaps[i].kve_type != KVME_TYPE_VNODE) continue;

            std::string path = vmaps[i].kve_path;
            uint64_t size = vmaps[i].kve_end - vmaps[i].kve_start;

            auto it = lib_map.find(path);
            if (it == lib_map.end()) {
                LibraryInfo li;
                li.path = path;
                auto slash = path.rfind('/');
                li.name = (slash != std::string::npos) ? path.substr(slash + 1) : path;
                li.base_address = std::format("{:x}", vmaps[i].kve_start);
                li.total_size = size;
                li.resident_size = vmaps[i].kve_resident * getpagesize();
                li.is_executable = (vmaps[i].kve_protection & KVME_PROT_EXEC) != 0;
                lib_map[path] = std::move(li);
            } else {
                it->second.total_size += size;
                it->second.resident_size += vmaps[i].kve_resident * getpagesize();
                if (vmaps[i].kve_protection & KVME_PROT_EXEC) {
                    it->second.is_executable = true;
                }
            }
        }
        procstat_freevmmap(ps, vmaps);
    }

    procstat_freeprocs(ps, proc);
    procstat_close(ps);

    for (auto& [_, lib] : lib_map) {
        libraries.push_back(std::move(lib));
    }

    // Sort by base address
    std::sort(libraries.begin(), libraries.end(), [](const LibraryInfo& a, const LibraryInfo& b) {
        return a.base_address < b.base_address;
    });

    return libraries;
}

std::vector<ParseError> FreeBSDProcessDataProvider::get_recent_errors() {
    std::lock_guard lock(errors_mutex_);
    return recent_errors_;
}

void FreeBSDProcessDataProvider::clear_errors() {
    std::lock_guard lock(errors_mutex_);
    recent_errors_.clear();
}

} // namespace pex
