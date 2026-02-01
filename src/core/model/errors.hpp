#pragma once

#include <chrono>
#include <string>

namespace pex {

// Generic parse/error info surfaced from data providers
struct ParseError {
    std::chrono::steady_clock::time_point timestamp;
    std::string message;
};

} // namespace pex
