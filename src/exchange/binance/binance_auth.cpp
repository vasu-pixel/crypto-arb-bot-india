#include "exchange/binance/binance_auth.h"
#include "common/crypto_utils.h"
#include "common/time_utils.h"
#include "common/logger.h"

BinanceAuth::BinanceAuth(const std::string& api_key, const std::string& secret_key)
    : api_key_(api_key), secret_key_(secret_key) {
    LOG_DEBUG("BinanceAuth initialised");
}

std::string BinanceAuth::sign_request(const std::string& query_string) const {
    // Add timestamp
    uint64_t timestamp = TimeUtils::now_ms();
    std::string qs = query_string;
    if (!qs.empty()) {
        qs += "&";
    }
    qs += "timestamp=" + std::to_string(timestamp);

    // Compute HMAC-SHA256 signature
    std::string signature = CryptoUtils::hmac_sha256_hex(secret_key_, qs);
    qs += "&signature=" + signature;

    LOG_DEBUG("BinanceAuth: signed query string (ts={})", timestamp);
    return qs;
}

const std::string& BinanceAuth::get_api_key() const {
    return api_key_;
}
