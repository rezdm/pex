#include "windows_process_data_provider.hpp"
#include "../../core/format_utils.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winternl.h>   // PROCESS_BASIC_INFORMATION, PEB, UNICODE_STRING
#include <tlhelp32.h>
#include <psapi.h>
#include <iphlpapi.h>
#include <sddl.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cwchar>
#include <format>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

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

// ---- ntdll entry points ----------------------------------------------------
// ntdll is linked, but these functions/classes are not all in the mingw
// headers, so resolve them once at runtime (same pattern as RtlGetVersion).
using NtQuerySystemInformation_t = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
using NtQueryObject_t = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
using NtQueryInformationProcess_t = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

struct NtApi {
    NtQuerySystemInformation_t QuerySystemInformation = nullptr;
    NtQueryObject_t QueryObject = nullptr;
    NtQueryInformationProcess_t QueryInformationProcess = nullptr;
};

const NtApi& nt() {
    static const NtApi api = [] {
        NtApi a;
        if (HMODULE h = GetModuleHandleW(L"ntdll.dll")) {
            a.QuerySystemInformation = reinterpret_cast<NtQuerySystemInformation_t>(
                reinterpret_cast<void*>(GetProcAddress(h, "NtQuerySystemInformation")));
            a.QueryObject = reinterpret_cast<NtQueryObject_t>(
                reinterpret_cast<void*>(GetProcAddress(h, "NtQueryObject")));
            a.QueryInformationProcess = reinterpret_cast<NtQueryInformationProcess_t>(
                reinterpret_cast<void*>(GetProcAddress(h, "NtQueryInformationProcess")));
        }
        return a;
    }();
    return api;
}

constexpr ULONG kSystemExtendedHandleInformation = 64;
constexpr ULONG kObjectNameInformation = 1;
constexpr ULONG kObjectTypeInformation = 2;
constexpr LONG  kStatusInfoLengthMismatch = static_cast<LONG>(0xC0000004);
// Handles with exactly this granted access can deadlock NtQueryObject (the
// classic synchronous-pipe hang); skip the name query for them.
constexpr ULONG kHangProneAccess = 0x0012019F;

struct SYSTEM_HANDLE_ENTRY_EX {
    PVOID Object;
    ULONG_PTR UniqueProcessId;
    PVOID HandleValue;
    ULONG GrantedAccess;
    USHORT CreatorBackTraceIndex;
    USHORT ObjectTypeIndex;
    ULONG HandleAttributes;
    ULONG Reserved;
};
struct SYSTEM_HANDLE_INFORMATION_EX {
    ULONG_PTR NumberOfHandles;
    ULONG_PTR Reserved;
    SYSTEM_HANDLE_ENTRY_EX Handles[1];
};

// SystemProcessInformation returns every process (and its threads) in one
// call. winternl's SYSTEM_PROCESS_INFORMATION hides the times/IO fields behind
// reserved bytes, so use fully-specified structs matching the documented x64
// layout (natural alignment reproduces the kernel layout).
constexpr ULONG kSystemProcessInformation = 5;

struct SysThreadInfo {
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER CreateTime;
    ULONG         WaitTime;
    PVOID         StartAddress;
    struct { HANDLE UniqueProcess; HANDLE UniqueThread; } ClientId;
    LONG          Priority;
    LONG          BasePriority;
    ULONG         ContextSwitches;
    ULONG         ThreadState;   // KTHREAD_STATE
    ULONG         WaitReason;    // KWAIT_REASON
};

