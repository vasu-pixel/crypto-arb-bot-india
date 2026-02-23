#include "exchange/bybit/bybit_auth.h"
#include "common/crypto_utils.h"
#include <chrono>

BybitAuth::BybitAuth(const std::string& api_key, const std::string& secret_key)
    : api_key_(api_key), secret_key_(secret_key) {}

std::string BybitAuth::get_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return std::to_string(ms.count());
}

std::string BybitAuth::sign_request(const std::string& timestamp, const std::string& recv_window,
                                     const std::string& payload) const {
    // Bybit v5: sign = HMAC-SHA256(timestamp + api_key + recv_window + payload, secret)
    std::string prehash = timestamp + api_key_ + recv_window + payload;
    return CryptoUtils::hmac_sha256_hex(secret_key_, prehash);
}
