#include "exchange/coinbase/coinbase_auth.h"
#include "common/crypto_utils.h"
#include "common/time_utils.h"
#include <nlohmann/json.hpp>
#include <sstream>

CoinbaseAuth::CoinbaseAuth(const std::string& api_key, const std::string& secret_key)
    : api_key_(api_key), secret_key_(secret_key) {}

std::string CoinbaseAuth::generate_jwt(const std::string& method, const std::string& path) const {
    // For Coinbase Advanced Trade API, we use ES256 or HMAC-based JWT
    // Simplified: use HMAC-SHA256 signing with the secret key
    uint64_t now = TimeUtils::now_ms() / 1000;
    uint64_t exp = now + 120; // 2 minute expiry

    // JWT Header
    nlohmann::json header = {{"alg", "HS256"}, {"typ", "JWT"}, {"kid", api_key_}};
    std::string header_b64 = CryptoUtils::base64_encode(header.dump());

    // JWT Payload
    nlohmann::json payload = {
        {"sub", api_key_},
        {"iss", "coinbase-cloud"},
        {"iat", now},
        {"exp", exp},
        {"nbf", now},
        {"uri", method + " " + path}
    };
    std::string payload_b64 = CryptoUtils::base64_encode(payload.dump());

    // Signature
    std::string signing_input = header_b64 + "." + payload_b64;
    std::string signature = CryptoUtils::hmac_sha256(secret_key_, signing_input);
    std::string sig_b64 = CryptoUtils::base64_encode(signature);

    return signing_input + "." + sig_b64;
}

std::string CoinbaseAuth::auth_header(const std::string& method, const std::string& path) const {
    return "Bearer " + generate_jwt(method, path);
}
