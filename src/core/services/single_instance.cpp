#include "single_instance.hpp"

#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace pex {

SingleInstance::SingleInstance() = default;

SingleInstance::~SingleInstance() {
    running_ = false;

    // Wake the listener out of accept() with shutdown(), but do NOT close the
    // fd yet: closing while the listener thread may still be using it races
    // with fd reuse (another thread could be handed the same fd number).
    // Only close after the thread has been joined.
    if (server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR);
    }

    if (listener_.joinable()) {
        listener_.join();
    }

    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }

    if (!socket_path_.empty()) {
        unlink(socket_path_.c_str());
    }
}

std::string SingleInstance::get_socket_path() {
    constexpr size_t max_path = sizeof(sockaddr_un{}.sun_path) - 1;

    if (const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR")) {
        std::string path = std::string(runtime_dir) + "/pex.sock";
        if (path.size() <= max_path) {
            return path;
        }
        // XDG_RUNTIME_DIR path too long, fall through to /tmp
    }
    // Fallback: a private 0700 per-user directory under /tmp. A bare
    // /tmp/pex-<uid>.sock is squattable: the name is predictable, and a
    // squatter accepting connections makes every new pex exit believing an
    // instance is already running. The directory is verified (ours, a real
    // directory, not group/world-accessible) before use; on any doubt we
    // return empty and the caller skips single-instance handling entirely.
    const std::string dir = "/tmp/pex-" + std::to_string(getuid());
    if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
        return {};
    }
    // Open the directory itself (O_NOFOLLOW rejects a symlink swapped in for
    // it) and verify/repair through the fd, so an attacker on a non-sticky
    // /tmp cannot race a symlink between the check and a path-based chmod and
    // have us chmod an arbitrary target.
    const int dfd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (dfd < 0) {
        return {};  // Not a real directory we own, or a symlink was swapped in
    }
    struct stat st{};
    if (fstat(dfd, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid()) {
        close(dfd);
        return {};  // Foreign-owned: do not trust it
    }
    if ((st.st_mode & 0077) != 0 && fchmod(dfd, 0700) != 0) {
        close(dfd);
        return {};
    }
    close(dfd);
    return dir + "/pex.sock";
}

bool SingleInstance::try_become_primary() {
    socket_path_ = get_socket_path();
    if (socket_path_.empty()) {
        return true;  // No trustworthy socket location: run as primary
    }

    // Try to connect to existing instance
    const int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd < 0) {
        return true; // Can't create socket, assume we're primary
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(client_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        // Connected to existing instance - send raise command
        const char* cmd = "RAISE\n";
        const ssize_t written = write(client_fd, cmd, strlen(cmd));
        (void)written; // Ignore result
        close(client_fd);
        return false; // Another instance is running
    }
    close(client_fd);

    // No existing instance - become the server.
    // First, remove any stale socket file. NOTE: there is an inherent TOCTOU
    // here — two instances started at the exact same moment can both fail the
    // connect, both unlink+bind, and both become primary. Acceptable for a
    // desktop tool; fixing it would require an flock-based lock file.
    unlink(socket_path_.c_str());

    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        return true; // Can't create server socket, proceed anyway
    }

    if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(server_fd_);
        server_fd_ = -1;
        return true; // Can't bind, proceed anyway
    }

    if (listen(server_fd_, 5) < 0) {
        close(server_fd_);
        server_fd_ = -1;
        unlink(socket_path_.c_str());
        return true; // Can't listen, proceed anyway
    }

    // Start listener thread
    running_ = true;
    listener_ = std::thread(&SingleInstance::listen_thread, this);

    return true;
}

void SingleInstance::set_raise_callback(std::function<void()> callback) {
    std::lock_guard lock(callback_mutex_);
    raise_callback_ = std::move(callback);
}

void SingleInstance::listen_thread() const {
    while (running_) {
        const int client_fd = accept(server_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            if (!running_) {
                break; // Server was shut down
            }
            continue;
        }

        // Read command
        char buffer[64];
        const ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
        close(client_fd);

        if (n > 0) {
            buffer[n] = '\0';
            if (strncmp(buffer, "RAISE", 5) == 0) {
                std::function<void()> cb;
                {
                    std::lock_guard lock(callback_mutex_);
                    cb = raise_callback_;
                }
                if (cb) {
                    cb();
                }
            }
        }
    }
}

} // namespace pex
