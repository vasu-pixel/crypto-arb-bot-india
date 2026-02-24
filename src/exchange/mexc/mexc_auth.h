#pragma once
#include <string>

class MexcAuth {
public:
    MexcAuth(const std::string& api_key, const std::string& secret_key);

    std::string get_api_key() const { return api_key_; }
    // MEXC v3: sign = HMAC-SHA256(query_string, secret)
    std::string sign_request(const std::string& query_string) const;
    std::string get_timestamp() const;

private:
    std::string api_key_;
    std::string secret_key_;
};
