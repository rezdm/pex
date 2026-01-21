#pragma once

#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <functional>

namespace pex {

class NameResolver {
public:
    NameResolver();
    ~NameResolver();

    // Start/stop the background resolver thread
    void start();
    void stop();

    // Request async resolution of an IP address
    // Returns cached result immediately if available, empty string otherwise
    // Resolution happens in background, call get_hostname again later
    std::string get_hostname(const std::string& ip);

    // Get service name for a port (synchronous, from /etc/services cache)
    std::string get_service_name(uint16_t port, const std::string& protocol);

    // Set callback for when a resolution completes (to trigger UI refresh)
    void set_on_resolved(std::function<void()> callback);

private:
    void resolver_thread();
    void load_services();

    // DNS cache: IP -> hostname (empty string means "resolving", special value means "not found")
    std::map<std::string, std::string> dns_cache_;
    std::mutex dns_mutex_;

    // Services cache: "port/protocol" -> service name
    std::map<std::string, std::string> services_cache_;

    // Resolution queue
    std::queue<std::string> resolve_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    // Thread management
    std::thread resolver_thread_;
    std::atomic<bool> running_{false};

    // Callback when resolution completes
    std::function<void()> on_resolved_;

    static constexpr const char* kResolving = "\x01";  // Sentinel for "in progress"
    static constexpr const char* kNotFound = "\x02";   // Sentinel for "resolution failed"
};

} // namespace pex