struct SysProcInfo {
    ULONG         NextEntryOffset;
    ULONG         NumberOfThreads;
    LARGE_INTEGER WorkingSetPrivateSize;
    ULONG         HardFaultCount;
    ULONG         NumberOfThreadsHighWatermark;
    ULONGLONG     CycleTime;
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER KernelTime;
    UNICODE_STRING ImageName;
    LONG          BasePriority;
    HANDLE        UniqueProcessId;
    HANDLE        InheritedFromUniqueProcessId;
    ULONG         HandleCount;
    ULONG         SessionId;
    ULONG_PTR     UniqueProcessKey;
    SIZE_T        PeakVirtualSize;
    SIZE_T        VirtualSize;
    ULONG         PageFaultCount;
    SIZE_T        PeakWorkingSetSize;
    SIZE_T        WorkingSetSize;
    SIZE_T        QuotaPeakPagedPoolUsage;
    SIZE_T        QuotaPagedPoolUsage;
    SIZE_T        QuotaPeakNonPagedPoolUsage;
    SIZE_T        QuotaNonPagedPoolUsage;
    SIZE_T        PagefileUsage;
    SIZE_T        PeakPagefileUsage;
    SIZE_T        PrivatePageCount;
    LARGE_INTEGER ReadOperationCount;
    LARGE_INTEGER WriteOperationCount;
    LARGE_INTEGER OtherOperationCount;
    LARGE_INTEGER ReadTransferCount;
    LARGE_INTEGER WriteTransferCount;
    LARGE_INTEGER OtherTransferCount;
    // SysThreadInfo Threads[NumberOfThreads] follows immediately.
};

// Derive a process state char from its threads' KTHREAD_STATE / KWAIT_REASON.
char derive_process_state(const SysProcInfo* p) {
    const auto* threads = reinterpret_cast<const SysThreadInfo*>(
        reinterpret_cast<const char*>(p) + sizeof(SysProcInfo));
    bool any_active = false;
    bool all_suspended = p->NumberOfThreads > 0;
    for (ULONG i = 0; i < p->NumberOfThreads; ++i) {
        const ULONG st = threads[i].ThreadState;
        // Running(2), Ready(1), Standby(3), DeferredReady(7) = on/for the CPU
        if (st == 2 || st == 1 || st == 3 || st == 7) any_active = true;
        // Suspended = Waiting(5) with WrSuspended(5)
        if (!(st == 5 && threads[i].WaitReason == 5)) all_suspended = false;
    }
    if (any_active) return 'R';
    if (all_suspended) return 'T';  // every thread suspended
    return 'S';                     // waiting / sleeping
}

// Convert an NT device path (\Device\HarddiskVolumeN\...) to a DOS path
// (C:\...) using the drive-letter -> device mapping; leaves it unchanged if
// no mapping matches (e.g. named pipes, sockets).
std::string nt_path_to_dos(const std::string& nt_path) {
    wchar_t drives[512];
    const DWORD n = GetLogicalDriveStringsW(511, drives);
    for (wchar_t* d = drives; *d && (d - drives) < static_cast<ptrdiff_t>(n); d += wcslen(d) + 1) {
        wchar_t drive[3] = { d[0], L':', L'\0' };
        wchar_t device[MAX_PATH];
        if (QueryDosDeviceW(drive, device, MAX_PATH) > 0) {
            const std::string dev = narrow(device);  // e.g. \Device\HarddiskVolume3
            if (!dev.empty() && nt_path.rfind(dev, 0) == 0) {
                return narrow(drive) + nt_path.substr(dev.size());
            }
        }
    }
    return nt_path;
}

// Query a UNICODE_STRING-headed object info class into a UTF-8 string.
std::string query_object_string(HANDLE dup, const ULONG info_class) {
    ULONG len = 0;
    nt().QueryObject(dup, info_class, nullptr, 0, &len);
    if (len == 0 || len > 64 * 1024) return {};
    std::vector<char> buf(len);
    if (nt().QueryObject(dup, info_class, buf.data(), len, &len) != 0) return {};
    const auto* us = reinterpret_cast<const UNICODE_STRING*>(buf.data());
    if (!us->Buffer || us->Length == 0) return {};
    // UNICODE_STRING is not guaranteed NUL-terminated; bound by Length.
    const std::wstring s(us->Buffer, us->Length / sizeof(wchar_t));
    return narrow(s.c_str());
}

