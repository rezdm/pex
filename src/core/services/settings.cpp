#include "settings.hpp"

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace pex {

Settings::Settings() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        path_ = std::string(xdg) + "/pex/pex.conf";
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        path_ = std::string(home) + "/.config/pex/pex.conf";
    } else {
        path_ = "pex.conf";  // Last resort: current directory
    }
}

void Settings::load() {
    std::ifstream file(path_);
    if (!file) return;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        values_[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

bool Settings::save() const {
    std::error_code ec;
    fs::create_directories(fs::path(path_).parent_path(), ec);

    std::ofstream file(path_);
    if (!file) return false;

    file << "# pex settings - edited by pex/pexc on exit\n";
    for (const auto& [key, value] : values_) {
        file << key << '=' << value << '\n';
    }
    return file.good();
}

int Settings::get_int(const std::string& key, const int def) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return def;
    int value = def;
    const auto& s = it->second;
    std::from_chars(s.data(), s.data() + s.size(), value);
    return value;
}

bool Settings::get_bool(const std::string& key, const bool def) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return def;
    return it->second == "1" || it->second == "true";
}

void Settings::set_int(const std::string& key, const int value) {
    values_[key] = std::to_string(value);
}

void Settings::set_bool(const std::string& key, const bool value) {
    values_[key] = value ? "1" : "0";
}

} // namespace pex
