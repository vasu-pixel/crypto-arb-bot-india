#include "exchange/kraken/kraken_auth.h"
#include "common/crypto_utils.h"
#include <chrono>

KrakenAuth::KrakenAuth(const std::string& api_key, const std::string& secret_key)
    : api_key_(api_key), secret_key_(secret_key) {}

std::string KrakenAuth::sign_request(const std::string& url_path, uint64_t nonce,
                                      const std::string& post_data) const {
    // 1. SHA256(nonce + post_data)
    std::string nonce_str = std::to_string(nonce);
    std::string hash_input = nonce_str + post_data;
    std::string sha256_hash = CryptoUtils::sha256(hash_input);

    // 2. HMAC-SHA512(url_path + sha256_hash, Base64Decode(secret))
    auto decoded_secret = CryptoUtils::base64_decode(secret_key_);
    std::string secret_str(decoded_secret.begin(), decoded_secret.end());
    std::string hmac_input = url_path + sha256_hash;
    std::string hmac_result = CryptoUtils::hmac_sha512(secret_str, hmac_input);

    // 3. Base64 encode the result
    return CryptoUtils::base64_encode(hmac_result);
}

uint64_t KrakenAuth::next_nonce() {
    uint64_t nonce = CryptoUtils::generate_nonce();
    if (nonce <= last_nonce_) {
        nonce = last_nonce_ + 1;
    }
    last_nonce_ = nonce;
    return nonce;
}
