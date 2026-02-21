#pragma once
#include <string>
#include <cstdint>

class CoinbaseAuth {
public:
    CoinbaseAuth(const std::string& api_key, const std::string& secret_key);

    std::string get_api_key() const { return api_key_; }
    // Generate JWT for Coinbase Advanced Trade API
    std::string generate_jwt(const std::string& method, const std::string& path) const;
    // Authorization header value
    std::string auth_header(const std::string& method, const std::string& path) const;

private:
    std::string api_key_;
    std::string secret_key_;
};
