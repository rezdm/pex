#pragma once

#include <string>
#include <chrono>
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

    // CPU usage (calculated by DataStore)
    double cpu_percent = 0.0;        // Per-core (100% = 1 core)
    double total_cpu_percent = 0.0;  // Overall (100% = all cores)

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
};

struct ThreadInfo {
    int tid = 0;                    // Thread ID (Linux: TID, Windows: thread ID)
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
    std::optional<int> inode;       // Socket inode (Linux-specific, optional)
};

struct MemoryMapInfo {
    std::string address;            // Address range (platform-specific format)
    std::string size;               // Human-readable size
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
    std::string base_address;       // First mapped address (hex string)
    int64_t total_size = 0;         // Sum of all mapped regions (bytes)
    int64_t resident_size = 0;      // RSS if available (bytes)
    bool is_executable = false;     // Main executable vs shared library
};

} // namespace pex
