#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>

namespace pex {

class SingleInstance {
public:
    SingleInstance();
    ~SingleInstance();

    // Returns true if this is the first instance
    // If another instance exists, sends raise signal and returns false
    bool try_become_primary();

    // Set callback for when another instance requests focus
    void set_raise_callback(std::function<void()> callback);

private:
    void listen_thread() const;

    static std::string get_socket_path();

    int server_fd_ = -1;
    std::string socket_path_;
    std::function<void()> raise_callback_;
    std::thread listener_;
    std::atomic<bool> running_{false};
};

} // namespace pex
