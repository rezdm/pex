#pragma once

#include <string>
#include <chrono>

namespace pex {

struct ProcessInfo {
    int pid = 0;
    int parent_pid = 0;
    std::string name;
    std::string command_line;
    std::string executable_path;
    char state_char = '?';
    std::string user_name;
    double cpu_percent = 0.0;        // Per-core (100% = 1 core)
    double total_cpu_percent = 0.0;  // Overall (100% = all cores)
    int64_t resident_memory = 0;
    int64_t virtual_memory = 0;
    double memory_percent = 0.0;
    int thread_count = 0;
    int priority = 0;
    std::chrono::system_clock::time_point start_time;
    uint64_t user_time = 0;
    uint64_t kernel_time = 0;
    uint64_t start_time_ticks = 0;
};

struct ThreadInfo {
    int tid = 0;
    std::string name;
    char state = '?';
    int priority = 0;
    int processor = -1;
    std::string stack;
};

struct FileHandleInfo {
    int fd = 0;
    std::string type;
    std::string path;
};

struct NetworkConnectionInfo {
    std::string protocol;
    std::string local_endpoint;
    std::string remote_endpoint;
    std::string state;
    int inode = 0;
};

struct MemoryMapInfo {
    std::string address;
    std::string size;
    std::string permissions;
    std::string pathname;
};

struct EnvironmentVariable {
    std::string name;
    std::string value;
};

} // namespace pex
