#pragma once
#include <string>
#include <cstdint>

class OkxAuth {
public:
    OkxAuth(const std::string& api_key, const std::string& secret_key);

    std::string get_api_key() const { return api_key_; }
    // Generate signature for OKX REST API
    // sign = Base64(HMAC-SHA256(timestamp + method + requestPath + body, secret))
    std::string sign_request(const std::string& timestamp, const std::string& method,
                             const std::string& request_path, const std::string& body = "") const;
    std::string get_timestamp() const;

private:
    std::string api_key_;
    std::string secret_key_;
};
