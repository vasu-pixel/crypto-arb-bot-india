#pragma once
#include <string>
#include <chrono>
#include <cstdint>

namespace TimeUtils {
    std::string now_iso8601();
    std::string to_iso8601(std::chrono::system_clock::time_point tp);
    std::chrono::system_clock::time_point from_iso8601(const std::string& iso);
    uint64_t now_ms();
    uint64_t to_ms(std::chrono::system_clock::time_point tp);
    std::chrono::system_clock::time_point from_ms(uint64_t ms);
}
