#pragma once
#include <string>
#include <cstdint>

class BybitAuth {
public:
    BybitAuth(const std::string& api_key, const std::string& secret_key);

    std::string get_api_key() const { return api_key_; }
    // Bybit v5 signature: HMAC-SHA256(timestamp + api_key + recv_window + query/body, secret)
    std::string sign_request(const std::string& timestamp, const std::string& recv_window,
                             const std::string& payload) const;
    std::string get_timestamp() const;

private:
    std::string api_key_;
    std::string secret_key_;
};
