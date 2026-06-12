#include "windows_process_data_provider.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <iphlpapi.h>
#include <sddl.h>

#include <algorithm>
#include <chrono>
#include <format>

namespace pex {

namespace {

uint64_t filetime_to_u64(const FILETIME& ft) {
    ULARGE_INTEGER v;
    v.LowPart = ft.dwLowDateTime;
    v.HighPart = ft.dwHighDateTime;
    return v.QuadPart;
}

// FILETIME epoch (1601-01-01) to Unix epoch (1970-01-01) in seconds
constexpr uint64_t kFiletimeEpochDelta = 11644473600ULL;

std::string narrow(const wchar_t* wide) {
    if (!wide || !*wide) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), len, nullptr, nullptr);
    return out;
}

const char* tcp_state_name(const DWORD state) {
    switch (state) {
        case MIB_TCP_STATE_CLOSED:     return "CLOSED";
        case MIB_TCP_STATE_LISTEN:     return "LISTEN";
        case MIB_TCP_STATE_SYN_SENT:   return "SYN_SENT";
        case MIB_TCP_STATE_SYN_RCVD:   return "SYN_RECV";
        case MIB_TCP_STATE_ESTAB:      return "ESTABLISHED";
        case MIB_TCP_STATE_FIN_WAIT1:  return "FIN_WAIT1";
        case MIB_TCP_STATE_FIN_WAIT2:  return "FIN_WAIT2";
        case MIB_TCP_STATE_CLOSE_WAIT: return "CLOSE_WAIT";
        case MIB_TCP_STATE_CLOSING:    return "CLOSING";
        case MIB_TCP_STATE_LAST_ACK:   return "LAST_ACK";
        case MIB_TCP_STATE_TIME_WAIT:  return "TIME_WAIT";
        case MIB_TCP_STATE_DELETE_TCB: return "DELETE";
        default:                       return "UNKNOWN";
    }
}

std::string ipv4_endpoint(const DWORD addr, const DWORD port_be) {
    char ip[INET_ADDRSTRLEN] = "0.0.0.0";
    IN_ADDR in;
    in.S_un.S_addr = addr;
    inet_ntop(AF_INET, &in, ip, sizeof(ip));
    return std::format("{}:{}", ip, ntohs(static_cast<u_short>(port_be)));
}

std::string ipv6_endpoint(const UCHAR* addr, const DWORD port_be) {
    char ip[INET6_ADDRSTRLEN] = "::";
    IN6_ADDR in{};
    memcpy(&in, addr, sizeof(in));
    inet_ntop(AF_INET6, &in, ip, sizeof(ip));
    return std::format("[{}]:{}", ip, ntohs(static_cast<u_short>(port_be)));
}

} // namespace

WindowsProcessDataProvider::WindowsProcessDataProvider() = default;

void WindowsProcessDataProvider::add_error(const std::string& context, const std::string& message) {
    std::lock_guard lock(errors_mutex_);
    recent_errors_.push_back({std::chrono::steady_clock::now(), context + ": " + message});
    if (recent_errors_.size() > kMaxErrors) {
        recent_errors_.erase(recent_errors_.begin());
    }
}

std::string WindowsProcessDataProvider::get_username_for_pid(void* process_handle) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(process_handle, TOKEN_QUERY, &token)) {
        return {};
    }

    std::string result;
    DWORD len = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &len);
    if (len > 0) {
        std::vector<char> buf(len);
        if (GetTokenInformation(token, TokenUser, buf.data(), len, &len)) {
            const auto* user = reinterpret_cast<TOKEN_USER*>(buf.data());

            // Cache by SID string: LookupAccountSid is expensive
            LPWSTR sid_str = nullptr;
            std::string sid_key;
            if (ConvertSidToStringSidW(user->User.Sid, &sid_str)) {
                sid_key = narrow(sid_str);
                LocalFree(sid_str);
            }
            if (!sid_key.empty()) {
                std::lock_guard lock(username_cache_mutex_);
                if (const auto it = username_cache_.find(sid_key); it != username_cache_.end()) {
                    CloseHandle(token);
                    return it->second;
                }
            }

            wchar_t name[256], domain[256];
            DWORD name_len = 256, domain_len = 256;
            SID_NAME_USE use;
            if (LookupAccountSidW(nullptr, user->User.Sid, name, &name_len,
                                  domain, &domain_len, &use)) {
                result = narrow(name);
            }
            if (!sid_key.empty()) {
                std::lock_guard lock(username_cache_mutex_);
                username_cache_[sid_key] = result;
            }
        }
    }
    CloseHandle(token);
    return result;
}

