#include "../procfs_reader.hpp"
#include "procfs_stat_parse.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <fcntl.h>
#include <unistd.h>
#include <format>
#include <charconv>
#include <cerrno>
#include <cstring>

namespace fs = std::filesystem;

namespace pex {

std::vector<ThreadInfo> ProcfsReader::get_threads(int pid) {
    std::vector<ThreadInfo> threads;
    std::string task_path = "/proc/" + std::to_string(pid) + "/task";
    std::string proc_path = "/proc/" + std::to_string(pid);

    // Build address range to library mapping from /proc/<pid>/maps
    struct AddressRange {
        uint64_t start;
        uint64_t end;
        std::string library;
    };
    std::vector<AddressRange> address_map;

    {
        std::ifstream maps_file(proc_path + "/maps");
        std::string line;
        while (std::getline(maps_file, line)) {
            std::istringstream iss(line);
            std::string address, perms, offset, dev, inode_str, pathname;
            iss >> address >> perms >> offset >> dev >> inode_str;
            std::getline(iss >> std::ws, pathname);

            if (pathname.find("(deleted)") != std::string::npos) continue;

            if (perms.size() >= 3 && perms[2] == 'x' && !pathname.empty() && pathname[0] == '/') {
                size_t dash = address.find('-');
                if (dash != std::string::npos) {
                    uint64_t start_addr = 0, end_addr = 0;
                    auto [ptr1, ec1] = std::from_chars(address.data(), address.data() + dash, start_addr, 16);
                    auto [ptr2, ec2] = std::from_chars(address.data() + dash + 1, address.data() + address.size(), end_addr, 16);

                    if (ec1 == std::errc{} && ec2 == std::errc{} && start_addr < end_addr) {
                        std::string lib_name = pathname;
                        if (size_t pos = pathname.rfind('/'); pos != std::string::npos) {
                            lib_name = pathname.substr(pos + 1);
                        }
                        address_map.push_back({start_addr, end_addr, lib_name});
                    }
                }
            }
        }
    }

    auto find_library = [&](const uint64_t addr) -> std::string {
        // address_map is sorted by start address (from /proc/pid/maps)
        auto it = std::upper_bound(address_map.begin(), address_map.end(), addr,
            [](uint64_t a, const AddressRange& r) { return a < r.start; });
        if (it != address_map.begin()) {
            --it;
            if (addr >= it->start && addr < it->end) {
                return it->library;
            }
        }
        return {};
    };

    try {
        for (const auto& entry : fs::directory_iterator(task_path)) {
            try {
                if (!entry.is_directory()) continue;

                const auto& name = entry.path().filename().string();
                int tid = 0;
                if (auto [ptr, ec] = std::from_chars(name.data(), name.data() + name.size(), tid); ec != std::errc{}) continue;

                ThreadInfo thread;
                thread.tid = tid;

                if (std::string stat = read_file(entry.path().string() + "/stat"); !stat.empty()) {
                    size_t comm_start = stat.find('(');
                    size_t comm_end = stat.rfind(')');

                    if (comm_start != std::string::npos && comm_end != std::string::npos && comm_end > comm_start) {
                        thread.name = stat.substr(comm_start + 1, comm_end - comm_start - 1);

                        const procfs::ThreadStatFields f = procfs::parse_thread_stat(stat);
                        thread.state = f.state;
                        thread.priority = f.priority;
                        thread.processor = f.processor;
                    } else {
                        thread.name = "???";
                        thread.state = '?';
                        thread.priority = 0;
                        thread.processor = -1;
                    }
                }

                if (std::string syscall = read_file(entry.path().string() + "/syscall"); !syscall.empty()) {
                    std::istringstream iss(syscall);
                    std::string syscall_num;
                    iss >> syscall_num;

                    if (syscall_num != "running" && !syscall_num.empty()) {
                        std::string hex;
                        int field_count = 0;
                        uint64_t pc = 0;

                        for (int i = 0; i < 7 && iss >> hex; i++) {
                            field_count++;
                        }

                        if (field_count == 7 && iss >> hex) {
                            if (hex.starts_with("0x") && hex.size() > 2) {
                                auto [ptr, ec] = std::from_chars(hex.data() + 2, hex.data() + hex.size(), pc, 16);
                                if (ec == std::errc{} && pc > 0) {
                                    thread.current_library = find_library(pc);
                                }
                            }
                        }
                    }
                }

                threads.push_back(std::move(thread));
            } catch (const std::exception&) {
                continue;
            }
        }
    } catch (const std::exception&) {
    }

    return threads;
}

std::string ProcfsReader::get_thread_stack(const int pid, const int tid) {
    const std::string path = "/proc/" + std::to_string(pid) + "/task/" + std::to_string(tid) + "/stack";

    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        if (errno == EACCES || errno == EPERM) {
            return "(requires root to read kernel stack)";
        }
        return std::format("(cannot read stack: {})", strerror(errno));
    }

    std::string result;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        result.append(buf, n);
    }
    close(fd);

    if (result.empty()) {
        return "(kernel stack requires root privileges)";
    }
    return result;
}

} // namespace pex
