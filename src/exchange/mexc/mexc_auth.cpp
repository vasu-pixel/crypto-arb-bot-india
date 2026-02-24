#include "exchange/mexc/mexc_auth.h"
#include "common/crypto_utils.h"
#include <chrono>

MexcAuth::MexcAuth(const std::string& api_key, const std::string& secret_key)
    : api_key_(api_key), secret_key_(secret_key) {}

std::string MexcAuth::get_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return std::to_string(ms.count());
}

std::string MexcAuth::sign_request(const std::string& query_string) const {
    // MEXC v3 API: signature = HMAC-SHA256(totalParams, secretKey)
    return CryptoUtils::hmac_sha256_hex(secret_key_, query_string);
}