void WindowsProcessDataProvider::fill_process_details(ProcessInfo& info, void* process_handle,
                                                      const int64_t total_memory) {
    HANDLE h = process_handle;

    FILETIME create{}, exit_t{}, kernel{}, user{};
    if (GetProcessTimes(h, &create, &exit_t, &kernel, &user)) {
        info.user_time = filetime_to_u64(user);     // 100 ns units, consistent
        info.kernel_time = filetime_to_u64(kernel); // with GetSystemTimes
        const uint64_t created = filetime_to_u64(create);
        if (created > 0) {
            info.start_time = std::chrono::system_clock::from_time_t(
                static_cast<time_t>(created / 10'000'000ULL - kFiletimeEpochDelta));
        }
    }

    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(h, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        info.resident_memory = static_cast<int64_t>(pmc.WorkingSetSize);
        info.virtual_memory = static_cast<int64_t>(pmc.PrivateUsage);
        if (total_memory > 0) {
            info.memory_percent = static_cast<double>(info.resident_memory) /
                                  static_cast<double>(total_memory) * 100.0;
        }
    }

    IO_COUNTERS io{};
    if (GetProcessIoCounters(h, &io)) {
        info.io_read_bytes = io.ReadTransferCount;
        info.io_write_bytes = io.WriteTransferCount;
    }

    wchar_t path[MAX_PATH];
    DWORD path_len = MAX_PATH;
    if (QueryFullProcessImageNameW(h, 0, path, &path_len)) {
        info.executable_path = narrow(path);
    }

    info.user_name = get_username_for_pid(h);
    info.priority = static_cast<int>(GetPriorityClass(h));
}

std::vector<ProcessInfo> WindowsProcessDataProvider::get_all_processes(int64_t total_memory) {
    std::vector<ProcessInfo> processes;

    if (total_memory <= 0) {
        MEMORYSTATUSEX ms{};
        ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms)) total_memory = static_cast<int64_t>(ms.ullTotalPhys);
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        add_error("get_all_processes", "CreateToolhelp32Snapshot failed");
        return processes;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry)) {
        ProcessInfo info;
        info.pid = static_cast<int>(entry.th32ProcessID);
        info.parent_pid = static_cast<int>(entry.th32ParentProcessID);
        info.name = narrow(entry.szExeFile);
        info.thread_count = static_cast<int>(entry.cntThreads);
        info.state_char = 'R';  // Windows exposes no R/S/D process state
        // PID 0 (System Idle) and PID 4 (System) are kernel
        info.is_kernel_thread = (info.pid == 0 || info.pid == 4 || info.parent_pid == 4);

        if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                   entry.th32ProcessID)) {
            fill_process_details(info, h, total_memory);
            CloseHandle(h);
        }
        if (info.name.empty()) info.name = std::format("(pid {})", info.pid);
        if (info.command_line.empty()) {
            info.command_line = info.executable_path.empty() ? info.name : info.executable_path;
        }

        processes.push_back(std::move(info));
    }
    CloseHandle(snapshot);

    return processes;
}

std::optional<ProcessInfo> WindowsProcessDataProvider::get_process_info(const int pid,
                                                                        const int64_t total_memory) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return std::nullopt;

    ProcessInfo info;
    info.pid = pid;
    info.state_char = 'R';
    fill_process_details(info, h, total_memory);
    CloseHandle(h);
    if (info.name.empty()) {
        const size_t slash = info.executable_path.find_last_of('\\');
        info.name = slash != std::string::npos ? info.executable_path.substr(slash + 1)
                                               : info.executable_path;
    }
    return info;
}

std::vector<ThreadInfo> WindowsProcessDataProvider::get_threads(const int pid) {
    std::vector<ThreadInfo> threads;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return threads;

    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != static_cast<DWORD>(pid)) continue;
        ThreadInfo ti;
        ti.tid = static_cast<int>(entry.th32ThreadID);
        ti.priority = static_cast<int>(entry.tpBasePri);
        ti.state = '?';
        threads.push_back(std::move(ti));
    }
    CloseHandle(snapshot);
    return threads;
}

std::string WindowsProcessDataProvider::get_thread_stack(int, int) {
    return "";  // Would need StackWalk64 + symbols (Phase 2+)
}

std::vector<FileHandleInfo> WindowsProcessDataProvider::get_file_handles(int) {
    return {};  // Phase 2: NtQuerySystemInformation(SystemHandleInformation)
}