// Read the real command line (with arguments) from the target's PEB. Needs a
// handle opened with PROCESS_VM_READ. CommandLine is a documented member of
// RTL_USER_PROCESS_PARAMETERS, so no undocumented offsets are involved.
std::string read_command_line(HANDLE h) {
    if (!nt().QueryInformationProcess) return {};

    PROCESS_BASIC_INFORMATION pbi{};
    ULONG ret = 0;
    if (nt().QueryInformationProcess(h, 0 /*ProcessBasicInformation*/, &pbi, sizeof(pbi), &ret) != 0
        || !pbi.PebBaseAddress) {
        return {};
    }
    PEB peb{};
    if (!ReadProcessMemory(h, pbi.PebBaseAddress, &peb, sizeof(peb), nullptr) ||
        !peb.ProcessParameters) {
        return {};
    }
    RTL_USER_PROCESS_PARAMETERS params{};
    if (!ReadProcessMemory(h, peb.ProcessParameters, &params, sizeof(params), nullptr)) {
        return {};
    }
    const UNICODE_STRING& cmd = params.CommandLine;
    if (!cmd.Buffer || cmd.Length == 0 || cmd.Length > 64 * 1024) return {};
    std::wstring w(cmd.Length / sizeof(wchar_t), L'\0');
    if (!ReadProcessMemory(h, cmd.Buffer, w.data(), cmd.Length, nullptr)) return {};
    return narrow(w.c_str());
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
            // Only cache a successful lookup: caching an empty result would
            // poison this SID's name permanently after one transient failure.
            if (!sid_key.empty() && !result.empty()) {
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

    // Real command line from the PEB. Reading it needs PROCESS_VM_READ, which
    // the caller's PROCESS_QUERY_LIMITED_INFORMATION handle lacks, so open a
    // separate best-effort handle — a failure here (e.g. an elevated process)
    // leaves command_line empty and get_all_processes falls back to the path.
    if (HANDLE hv = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE,
                                static_cast<DWORD>(info.pid))) {
        info.command_line = read_command_line(hv);
        CloseHandle(hv);
    }
}

std::vector<ProcessInfo> WindowsProcessDataProvider::get_all_processes(int64_t total_memory) {
    std::vector<ProcessInfo> processes;

    if (total_memory <= 0) {
        MEMORYSTATUSEX ms{};
        ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms)) total_memory = static_cast<int64_t>(ms.ullTotalPhys);
    }

    if (!nt().QuerySystemInformation) {
        add_error("get_all_processes", "NtQuerySystemInformation unavailable");
        return processes;
    }

    // One call returns every process with name/pid/ppid/threads/times/memory/IO
    // — no per-process handle opens (which dominated the old Toolhelp path).
    ULONG size = 512 * 1024;
    std::vector<char> buf;
    LONG status = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        buf.resize(size);
        ULONG needed = 0;
        status = nt().QuerySystemInformation(kSystemProcessInformation, buf.data(), size, &needed);
        if (status == 0) break;
        if (status == kStatusInfoLengthMismatch) {
            size = needed ? needed + (128 * 1024) : size * 2;
            continue;
        }
        add_error("get_all_processes", "NtQuerySystemInformation failed");
        return processes;
    }
    if (status != 0) return processes;

    std::unordered_set<int> current_pids;

    for (size_t offset = 0;;) {
        const auto* p = reinterpret_cast<const SysProcInfo*>(buf.data() + offset);
        const int pid = static_cast<int>(reinterpret_cast<uintptr_t>(p->UniqueProcessId));

        ProcessInfo info;
        info.pid = pid;
        info.parent_pid = static_cast<int>(reinterpret_cast<uintptr_t>(p->InheritedFromUniqueProcessId));
        info.thread_count = static_cast<int>(p->NumberOfThreads);
        info.state_char = derive_process_state(p);
        info.is_kernel_thread = (pid == 0 || pid == 4 || info.parent_pid == 4);

        if (p->ImageName.Buffer && p->ImageName.Length > 0) {
            info.name = narrow(std::wstring(p->ImageName.Buffer,
                                            p->ImageName.Length / sizeof(wchar_t)).c_str());
        } else {
            info.name = (pid == 0) ? "System Idle Process" : std::format("(pid {})", pid);
        }

        info.user_time = static_cast<uint64_t>(p->UserTime.QuadPart);      // 100 ns units
        info.kernel_time = static_cast<uint64_t>(p->KernelTime.QuadPart);
        info.resident_memory = static_cast<int64_t>(p->WorkingSetSize);
        info.virtual_memory = static_cast<int64_t>(p->VirtualSize);
        info.io_read_bytes = static_cast<uint64_t>(p->ReadTransferCount.QuadPart);
        info.io_write_bytes = static_cast<uint64_t>(p->WriteTransferCount.QuadPart);
        info.priority = p->BasePriority;
        if (total_memory > 0) {
            info.memory_percent = static_cast<double>(info.resident_memory) /
                                  static_cast<double>(total_memory) * 100.0;
        }
        if (const auto ct = static_cast<uint64_t>(p->CreateTime.QuadPart); ct > 0) {
            info.start_time = std::chrono::system_clock::from_time_t(
                static_cast<time_t>(ct / 10'000'000ULL - kFiletimeEpochDelta));
        }

        // Immutable strings (user, exe path, command line): fetch once per
        // process instance and cache, so steady state opens no handles.
        current_pids.insert(pid);
        const auto ct = static_cast<uint64_t>(p->CreateTime.QuadPart);
        auto& cache = proc_strings_[pid];
        if (cache.create_time != ct) {
            cache = {ct, {}, {}, {}};
            if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                       static_cast<DWORD>(pid))) {
                cache.user_name = get_username_for_pid(h);
                wchar_t path[MAX_PATH];
                DWORD path_len = MAX_PATH;
                if (QueryFullProcessImageNameW(h, 0, path, &path_len)) {
                    cache.executable_path = narrow(path);
                }
                CloseHandle(h);
            }
            if (HANDLE hv = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                                        FALSE, static_cast<DWORD>(pid))) {
                cache.command_line = read_command_line(hv);
                CloseHandle(hv);
            }
        }
        info.user_name = cache.user_name;
        info.executable_path = cache.executable_path;
        info.command_line = cache.command_line.empty()
            ? (info.executable_path.empty() ? info.name : info.executable_path)
            : cache.command_line;

        processes.push_back(std::move(info));

        if (p->NextEntryOffset == 0) break;
        offset += p->NextEntryOffset;
    }

    // Drop cache entries for processes that no longer exist.
    std::erase_if(proc_strings_, [&current_pids](const auto& e) {
        return !current_pids.contains(e.first);
    });

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

