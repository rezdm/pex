#include "../procfs_reader.hpp"
#include <fstream>
#include <sstream>
#include <charconv>
#include <format>

namespace pex {

std::vector<MemoryMapInfo> ProcfsReader::get_memory_maps(int pid) {
    std::vector<MemoryMapInfo> maps;
    std::string maps_path = "/proc/" + std::to_string(pid) + "/maps";

    std::ifstream file(maps_path);
    if (!file) return maps;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string address, perms, offset, dev, inode;
        std::string pathname;

        iss >> address >> perms >> offset >> dev >> inode;
        std::getline(iss >> std::ws, pathname);

        if (pathname.find("(deleted)") != std::string::npos) continue;

        size_t dash = address.find('-');
        uint64_t start_addr = 0, end_addr = 0;
        if (dash != std::string::npos) {
            auto [ptr1, ec1] = std::from_chars(address.data(), address.data() + dash, start_addr, 16);
            auto [ptr2, ec2] = std::from_chars(address.data() + dash + 1, address.data() + address.size(), end_addr, 16);
            if (ec1 != std::errc{} || ec2 != std::errc{}) {
                add_error(std::format("PID {}: malformed address in maps: {}", pid, address));
                continue;
            }
        }
        uint64_t size = end_addr - start_addr;

        MemoryMapInfo map;
        map.address = address;
        map.permissions = perms;
        map.pathname = pathname;

        if (size < 1024) {
            map.size = std::to_string(size) + " B";
        } else if (size < 1024 * 1024) {
            map.size = std::format("{:.1f} KB", size / 1024.0);
        } else if (size < 1024 * 1024 * 1024) {
            map.size = std::format("{:.1f} MB", size / (1024.0 * 1024));
        } else {
            map.size = std::format("{:.2f} GB", size / (1024.0 * 1024 * 1024));
        }

        maps.push_back(std::move(map));
    }

    return maps;
}

} // namespace pex
