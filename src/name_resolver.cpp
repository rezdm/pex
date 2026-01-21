#include "name_resolver.hpp"
#include <fstream>
#include <sstream>
#include <netdb.h>
#include <arpa/inet.h>

namespace pex {

NameResolver::NameResolver() {
    load_services();
}

NameResolver::~NameResolver() {
    stop();
}

void NameResolver::start() {
    if (running_) return;
    running_ = true;
    resolver_thread_ = std::thread(&NameResolver::resolver_thread, this);
}

void NameResolver::stop() {
    if (!running_) return;
    running_ = false;
    queue_cv_.notify_all();
    if (resolver_thread_.joinable()) {
        resolver_thread_.join();
    }
}

void NameResolver::set_on_resolved(std::function<void()> callback) {
    on_resolved_ = std::move(callback);
}

void NameResolver::load_services() {
    std::ifstream file("/etc/services");
    if (!file) return;

    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string name, port_proto;
        iss >> name >> port_proto;

        if (name.empty() || port_proto.empty()) continue;

        // port_proto is like "80/tcp" or "53/udp"
        size_t slash = port_proto.find('/');
        if (slash == std::string::npos) continue;

        std::string port_str = port_proto.substr(0, slash);
        std::string proto = port_proto.substr(slash + 1);

        // Store as "port/protocol" -> name
        services_cache_[port_str + "/" + proto] = name;
    }
}

std::string NameResolver::get_service_name(uint16_t port, const std::string& protocol) {
    std::string key = std::to_string(port) + "/" + protocol;
    if (auto it = services_cache_.find(key); it != services_cache_.end()) {
        return it->second;
    }
    return {};
}

std::string NameResolver::get_hostname(const std::string& ip) {
    if (ip.empty() || ip == "0.0.0.0" || ip == "[::]") {
        return "*";
    }

    {
        std::lock_guard lock(dns_mutex_);
        if (auto it = dns_cache_.find(ip); it != dns_cache_.end()) {
            if (it->second == kResolving) {
                return {};  // Still resolving
            }
            if (it->second == kNotFound) {
                return {};  // Resolution failed, return empty
            }
            return it->second;  // Return cached hostname
        }

        // Mark as resolving and queue for resolution
        dns_cache_[ip] = kResolving;
    }

    {
        std::lock_guard lock(queue_mutex_);
        resolve_queue_.push(ip);
    }
    queue_cv_.notify_one();

    return {};  // Will be resolved asynchronously
}

void NameResolver::resolver_thread() {
    while (running_) {
        std::string ip;

        {
            std::unique_lock lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !resolve_queue_.empty() || !running_;
            });

            if (!running_) break;

            if (!resolve_queue_.empty()) {
                ip = resolve_queue_.front();
                resolve_queue_.pop();
            }
        }

        if (ip.empty()) continue;

        // Perform DNS reverse lookup
        std::string hostname;

        // Check if IPv4 or IPv6
        struct sockaddr_in sa4{};
        struct sockaddr_in6 sa6{};
        char host[NI_MAXHOST];

        if (inet_pton(AF_INET, ip.c_str(), &sa4.sin_addr) == 1) {
            // IPv4
            sa4.sin_family = AF_INET;
            if (getnameinfo(reinterpret_cast<sockaddr*>(&sa4), sizeof(sa4),
                           host, sizeof(host), nullptr, 0, NI_NAMEREQD) == 0) {
                hostname = host;
            }
        } else if (inet_pton(AF_INET6, ip.c_str(), &sa6.sin6_addr) == 1) {
            // IPv6
            sa6.sin6_family = AF_INET6;
            if (getnameinfo(reinterpret_cast<sockaddr*>(&sa6), sizeof(sa6),
                           host, sizeof(host), nullptr, 0, NI_NAMEREQD) == 0) {
                hostname = host;
            }
        }

        // Update cache
        {
            std::lock_guard lock(dns_mutex_);
            dns_cache_[ip] = hostname.empty() ? kNotFound : hostname;
        }

        // Notify that resolution completed
        if (on_resolved_) {
            on_resolved_();
        }
    }
}

} // namespace pex