std::string WindowsProcessDataProvider::get_thread_stack(int, int64_t) {
    return "";  // Would need StackWalk64 + symbols (Phase 2+)
}

std::vector<FileHandleInfo> WindowsProcessDataProvider::get_file_handles(const int pid) {
    std::vector<FileHandleInfo> handles;
    if (!nt().QuerySystemInformation || !nt().QueryObject) return handles;

    // Enumerate every handle system-wide, then filter to this PID. The buffer
    // size fluctuates as handles open/close, so retry on length mismatch.
    ULONG size = 1 << 20;  // 1 MiB initial guess
    std::vector<char> buf;
    LONG status = 0;
    for (int attempt = 0; attempt < 6; ++attempt) {
        buf.resize(size);
        ULONG needed = 0;
        status = nt().QuerySystemInformation(kSystemExtendedHandleInformation,
                                             buf.data(), size, &needed);
        if (status == 0) break;
        if (status == kStatusInfoLengthMismatch) {
            size = needed ? needed + (64 * 1024) : size * 2;
            continue;
        }
        return handles;  // Unexpected error
    }
    if (status != 0) return handles;

    const auto* info = reinterpret_cast<const SYSTEM_HANDLE_INFORMATION_EX*>(buf.data());

    // A handle to the owning process, to duplicate its handles into ours.
    HANDLE target = OpenProcess(PROCESS_DUP_HANDLE, FALSE, static_cast<DWORD>(pid));
    if (!target) return handles;

    // Duplicate this pid's handles into our process (fast, never hangs).
    struct DupHandle { HANDLE h; ULONG access; int fd; };
    std::vector<DupHandle> dups;
    const HANDLE self = GetCurrentProcess();
    constexpr size_t kMaxHandles = 8192;
    for (ULONG_PTR i = 0; i < info->NumberOfHandles && dups.size() < kMaxHandles; ++i) {
        const SYSTEM_HANDLE_ENTRY_EX& e = info->Handles[i];
        if (e.UniqueProcessId != static_cast<ULONG_PTR>(pid)) continue;
        HANDLE dup = nullptr;
        if (DuplicateHandle(target, static_cast<HANDLE>(e.HandleValue), self, &dup,
                            0, FALSE, DUPLICATE_SAME_ACCESS) && dup) {
            dups.push_back({dup, e.GrantedAccess,
                            static_cast<int>(reinterpret_cast<uintptr_t>(e.HandleValue))});
        }
    }
    CloseHandle(target);
    if (dups.empty()) return handles;

    // Resolve type/name on a worker thread with a total timeout. NtQueryObject
    // can hang indefinitely on some synchronous handles, and get_file_handles
    // runs on the UI thread when the details panel refreshes — a stuck query
    // would freeze the whole app (which is exactly what happened). On timeout
    // we return what completed; the detached worker keeps draining and closing
    // its own dups (the shared state outlives it via the shared_ptr) while the
    // UI stays responsive. A genuinely stuck query leaks that one thread +
    // handle, which is rare and bounded.
    struct QueryState {
        std::mutex mtx;
        std::condition_variable cv;
        std::vector<FileHandleInfo> results;
        bool done = false;
    };
    auto state = std::make_shared<QueryState>();

    std::thread([state, work = std::move(dups)]() {
        for (const auto& d : work) {
            const std::string type = query_object_string(d.h, kObjectTypeInformation);
            if (type == "File") {
                std::string path;
                if (d.access != kHangProneAccess) {
                    path = nt_path_to_dos(query_object_string(d.h, kObjectNameInformation));
                }
                FileHandleInfo fh;
                fh.fd = d.fd;
                if (path.rfind("\\Device\\NamedPipe", 0) == 0) fh.type = "pipe";
                else if (path.rfind("\\Device\\Afd", 0) == 0)  fh.type = "socket";
                else                                            fh.type = "file";
                fh.path = path.empty() ? "(name unavailable)" : path;
                std::lock_guard lock(state->mtx);
                state->results.push_back(std::move(fh));
            }
            CloseHandle(d.h);
        }
        std::lock_guard lock(state->mtx);
        state->done = true;
        state->cv.notify_one();
    }).detach();

    {
        std::unique_lock lock(state->mtx);
        state->cv.wait_for(lock, std::chrono::milliseconds(750), [&] { return state->done; });
        handles = state->results;  // whatever is ready (copied under the lock)
    }

    std::ranges::sort(handles, [](const FileHandleInfo& a, const FileHandleInfo& b) {
        return a.fd < b.fd;
    });
    return handles;
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

std::vector<MemoryMapInfo> WindowsProcessDataProvider::get_memory_maps(const int pid) {
    std::vector<MemoryMapInfo> maps;

    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE,
                           static_cast<DWORD>(pid));
    if (!h) return maps;

    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t addr = 0;
    while (VirtualQueryEx(h, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == sizeof(mbi)) {
        const auto base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const uintptr_t next = base + mbi.RegionSize;

        if (mbi.State == MEM_COMMIT) {
            MemoryMapInfo m;
            m.address = std::format("{:016x}-{:016x}", base, next);
            m.size_bytes = static_cast<uint64_t>(mbi.RegionSize);
            m.size = format_bytes(static_cast<int64_t>(m.size_bytes), false);

            // Page protection -> rwx string (low byte holds the base protection;
            // PAGE_GUARD/PAGE_NOCACHE live in the high bits and are ignored).
            const DWORD p = mbi.Protect & 0xFF;
            const bool r = p & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
            const bool w = p & (PAGE_READWRITE | PAGE_WRITECOPY |
                                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
            const bool x = p & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
            std::string perms;
            perms += r ? 'r' : '-';
            perms += w ? 'w' : '-';
            perms += x ? 'x' : '-';
            perms += (mbi.Type == MEM_PRIVATE) ? 'p' : 's';
            m.permissions = perms;

            wchar_t name[MAX_PATH];
            if (GetMappedFileNameW(h, mbi.BaseAddress, name, MAX_PATH) > 0) {
                m.pathname = nt_path_to_dos(narrow(name));
            } else if (mbi.Type == MEM_IMAGE) {
                m.pathname = "[image]";
            } else if (mbi.Type == MEM_MAPPED) {
                m.pathname = "[mapped]";
            } else {
                m.pathname = "[private]";
            }

            maps.push_back(std::move(m));
        }

        if (next <= addr) break;  // No forward progress / address wrap
        addr = next;
    }
    CloseHandle(h);
    return maps;
}

std::vector<EnvironmentVariable> WindowsProcessDataProvider::get_environment_variables(const int pid) {
    std::vector<EnvironmentVariable> env;
    if (!nt().QueryInformationProcess) return env;

    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE,
                           static_cast<DWORD>(pid));
    if (!h) return env;

    // Walk PEB -> ProcessParameters -> Environment in the target's address space.
    PROCESS_BASIC_INFORMATION pbi{};
    ULONG ret = 0;
    if (nt().QueryInformationProcess(h, 0 /*ProcessBasicInformation*/, &pbi, sizeof(pbi), &ret) != 0
        || !pbi.PebBaseAddress) {
        CloseHandle(h);
        return env;
    }

    PEB peb{};
    if (!ReadProcessMemory(h, pbi.PebBaseAddress, &peb, sizeof(peb), nullptr) ||
        !peb.ProcessParameters) {
        CloseHandle(h);
        return env;
    }

    // The Environment pointer lives past the documented head of
    // RTL_USER_PROCESS_PARAMETERS (offset 0x80 on x64). winternl's struct omits
    // it, so read the pointer directly. (64-bit pex reading a 64-bit process;
    // 32-bit targets under WOW64 are best-effort.)
    constexpr uintptr_t kEnvPtrOffset = 0x80;
    void* env_addr = nullptr;
    const auto params = reinterpret_cast<uintptr_t>(peb.ProcessParameters);
    if (ReadProcessMemory(h, reinterpret_cast<LPCVOID>(params + kEnvPtrOffset),
                          &env_addr, sizeof(env_addr), nullptr) && env_addr) {
        // Read the block (NUL-separated NAME=VALUE entries, double-NUL end) in
        // chunks until the terminator or a sane cap.
        std::wstring block;
        constexpr size_t kCapWChars = 128 * 1024;
        auto cur = reinterpret_cast<uintptr_t>(env_addr);
        bool done = false;
        while (!done && block.size() < kCapWChars) {
            wchar_t buf[2048];
            SIZE_T got = 0;
            if (!ReadProcessMemory(h, reinterpret_cast<LPCVOID>(cur), buf, sizeof(buf), &got) ||
                got < sizeof(wchar_t)) {
                break;
            }
            const size_t count = got / sizeof(wchar_t);
            for (size_t i = 0; i < count; ++i) {
                if (buf[i] == L'\0' && !block.empty() && block.back() == L'\0') { done = true; break; }
                block.push_back(buf[i]);
            }
            cur += count * sizeof(wchar_t);
        }

        for (size_t start = 0; start < block.size();) {
            size_t nul = block.find(L'\0', start);
            if (nul == std::wstring::npos) nul = block.size();
            if (nul > start) {
                const std::wstring entry = block.substr(start, nul - start);
                // Skip Windows' "=C:=..." drive-cwd pseudo-vars (name is empty).
                if (const size_t eq = entry.find(L'='); eq != std::wstring::npos && eq > 0) {
                    EnvironmentVariable ev;
                    ev.name = narrow(entry.substr(0, eq).c_str());
                    ev.value = narrow(entry.substr(eq + 1).c_str());
                    env.push_back(std::move(ev));
                }
            }
            start = nul + 1;
        }
    }
    CloseHandle(h);

    std::ranges::sort(env, [](const EnvironmentVariable& a, const EnvironmentVariable& b) {
        return a.name < b.name;
    });
    return env;
}

std::vector<LibraryInfo> WindowsProcessDataProvider::get_libraries(const int pid) {
    std::vector<LibraryInfo> libraries;

    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE,
                           static_cast<DWORD>(pid));
    if (!h) return libraries;

    // Size the module array from what the process actually has: a fixed cap
    // would silently drop modules from a process that loads more than it.
    std::vector<HMODULE> modules(1024);
    DWORD needed = 0;
    if (EnumProcessModulesEx(h, modules.data(),
                             static_cast<DWORD>(modules.size() * sizeof(HMODULE)),
                             &needed, LIST_MODULES_ALL)) {
        if (needed > modules.size() * sizeof(HMODULE)) {
            modules.resize(needed / sizeof(HMODULE));
            EnumProcessModulesEx(h, modules.data(),
                                 static_cast<DWORD>(modules.size() * sizeof(HMODULE)),
                                 &needed, LIST_MODULES_ALL);
        }
        const size_t count = std::min<size_t>(needed / sizeof(HMODULE), modules.size());
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
