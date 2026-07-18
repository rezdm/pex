#include "macos_process_data_provider.hpp"
#include "../../core/format_utils.hpp"

#include <libproc.h>
#include <sys/proc_info.h>
#include <mach/vm_prot.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <format>
#include <map>

namespace pex {

namespace {

// TH_STATE_* (from <mach/thread_info.h>, inlined to keep includes minimal).
char map_thread_state(int run_state) {
    switch (run_state) {
        case 1: return 'R';  // RUNNING
        case 2: return 'T';  // STOPPED
        case 3: return 'S';  // WAITING
        case 4: return 'D';  // UNINTERRUPTIBLE
        case 5: return 'T';  // HALTED
        default: return '?';
    }
}

// in_sockinfo.insi_vflag bits.
constexpr uint8_t kIniIPv4 = 0x1;
constexpr uint8_t kIniIPv6 = 0x2;

const char* tcp_state_str(int s) {
    switch (s) {
        case 0:  return "CLOSED";
        case 1:  return "LISTEN";
        case 2:  return "SYN_SENT";
        case 3:  return "SYN_RCVD";
        case 4:  return "ESTABLISHED";
        case 5:  return "CLOSE_WAIT";
        case 6:  return "FIN_WAIT_1";
        case 7:  return "CLOSING";
        case 8:  return "LAST_ACK";
        case 9:  return "FIN_WAIT_2";
        case 10: return "TIME_WAIT";
        default: return "";
    }
}

std::string format_endpoint(bool v6, const struct in_addr& a4,
                            const struct in6_addr& a6, int port_net) {
    char ip[INET6_ADDRSTRLEN] = {0};
    if (v6) {
        inet_ntop(AF_INET6, &a6, ip, sizeof(ip));
    } else {
        inet_ntop(AF_INET, &a4, ip, sizeof(ip));
    }
    const uint16_t port = ntohs(static_cast<uint16_t>(port_net & 0xffff));
    return std::string(ip) + ":" + (port ? std::to_string(port) : "*");
}

std::string basename_of(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

std::vector<ThreadInfo> MacosProcessDataProvider::get_threads(int pid) {
    std::vector<ThreadInfo> threads;

    const int needed = proc_pidinfo(pid, PROC_PIDLISTTHREADS, 0, nullptr, 0);
    if (needed <= 0) return threads;

    std::vector<uint64_t> tids(needed / sizeof(uint64_t) + 16);
    const int got = proc_pidinfo(pid, PROC_PIDLISTTHREADS, 0, tids.data(),
                                 static_cast<int>(tids.size() * sizeof(uint64_t)));
    const int count = got > 0 ? got / static_cast<int>(sizeof(uint64_t)) : 0;

    for (int i = 0; i < count; ++i) {
        struct proc_threadinfo pti;
        if (proc_pidinfo(pid, PROC_PIDTHREADINFO, tids[i], &pti, sizeof(pti)) !=
            static_cast<int>(sizeof(pti))) {
            continue;
        }
        ThreadInfo t;
        t.tid = static_cast<int>(tids[i]);
        t.name = pti.pth_name[0] ? pti.pth_name : "";
        t.state = map_thread_state(pti.pth_run_state);
        t.priority = pti.pth_curpri;
        threads.push_back(std::move(t));
    }
    return threads;
}

std::string MacosProcessDataProvider::get_thread_stack([[maybe_unused]] int pid,
                                                       [[maybe_unused]] int tid) {
    // Per-thread kernel stacks require task_for_pid (SIP-restricted); unavailable.
    return {};
}

std::vector<FileHandleInfo> MacosProcessDataProvider::get_file_handles(int pid) {
    std::vector<FileHandleInfo> handles;

    const int needed = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, nullptr, 0);
    if (needed <= 0) return handles;

    std::vector<char> buf(needed);
    const int got = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, buf.data(), needed);
    const int count = got > 0 ? got / static_cast<int>(sizeof(struct proc_fdinfo)) : 0;
    const auto* fds = reinterpret_cast<struct proc_fdinfo*>(buf.data());