std::vector<NetworkConnectionInfo> WindowsProcessDataProvider::get_network_connections(const int pid) {
    std::vector<NetworkConnectionInfo> connections;
    const auto want = static_cast<DWORD>(pid);

    // TCP v4
    DWORD size = 0;
    GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (size > 0) {
        std::vector<char> buf(size);
        if (GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET,
                                TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
            const auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buf.data());
            for (DWORD i = 0; i < table->dwNumEntries; i++) {
                const auto& row = table->table[i];
                if (row.dwOwningPid != want) continue;
                NetworkConnectionInfo conn;
                conn.protocol = "tcp";
                conn.local_endpoint = ipv4_endpoint(row.dwLocalAddr, row.dwLocalPort);
                conn.remote_endpoint = row.dwState == MIB_TCP_STATE_LISTEN
                    ? "*:*" : ipv4_endpoint(row.dwRemoteAddr, row.dwRemotePort);
                conn.state = tcp_state_name(row.dwState);
                connections.push_back(std::move(conn));
            }
        }
    }

    // TCP v6
    size = 0;
    GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0);
    if (size > 0) {
        std::vector<char> buf(size);
        if (GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET6,
                                TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
            const auto* table = reinterpret_cast<MIB_TCP6TABLE_OWNER_PID*>(buf.data());
            for (DWORD i = 0; i < table->dwNumEntries; i++) {
                const auto& row = table->table[i];
                if (row.dwOwningPid != want) continue;
                NetworkConnectionInfo conn;
                conn.protocol = "tcp6";
                conn.local_endpoint = ipv6_endpoint(row.ucLocalAddr, row.dwLocalPort);
                conn.remote_endpoint = row.dwState == MIB_TCP_STATE_LISTEN
                    ? "*:*" : ipv6_endpoint(row.ucRemoteAddr, row.dwRemotePort);
                conn.state = tcp_state_name(row.dwState);
                connections.push_back(std::move(conn));
            }
        }
    }

    // UDP v4
    size = 0;
    GetExtendedUdpTable(nullptr, &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    if (size > 0) {
        std::vector<char> buf(size);
        if (GetExtendedUdpTable(buf.data(), &size, FALSE, AF_INET,
                                UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
            const auto* table = reinterpret_cast<MIB_UDPTABLE_OWNER_PID*>(buf.data());
            for (DWORD i = 0; i < table->dwNumEntries; i++) {
                const auto& row = table->table[i];
                if (row.dwOwningPid != want) continue;
                NetworkConnectionInfo conn;
                conn.protocol = "udp";
                conn.local_endpoint = ipv4_endpoint(row.dwLocalAddr, row.dwLocalPort);
                conn.remote_endpoint = "*:*";
                conn.state = "-";
                connections.push_back(std::move(conn));
            }
        }
    }

    // UDP v6
    size = 0;
    GetExtendedUdpTable(nullptr, &size, FALSE, AF_INET6, UDP_TABLE_OWNER_PID, 0);
    if (size > 0) {
        std::vector<char> buf(size);
        if (GetExtendedUdpTable(buf.data(), &size, FALSE, AF_INET6,
                                UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
            const auto* table = reinterpret_cast<MIB_UDP6TABLE_OWNER_PID*>(buf.data());
            for (DWORD i = 0; i < table->dwNumEntries; i++) {
                const auto& row = table->table[i];
                if (row.dwOwningPid != want) continue;
                NetworkConnectionInfo conn;
                conn.protocol = "udp6";
                conn.local_endpoint = ipv6_endpoint(row.ucLocalAddr, row.dwLocalPort);
                conn.remote_endpoint = "*:*";
                conn.state = "-";
                connections.push_back(std::move(conn));
            }
        }
    }

    return connections;
}

std::vector<MemoryMapInfo> WindowsProcessDataProvider::get_memory_maps(int) {
    return {};  // Phase 2: VirtualQueryEx walk
}

std::vector<EnvironmentVariable> WindowsProcessDataProvider::get_environment_variables(int) {
    return {};  // Phase 2: remote PEB read
}

std::vector<LibraryInfo> WindowsProcessDataProvider::get_libraries(const int pid) {
    std::vector<LibraryInfo> libraries;

    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE,
                           static_cast<DWORD>(pid));
    if (!h) return libraries;

    HMODULE modules[1024];
    DWORD needed = 0;
    if (EnumProcessModulesEx(h, modules, sizeof(modules), &needed, LIST_MODULES_ALL)) {
        const size_t count = std::min<size_t>(needed / sizeof(HMODULE), 1024);
        for (size_t i = 0; i < count; i++) {
            wchar_t path[MAX_PATH];
            if (!GetModuleFileNameExW(h, modules[i], path, MAX_PATH)) continue;

            MODULEINFO mi{};
            GetModuleInformation(h, modules[i], &mi, sizeof(mi));

            LibraryInfo li;
            li.path = narrow(path);
            const size_t slash = li.path.find_last_of('\\');
            li.name = slash != std::string::npos ? li.path.substr(slash + 1) : li.path;
            li.base_addr = reinterpret_cast<uint64_t>(mi.lpBaseOfDll);
            li.base_address = std::format("{:x}", li.base_addr);
            li.total_size = static_cast<int64_t>(mi.SizeOfImage);
            li.is_executable = (i == 0);  // First module is the main executable
            libraries.push_back(std::move(li));
        }
    }
    CloseHandle(h);

    std::ranges::sort(libraries, [](const LibraryInfo& a, const LibraryInfo& b) {
        if (a.is_executable != b.is_executable) return a.is_executable;
        return a.base_addr < b.base_addr;
    });
    return libraries;
}

std::vector<ParseError> WindowsProcessDataProvider::get_recent_errors() {
    std::lock_guard lock(errors_mutex_);
    const auto cutoff = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    std::vector<ParseError> result;
    for (const auto& err : recent_errors_) {
        if (err.timestamp > cutoff) result.push_back(err);
    }
    return result;
}

void WindowsProcessDataProvider::clear_errors() {
    std::lock_guard lock(errors_mutex_);
    recent_errors_.clear();
}

} // namespace pex
