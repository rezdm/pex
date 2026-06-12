#include "windows_system_data_provider.hpp"

#include <windows.h>
#include <winternl.h>

#include <ctime>
#include <format>
#include <thread>

namespace pex {

namespace {

uint64_t filetime_to_u64(const FILETIME& ft) {
    ULARGE_INTEGER v;
    v.LowPart = ft.dwLowDateTime;
    v.HighPart = ft.dwHighDateTime;
    return v.QuadPart;
}

// SystemProcessorPerformanceInformation (class 8) - documented in winternl.h
// as SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION but resolved at runtime to
// avoid a hard ntdll import dependency on older toolchains.
struct ProcessorPerf {
    LARGE_INTEGER IdleTime;
    LARGE_INTEGER KernelTime;   // Includes IdleTime
    LARGE_INTEGER UserTime;
    LARGE_INTEGER DpcTime;
    LARGE_INTEGER InterruptTime;
    ULONG InterruptCount;
};

using NtQuerySystemInformationFn = NTSTATUS(WINAPI*)(ULONG, PVOID, ULONG, PULONG);

NtQuerySystemInformationFn nt_query_system_information() {
    static const auto fn = []() -> NtQuerySystemInformationFn {
        if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
            return reinterpret_cast<NtQuerySystemInformationFn>(
                reinterpret_cast<void*>(GetProcAddress(ntdll, "NtQuerySystemInformation")));
        }
        return nullptr;
    }();
    return fn;
}

} // namespace

WindowsSystemDataProvider::WindowsSystemDataProvider() {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    processor_count_ = si.dwNumberOfProcessors > 0 ? si.dwNumberOfProcessors : 1;
}

CpuTimes WindowsSystemDataProvider::get_cpu_times() {
    CpuTimes times;
    FILETIME idle{}, kernel{}, user{};
    if (GetSystemTimes(&idle, &kernel, &user)) {
        const uint64_t idle_t = filetime_to_u64(idle);
        const uint64_t kernel_t = filetime_to_u64(kernel);  // Includes idle
        times.user = filetime_to_u64(user);
        times.system = kernel_t >= idle_t ? kernel_t - idle_t : 0;
        times.idle = idle_t;
    }
    return times;
}

std::vector<CpuTimes> WindowsSystemDataProvider::get_per_cpu_times() {
    std::vector<CpuTimes> result;
    get_per_cpu_times(result);
    return result;
}

void WindowsSystemDataProvider::get_per_cpu_times(std::vector<CpuTimes>& out) {
    out.clear();

    const auto query = nt_query_system_information();
    if (!query) return;

    std::vector<ProcessorPerf> perf(processor_count_);
    ULONG returned = 0;
    constexpr ULONG kSystemProcessorPerformanceInformation = 8;
    if (query(kSystemProcessorPerformanceInformation, perf.data(),
              static_cast<ULONG>(perf.size() * sizeof(ProcessorPerf)), &returned) != 0) {
        return;
    }

    const size_t count = returned / sizeof(ProcessorPerf);
    out.resize(count);
    for (size_t i = 0; i < count; i++) {
        const uint64_t idle = static_cast<uint64_t>(perf[i].IdleTime.QuadPart);
        const uint64_t kernel = static_cast<uint64_t>(perf[i].KernelTime.QuadPart);
        out[i].user = static_cast<uint64_t>(perf[i].UserTime.QuadPart);
        out[i].system = kernel >= idle ? kernel - idle : 0;
        out[i].idle = idle;
        out[i].irq = static_cast<uint64_t>(perf[i].InterruptTime.QuadPart);
    }
}

MemoryInfo WindowsSystemDataProvider::get_memory_info() {
    MemoryInfo info;
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        info.total = static_cast<int64_t>(ms.ullTotalPhys);
        info.available = static_cast<int64_t>(ms.ullAvailPhys);
        info.used = info.total - info.available;
    }
    return info;
}

SwapInfo WindowsSystemDataProvider::get_swap_info() {
    SwapInfo info;
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        // Commit charge beyond physical RAM ~ page file usage. TotalPageFile
        // is the commit limit (RAM + page files), so subtract physical RAM.
        const int64_t total = static_cast<int64_t>(ms.ullTotalPageFile) -
                              static_cast<int64_t>(ms.ullTotalPhys);
        const int64_t avail = static_cast<int64_t>(ms.ullAvailPageFile) -
                              static_cast<int64_t>(ms.ullAvailPhys);
        info.total = total > 0 ? total : 0;
        info.free = avail > 0 ? (avail < info.total ? avail : info.total) : 0;
        info.used = info.total - info.free;
    }
    return info;
}

LoadAverage WindowsSystemDataProvider::get_load_average() {
    // Windows has no load average; task counts are filled by DataStore from
    // the process snapshot, so leave everything at 0 here.
    return {};
}

UptimeInfo WindowsSystemDataProvider::get_uptime() {
    UptimeInfo info;
    info.uptime_seconds = GetTickCount64() / 1000;
    return info;
}

unsigned int WindowsSystemDataProvider::get_processor_count() const {
    return processor_count_;
}

long WindowsSystemDataProvider::get_clock_ticks_per_second() const {
    return 10'000'000;  // FILETIME unit: 100 ns
}

uint64_t WindowsSystemDataProvider::get_boot_time_ticks() const {
    return static_cast<uint64_t>(std::time(nullptr)) - GetTickCount64() / 1000;
}

std::string WindowsSystemDataProvider::get_system_info_string() const {
    // RtlGetVersion reports the true version (GetVersionEx lies without a
    // manifest). Resolved at runtime like NtQuerySystemInformation above.
    using RtlGetVersionFn = NTSTATUS(WINAPI*)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (const auto fn = reinterpret_cast<RtlGetVersionFn>(
                reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")))) {
            fn(&vi);
        }
    }
    return std::format("Windows {}.{} build {}", vi.dwMajorVersion, vi.dwMinorVersion,
                       vi.dwBuildNumber);
}

} // namespace pex