    for (int i = 0; i < count; ++i) {
        FileHandleInfo h;
        h.fd = fds[i].proc_fd;
        switch (fds[i].proc_fdtype) {
            case PROX_FDTYPE_VNODE: {
                h.type = "file";
                struct vnode_fdinfowithpath vp;
                if (proc_pidfdinfo(pid, fds[i].proc_fd, PROC_PIDFDVNODEPATHINFO, &vp,
                                   sizeof(vp)) == static_cast<int>(sizeof(vp))) {
                    h.path = vp.pvip.vip_path;
                }
                break;
            }
            case PROX_FDTYPE_SOCKET:   h.type = "socket"; h.path = "socket"; break;
            case PROX_FDTYPE_PIPE:     h.type = "pipe";   h.path = "pipe";   break;
            case PROX_FDTYPE_KQUEUE:   h.type = "kqueue"; h.path = "kqueue"; break;
            case PROX_FDTYPE_PSEM:     h.type = "psem";   h.path = "semaphore"; break;
            case PROX_FDTYPE_PSHM:     h.type = "pshm";   h.path = "shared-memory"; break;
            case PROX_FDTYPE_FSEVENTS: h.type = "fsevents"; h.path = "fsevents"; break;
            default:                   h.type = "other";  break;
        }
        handles.push_back(std::move(h));
    }
    return handles;
}

std::vector<NetworkConnectionInfo> MacosProcessDataProvider::get_network_connections(int pid) {
    std::vector<NetworkConnectionInfo> conns;

    const int needed = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, nullptr, 0);
    if (needed <= 0) return conns;

    std::vector<char> buf(needed);
    const int got = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, buf.data(), needed);
    const int count = got > 0 ? got / static_cast<int>(sizeof(struct proc_fdinfo)) : 0;
    const auto* fds = reinterpret_cast<struct proc_fdinfo*>(buf.data());

    for (int i = 0; i < count; ++i) {
        if (fds[i].proc_fdtype != PROX_FDTYPE_SOCKET) continue;

        struct socket_fdinfo si;
        if (proc_pidfdinfo(pid, fds[i].proc_fd, PROC_PIDFDSOCKETINFO, &si, sizeof(si)) !=
            static_cast<int>(sizeof(si))) {
            continue;
        }
        const socket_info& soi = si.psi;

        if (soi.soi_kind == SOCKINFO_TCP) {
            const in_sockinfo& in = soi.soi_proto.pri_tcp.tcpsi_ini;
            const bool v6 = (in.insi_vflag & kIniIPv6) != 0;
            NetworkConnectionInfo n;
            n.protocol = v6 ? "tcp6" : "tcp";
            n.local_endpoint = format_endpoint(v6, in.insi_laddr.ina_46.i46a_addr4,
                                               in.insi_laddr.ina_6, in.insi_lport);
            n.remote_endpoint = format_endpoint(v6, in.insi_faddr.ina_46.i46a_addr4,
                                                in.insi_faddr.ina_6, in.insi_fport);
            n.state = tcp_state_str(soi.soi_proto.pri_tcp.tcpsi_state);
            conns.push_back(std::move(n));
        } else if (soi.soi_kind == SOCKINFO_IN) {
            const in_sockinfo& in = soi.soi_proto.pri_in;
            const bool v6 = (in.insi_vflag & kIniIPv6) != 0;
            NetworkConnectionInfo n;
            if (soi.soi_protocol == IPPROTO_UDP) {
                n.protocol = v6 ? "udp6" : "udp";
            } else {
                n.protocol = v6 ? "raw6" : "raw";
            }
            n.local_endpoint = format_endpoint(v6, in.insi_laddr.ina_46.i46a_addr4,
                                               in.insi_laddr.ina_6, in.insi_lport);
            n.remote_endpoint = format_endpoint(v6, in.insi_faddr.ina_46.i46a_addr4,
                                                in.insi_faddr.ina_6, in.insi_fport);
            conns.push_back(std::move(n));
        }
    }
    return conns;
}

