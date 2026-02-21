#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace CryptoUtils {
    std::string hmac_sha256(const std::string& key, const std::string& data);
    std::string hmac_sha512(const std::string& key, const std::string& data);
    std::string hmac_sha256_hex(const std::string& key, const std::string& data);
    std::string hmac_sha512_hex(const std::string& key, const std::string& data);
    std::string base64_encode(const std::vector<uint8_t>& data);
    std::string base64_encode(const std::string& data);
    std::vector<uint8_t> base64_decode(const std::string& encoded);
    std::string sha256(const std::string& data);
    std::string sha512(const std::string& data);
    std::string generate_uuid();
    uint64_t generate_nonce();
}
