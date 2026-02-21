#pragma once
#include <string>
#include <cstdint>

class KrakenAuth {
public:
    KrakenAuth(const std::string& api_key, const std::string& secret_key);

    std::string get_api_key() const { return api_key_; }
    // Generate signature: Base64(HMAC-SHA512(url_path + SHA256(nonce + post_data), Base64Decode(secret)))
    std::string sign_request(const std::string& url_path, uint64_t nonce,
                             const std::string& post_data) const;
    uint64_t next_nonce();

private:
    std::string api_key_;
    std::string secret_key_;
    uint64_t last_nonce_ = 0;
};
