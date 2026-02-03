#pragma once

#include <cstdint>
#include <sstream>
#include <iomanip>
#include <string>

namespace pex {

// Format byte counts in human-readable form.
// compact=true:  "1.23G" (no spaces, short units - suitable for TUI/tables)
// compact=false: "1.23 GB" (spaces, standard units - suitable for GUI)
inline std::string format_bytes(int64_t bytes, bool compact = true) {
    static constexpr const char* units_compact[] = {"B", "K", "M", "G", "T", "P"};
    static constexpr const char* units_full[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    constexpr int max_unit = 5;

    const auto* units = compact ? units_compact : units_full;
    const char* sep = compact ? "" : " ";

    int unit_idx = 0;
    auto size = static_cast<double>(bytes);

    while (size >= 1024.0 && unit_idx < max_unit) {
        size /= 1024.0;
        unit_idx++;
    }

    std::ostringstream oss;
    if (unit_idx == 0) {
        oss << bytes << sep << units[unit_idx];
    } else if (size >= 100.0) {
        oss << std::fixed << std::setprecision(0) << size << sep << units[unit_idx];
    } else if (size >= 10.0) {
        oss << std::fixed << std::setprecision(1) << size << sep << units[unit_idx];
    } else {
        oss << std::fixed << std::setprecision(2) << size << sep << units[unit_idx];
    }
    return oss.str();
}

} // namespace pex
