#include "name_resolver.hpp"
#include <fstream>
#include <sstream>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

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

// running_ participates in the queue_cv_ wait predicate, so it must be
// cleared while holding queue_mutex_ — otherwise the resolver thread can
// evaluate the predicate, get preempted, miss the notify, and sleep a full
// kNotifyInterval (same lost-wakeup pattern fixed in DataStore::stop()).
void NameResolver::stop() {
    {
        std::lock_guard lock(queue_mutex_);
        if (!running_.exchange(false)) return;
    }
    queue_cv_.notify_all();
    if (resolver_thread_.joinable()) {
        resolver_thread_.join();
    }
}

void NameResolver::set_on_resolved(std::function<void()> callback) {
    std::lock_guard lock(callback_mutex_);
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

std::string NameResolver::get_service_name(const uint16_t port, const std::string& protocol) {
    const std::string key = std::to_string(port) + "/" + protocol;
    if (const auto it = services_cache_.find(key); it != services_cache_.end()) {
        return it->second;
    }
    return {};
}

std::string NameResolver::get_hostname(const std::string& ip) {
    std::string normalized = ip;
    if (normalized.size() >= 2 && normalized.front() == '[' && normalized.back() == ']') {
        normalized = normalized.substr(1, normalized.size() - 2);
    }

    if (normalized.empty() || normalized == "*" ||
        normalized == "0.0.0.0" ||
        normalized == "::" || normalized == "0:0:0:0:0:0:0:0") {
        return "*";
    }

    {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard lock(dns_mutex_);
        if (auto it = dns_cache_.find(normalized); it != dns_cache_.end()) {
            if (it->second.expires <= now) {
                dns_cache_.erase(it);
            } else {
                it->second.last_used = now;
                if (it->second.value == kResolving) {
                    return {};  // Still resolving
                }
                if (it->second.value == kNotFound) {
                    return {};  // Resolution failed, return empty
                }
                return it->second.value;  // Return cached hostname
            }
        }

        // Mark as resolving and queue for resolution
        dns_cache_[normalized] = {kResolving, now, now + kResolvingTtl};
        evict_if_needed();
    }

    {
        std::lock_guard lock(queue_mutex_);
        resolve_queue_.push(normalized);
    }
    queue_cv_.notify_one();

    return {};  // Will be resolved asynchronously
}

void NameResolver::evict_if_needed() {
    while (dns_cache_.size() > kMaxCacheEntries) {
        auto oldest_it = dns_cache_.begin();
        for (auto it = dns_cache_.begin(); it != dns_cache_.end(); ++it) {
            if (it->second.last_used < oldest_it->second.last_used) {
                oldest_it = it;
            }
        }
        dns_cache_.erase(oldest_it);
    }
}

void NameResolver::resolver_thread() {
    while (running_) {
        std::string ip;

        {
            std::unique_lock lock(queue_mutex_);

            // Wait for work or timeout (to flush pending notifications)
            queue_cv_.wait_for(lock, kNotifyInterval, [this] {
                return !resolve_queue_.empty() || !running_;
            });

            if (!running_) break;

            if (!resolve_queue_.empty()) {
                ip = resolve_queue_.front();
                resolve_queue_.pop();
            }
        }

        // Check if we need to flush pending notification (even if no work)
        if (ip.empty()) {
            if (pending_notify_.exchange(false)) {
                std::function<void()> cb;
                {
                    std::lock_guard lock(callback_mutex_);
                    cb = on_resolved_;
                }
                if (cb) cb();
            }
            continue;
        }

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

        const auto now = std::chrono::steady_clock::now();

        // Update cache
        {
            std::lock_guard lock(dns_mutex_);
            if (hostname.empty()) {
                dns_cache_[ip] = {kNotFound, now, now + kNotFoundTtl};
            } else {
                dns_cache_[ip] = {hostname, now, now + kResolveTtl};
            }
            evict_if_needed();
        }

        // Throttle notifications to reduce UI wakeups
        {
            std::function<void()> cb;
            {
                std::lock_guard lock(callback_mutex_);
                cb = on_resolved_;
            }
            if (cb) {
                if (now - last_notify_time_ >= kNotifyInterval) {
                    last_notify_time_ = now;
                    pending_notify_ = false;
                    cb();
                } else {
                    pending_notify_ = true;
                }
            }
        }
    }
}

} // namespace pex
