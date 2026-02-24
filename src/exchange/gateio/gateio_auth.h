#pragma once
#include <string>

class GateioAuth {
public:
    GateioAuth(const std::string& api_key, const std::string& secret_key);

    std::string get_api_key() const { return api_key_; }
    // Gate.io v4: sign = HMAC-SHA512(method\npath\nquery\nhash(body)\ntimestamp, secret)
    std::string sign_request(const std::string& method, const std::string& path,
                             const std::string& query = "", const std::string& body = "") const;
    std::string get_timestamp() const;

private:
    std::string api_key_;
    std::string secret_key_;
};
