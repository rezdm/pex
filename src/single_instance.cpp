#include "single_instance.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>

namespace pex {

SingleInstance::SingleInstance() = default;

SingleInstance::~SingleInstance() {
    running_ = false;

    if (server_fd_ >= 0) {
        // Close the server socket to unblock accept()
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }

    if (listener_.joinable()) {
        listener_.join();
    }

    if (!socket_path_.empty()) {
        unlink(socket_path_.c_str());
    }
}

std::string SingleInstance::get_socket_path() {
    if (const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR")) {
        return std::string(runtime_dir) + "/pex.sock";
    }
    // Fallback: use /tmp with UID
    return "/tmp/pex-" + std::to_string(getuid()) + ".sock";
}

bool SingleInstance::try_become_primary() {
    socket_path_ = get_socket_path();

    // Try to connect to existing instance
    int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd < 0) {
        return true; // Can't create socket, assume we're primary
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(client_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        // Connected to existing instance - send raise command
        const char* cmd = "RAISE\n";
        ssize_t written = write(client_fd, cmd, strlen(cmd));
        (void)written; // Ignore result
        close(client_fd);
        return false; // Another instance is running
    }
    close(client_fd);

    // No existing instance - become the server
    // First, remove any stale socket file
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
    raise_callback_ = std::move(callback);
}

void SingleInstance::listen_thread() const {
    while (running_) {
        int client_fd = accept(server_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            if (!running_) {
                break; // Server was shut down
            }
            continue;
        }

        // Read command
        char buffer[64];
        ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
        close(client_fd);

        if (n > 0) {
            buffer[n] = '\0';
            if (strncmp(buffer, "RAISE", 5) == 0) {
                if (raise_callback_) {
                    raise_callback_();
                }
            }
        }
    }
}

} // namespace pex
