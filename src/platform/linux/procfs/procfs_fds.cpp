#include "../procfs_reader.hpp"
#include <filesystem>
#include <sys/stat.h>
#include <charconv>
#include <algorithm>

namespace fs = std::filesystem;

namespace pex {

std::vector<FileHandleInfo> ProcfsReader::get_file_handles(const int pid) {
    std::vector<FileHandleInfo> handles;
    const std::string fd_path = "/proc/" + std::to_string(pid) + "/fd";

    try {
        for (const auto& entry : fs::directory_iterator(fd_path)) {
            try {
                FileHandleInfo handle;

                const auto& name = entry.path().filename().string();
                if (auto [ptr, ec] = std::from_chars(name.data(), name.data() + name.size(), handle.fd); ec != std::errc{}) continue;

                handle.path = read_symlink(entry.path().string());

                if (handle.path.starts_with("socket:")) {
                    handle.type = "socket";
                } else if (handle.path.starts_with("pipe:")) {
                    handle.type = "pipe";
                } else if (handle.path.starts_with("anon_inode:")) {
                    handle.type = "anon_inode";
                } else if (handle.path.starts_with("/")) {
                    // Stat the /proc/<pid>/fd/N link, not the resolved path
                    // string: the link follows to the real inode even across
                    // mount namespaces and for deleted files, while the path
                    // may resolve to a different file (or nothing) in pex's
                    // own namespace.
                    struct stat st{};
                    if (stat(entry.path().c_str(), &st) == 0) {
                        if (S_ISREG(st.st_mode)) handle.type = "file";
                        else if (S_ISDIR(st.st_mode)) handle.type = "dir";
                        else if (S_ISCHR(st.st_mode)) handle.type = "char";
                        else if (S_ISBLK(st.st_mode)) handle.type = "block";
                        else if (S_ISFIFO(st.st_mode)) handle.type = "fifo";
                        else if (S_ISSOCK(st.st_mode)) handle.type = "socket";
                        else handle.type = "unknown";
                    } else {
                        handle.type = "file";
                    }
                } else {
                    handle.type = "unknown";
                }

                handles.push_back(std::move(handle));
            } catch (const std::exception&) {
                continue;
            }
        }
    } catch (const std::exception&) {
    }

    std::ranges::sort(handles, [](const auto& a, const auto& b) {
        return a.fd < b.fd;
    });

    return handles;
}

} // namespace pex
