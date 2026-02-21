#include "common/time_utils.h"
#include <iomanip>
#include <sstream>
#include <ctime>

namespace TimeUtils {

std::string now_iso8601() {
    return to_iso8601(std::chrono::system_clock::now());
}

std::string to_iso8601(std::chrono::system_clock::time_point tp) {
    auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t_val), "%Y-%m-%dT%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return oss.str();
}

std::chrono::system_clock::time_point from_iso8601(const std::string& iso) {
    std::tm tm = {};
    std::istringstream ss(iso);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    auto tp = std::chrono::system_clock::from_time_t(timegm(&tm));

    auto dot_pos = iso.find('.');
    if (dot_pos != std::string::npos) {
        auto ms_str = iso.substr(dot_pos + 1, 3);
        int ms = std::stoi(ms_str);
        tp += std::chrono::milliseconds(ms);
    }
    return tp;
}

uint64_t now_ms() {
    return to_ms(std::chrono::system_clock::now());
}

uint64_t to_ms(std::chrono::system_clock::time_point tp) {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            tp.time_since_epoch()).count());
}

std::chrono::system_clock::time_point from_ms(uint64_t ms) {
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
}

} // namespace TimeUtils
