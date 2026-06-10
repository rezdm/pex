#include "freebsd_process_data_provider.hpp"
#include "../../core/format_utils.hpp"

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
#include <unistd.h>

#include <cstring>
#include <format>
#include <algorithm>
#include <map>
#include <cstdio>

namespace pex {

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

            // TCP state - FreeBSD's procstat doesn't expose TCP state directly
            // We infer state based on peer address: if remote is non-zero, likely connected
            if (ss.type == SOCK_STREAM) {
                bool has_peer = false;
                if (ss.dom_family == AF_INET) {
                    struct sockaddr_in* sin_peer = (struct sockaddr_in*)&ss.sa_peer;
                    has_peer = (sin_peer->sin_port != 0 || sin_peer->sin_addr.s_addr != 0);
                } else if (ss.dom_family == AF_INET6) {
                    struct sockaddr_in6* sin6_peer = (struct sockaddr_in6*)&ss.sa_peer;
                    has_peer = (sin6_peer->sin6_port != 0);
                }
                // Without kernel TCP state access, infer: has peer = likely established/connected
                conn.state = has_peer ? "ESTABLISHED" : "LISTEN";
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
            mm.size_bytes = size;
            mm.size = format_bytes(static_cast<int64_t>(size), false);

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

    // Real executable path, used to distinguish the main binary from shared libraries
    const std::string exe_path = get_executable_path(pid);

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
                li.base_addr = vmaps[i].kve_start;
                li.base_address = std::format("{:x}", vmaps[i].kve_start);
                li.total_size = size;
                li.resident_size = vmaps[i].kve_resident * getpagesize();
                li.is_executable = (!exe_path.empty() && path == exe_path);
                lib_map[path] = std::move(li);
            } else {
                it->second.total_size += size;
                it->second.resident_size += vmaps[i].kve_resident * getpagesize();
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
        return a.base_addr < b.base_addr;
    });

    return libraries;
}

std::vector<ParseError> FreeBSDProcessDataProvider::get_recent_errors() {
    std::lock_guard lock(errors_mutex_);
    const auto cutoff = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    std::vector<ParseError> result;
    for (const auto& err : recent_errors_) {
        if (err.timestamp > cutoff) {
            result.push_back(err);
        }
    }
    return result;
}

void FreeBSDProcessDataProvider::clear_errors() {
    std::lock_guard lock(errors_mutex_);
    recent_errors_.clear();
}

} // namespace pex
