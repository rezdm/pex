#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace pex {

// ASCII-lowercase a copy, for case-insensitive substring matching (e.g. the
// find-open-file search in both UIs).
inline std::string to_lower_copy(const std::string& s) {
    std::string result = s;
    std::ranges::transform(result, result.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

} // namespace pex
