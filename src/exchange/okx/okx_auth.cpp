#include "exchange/okx/okx_auth.h"
#include "common/crypto_utils.h"
#include <chrono>
#include <iomanip>
#include <sstream>

OkxAuth::OkxAuth(const std::string& api_key, const std::string& secret_key)
    : api_key_(api_key), secret_key_(secret_key) {}

std::string OkxAuth::get_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    auto secs = ms.count() / 1000;
    auto frac = ms.count() % 1000;
    std::time_t t = static_cast<std::time_t>(secs);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << "." << std::setw(3) << std::setfill('0') << frac << "Z";
    return oss.str();
}

std::string OkxAuth::sign_request(const std::string& timestamp, const std::string& method,
                                   const std::string& request_path, const std::string& body) const {
    std::string prehash = timestamp + method + request_path + body;
    std::string hmac = CryptoUtils::hmac_sha256(secret_key_, prehash);
    return CryptoUtils::base64_encode(hmac);
}
