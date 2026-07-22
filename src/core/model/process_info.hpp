#pragma once

#include <string>
#include <chrono>
#include <cstdint>
#include <optional>

namespace pex {

// Process state characters (platform-neutral meanings):
// 'R' = Running/Runnable
// 'S' = Sleeping (interruptible)
// 'D' = Disk sleep (uninterruptible)
// 'Z' = Zombie
// 'T' = Stopped (signal or debugger)
// 'I' = Idle
// '?' = Unknown

struct ProcessInfo {
    // Core fields (available on all platforms)
    int pid = 0;
    int parent_pid = 0;
    std::string name;
    std::string command_line;
    std::string executable_path;
    char state_char = '?';
    std::string user_name;
    bool is_kernel_thread = false;  // Linux: PF_KTHREAD, FreeBSD: P_KPROC, Solaris: SSYS

    // CPU usage (calculated by DataStore)
    double cpu_percent = 0.0;        // Per-core (100% = 1 core)
    double total_cpu_percent = 0.0;  // Overall (100% = all cores)
    double cpu_user_percent = 0.0;   // User-mode share of all cores (sums with kernel to total_cpu_percent)
    double cpu_kernel_percent = 0.0; // Kernel-mode share of all cores

    // Memory (in bytes)
    int64_t resident_memory = 0;     // Working set / RSS
    int64_t virtual_memory = 0;      // Virtual memory size
    double memory_percent = 0.0;     // Percentage of total system memory

    int thread_count = 0;
    int priority = 0;
    std::chrono::system_clock::time_point start_time;

    // CPU time counters (platform-specific units, used for delta calculations)
    // Linux: jiffies (clock ticks), Windows: 100ns intervals, macOS: mach time
    // These are cumulative counters - only the delta between snapshots is meaningful
    uint64_t user_time = 0;
    uint64_t kernel_time = 0;

    // Cumulative storage I/O counters in bytes (Linux: /proc/<pid>/io
    // read_bytes/write_bytes). 0 = unavailable on this platform.
    uint64_t io_read_bytes = 0;
    uint64_t io_write_bytes = 0;

    // I/O rates in bytes/sec (calculated by DataStore from counter deltas)
    double io_read_rate = 0.0;
    double io_write_rate = 0.0;
};

struct ThreadInfo {
    int64_t tid = 0;                // Thread ID (macOS Mach thread IDs are 64-bit; narrowing to int made them negative — issue #95)
    std::string name;               // Thread name if available
    char state = '?';               // Same state chars as ProcessInfo
    int priority = 0;
    int processor = -1;             // Last CPU this thread ran on (-1 = unknown)
    std::string stack;              // Kernel stack trace (may be empty if unavailable)
    std::string current_library;    // Library where thread is currently executing
};

struct FileHandleInfo {
    int fd = 0;                     // File descriptor number
    std::string type;               // "file", "socket", "pipe", "device", etc.
    std::string path;               // Path or description
};

struct NetworkConnectionInfo {
    std::string protocol;           // "tcp", "tcp6", "udp", "udp6"
    std::string local_endpoint;     // "ip:port" format
    std::string remote_endpoint;    // "ip:port" format (may be "*:*" for listening)
    std::string state;              // "LISTEN", "ESTABLISHED", "TIME_WAIT", etc.
    std::optional<uint64_t> inode;  // Socket inode (Linux-specific, optional)
};

struct MemoryMapInfo {
    std::string address;            // Address range (platform-specific format)
    uint64_t size_bytes = 0;        // Region size in bytes (authoritative value)
    std::string size;               // Human-readable size (for display only)
    std::string permissions;        // "rwxp" style or equivalent
    std::string pathname;           // Mapped file path or "[heap]", "[stack]", etc.
};

struct EnvironmentVariable {
    std::string name;
    std::string value;
};

struct LibraryInfo {
    std::string path;               // Full path to the library
    std::string name;               // Just the filename
    uint64_t base_addr = 0;         // First mapped address (numeric, for sorting)
    std::string base_address;       // First mapped address (hex string, for display)
    int64_t total_size = 0;         // Sum of all mapped regions (bytes)
    int64_t resident_size = 0;      // RSS if available (bytes, 0 = unknown)
    bool is_executable = false;     // Main executable vs shared library
};

} // namespace pex
