#include "../procfs_reader.hpp"
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <pwd.h>

namespace pex {

// Error tracking methods
void ProcfsReader::add_error(const std::string& message) {
    std::lock_guard lock(errors_mutex_);
    recent_errors_.push_back({std::chrono::steady_clock::now(), message});
    if (recent_errors_.size() > kMaxErrors) {
        recent_errors_.erase(recent_errors_.begin());
    }
}

std::vector<ParseError> ProcfsReader::get_recent_errors() const {
    std::lock_guard lock(errors_mutex_);
    const auto cutoff = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    std::vector<ParseError> result;
    for (const auto& err : recent_errors_) {
        if (err.timestamp > cutoff) {
            result.push_back(err);
        }
    }
    return result;
}

void ProcfsReader::clear_errors() {
    std::lock_guard lock(errors_mutex_);
    recent_errors_.clear();
}

std::string ProcfsReader::read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) return {};
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string ProcfsReader::read_symlink(const std::string& path) {
    char buf[4096];
    const ssize_t len = readlink(path.c_str(), buf, sizeof(buf) - 1);
    if (len == -1) return {};
    buf[len] = '\0';
    return buf;
}

std::string ProcfsReader::get_username(const int uid) {
    if (const auto it = uid_cache_.find(uid); it != uid_cache_.end()) {
        return it->second;
    }

    struct passwd pwd_buf;
    struct passwd* result = nullptr;
    char buf[1024];
    std::string name;
    if (getpwuid_r(uid, &pwd_buf, buf, sizeof(buf), &result) == 0 && result) {
        name = result->pw_name;
    } else {
        name = std::to_string(uid);
    }
    uid_cache_[uid] = name;
    return name;
}

} // namespace pex
