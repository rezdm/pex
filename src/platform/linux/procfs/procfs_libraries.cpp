#include "../procfs_reader.hpp"
#include <fstream>
#include <sstream>
#include <map>
#include <charconv>
#include <format>
#include <algorithm>
#include <ranges>

namespace pex {

std::vector<LibraryInfo> ProcfsReader::get_libraries(const int pid) {
    std::vector<LibraryInfo> libraries;
    const std::string maps_path = "/proc/" + std::to_string(pid) + "/maps";
    const std::string exe_path = read_symlink("/proc/" + std::to_string(pid) + "/exe");

    std::ifstream file(maps_path);
    if (!file) return libraries;

    std::map<std::string, LibraryInfo> lib_map;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string address, perms, offset, dev, inode_str;
        std::string pathname;

        iss >> address >> perms >> offset >> dev >> inode_str;
        std::getline(iss >> std::ws, pathname);

        if (pathname.empty() || pathname[0] != '/') continue;
        if (pathname.starts_with("/dev/") || pathname.starts_with("/memfd:")) continue;
        if (pathname.find("(deleted)") != std::string::npos) continue;

        bool is_library = pathname.find(".so") != std::string::npos;
        bool is_main_exe = (pathname == exe_path);

        if (!is_library && !is_main_exe) continue;

        size_t dash = address.find('-');
        uint64_t start_addr = 0, end_addr = 0;
        if (dash != std::string::npos) {
            auto [ptr1, ec1] = std::from_chars(address.data(), address.data() + dash, start_addr, 16);
            auto [ptr2, ec2] = std::from_chars(address.data() + dash + 1, address.data() + address.size(), end_addr, 16);
            if (ec1 != std::errc{} || ec2 != std::errc{}) {
                continue;
            }
        }
        uint64_t size = end_addr - start_addr;

        auto& lib = lib_map[pathname];
        if (lib.path.empty()) {
            lib.path = pathname;
            if (size_t pos = pathname.rfind('/'); pos != std::string::npos) {
                lib.name = pathname.substr(pos + 1);
            } else {
                lib.name = pathname;
            }
            lib.base_address = std::format("{:x}", start_addr);
            lib.is_executable = is_main_exe;
        }
        lib.total_size += static_cast<int64_t>(size);
    }

    libraries.reserve(lib_map.size());
    for (auto &lib: lib_map | std::views::values) {
        libraries.push_back(std::move(lib));
    }

    std::ranges::sort(libraries, [](const auto& a, const auto& b) {
        if (a.is_executable != b.is_executable) return a.is_executable > b.is_executable;
        return a.name < b.name;
    });

    return libraries;
}

} // namespace pex