std::vector<MemoryMapInfo> MacosProcessDataProvider::get_memory_maps(int pid) {
    std::vector<MemoryMapInfo> maps;

    uint64_t addr = 0;
    for (int guard = 0; guard < 1000000; ++guard) {
        struct proc_regionwithpathinfo rpi;
        if (proc_pidinfo(pid, PROC_PIDREGIONPATHINFO, addr, &rpi, sizeof(rpi)) !=
            static_cast<int>(sizeof(rpi))) {
            break;
        }
        const auto& r = rpi.prp_prinfo;
        MemoryMapInfo m;
        m.address = std::format("{:016x}-{:016x}", r.pri_address, r.pri_address + r.pri_size);
        m.size_bytes = r.pri_size;
        m.size = format_bytes(static_cast<int64_t>(r.pri_size), false);
        std::string perms;
        perms += (r.pri_protection & VM_PROT_READ) ? 'r' : '-';
        perms += (r.pri_protection & VM_PROT_WRITE) ? 'w' : '-';
        perms += (r.pri_protection & VM_PROT_EXECUTE) ? 'x' : '-';
        m.permissions = perms;
        m.pathname = rpi.prp_vip.vip_path[0] ? rpi.prp_vip.vip_path : "[anon]";
        maps.push_back(std::move(m));

        const uint64_t next = r.pri_address + r.pri_size;
        if (next <= addr) break;  // no forward progress: stop
        addr = next;
    }
    return maps;
}

std::vector<EnvironmentVariable> MacosProcessDataProvider::get_environment_variables(int pid) {
    std::vector<EnvironmentVariable> env;
    std::vector<char> buf(argmax_);
    read_proc_args(pid, buf, nullptr, &env);
    return env;
}

std::vector<LibraryInfo> MacosProcessDataProvider::get_libraries(int pid) {
    // No unprivileged dyld image list for a foreign task; derive the loaded
    // images from file-backed memory regions instead, aggregating per path.
    std::string exe_path;
    char pathbuf[PROC_PIDPATHINFO_MAXSIZE];
    if (proc_pidpath(pid, pathbuf, sizeof(pathbuf)) > 0) exe_path = pathbuf;

    const long page = sysconf(_SC_PAGESIZE);
    std::map<std::string, LibraryInfo> by_path;

    uint64_t addr = 0;
    for (int guard = 0; guard < 1000000; ++guard) {
        struct proc_regionwithpathinfo rpi;
        if (proc_pidinfo(pid, PROC_PIDREGIONPATHINFO, addr, &rpi, sizeof(rpi)) !=
            static_cast<int>(sizeof(rpi))) {
            break;
        }
        const auto& r = rpi.prp_prinfo;
        const uint64_t next = r.pri_address + r.pri_size;

        if (rpi.prp_vip.vip_path[0]) {
            const std::string path = rpi.prp_vip.vip_path;
            auto [it, inserted] = by_path.try_emplace(path);
            LibraryInfo& lib = it->second;
            if (inserted) {
                lib.path = path;
                lib.name = basename_of(path);
                lib.base_addr = r.pri_address;
                lib.base_address = std::format("{:016x}", r.pri_address);
                lib.is_executable = (path == exe_path);
            } else if (r.pri_address < lib.base_addr) {
                lib.base_addr = r.pri_address;
                lib.base_address = std::format("{:016x}", r.pri_address);
            }
            lib.total_size += static_cast<int64_t>(r.pri_size);
            lib.resident_size += static_cast<int64_t>(r.pri_pages_resident) * page;
        }

        if (next <= addr) break;
        addr = next;
    }

    std::vector<LibraryInfo> libs;
    libs.reserve(by_path.size());
    for (auto& [path, lib] : by_path) libs.push_back(std::move(lib));
    return libs;
}

} // namespace pex
