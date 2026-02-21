#pragma once

#include <string>

class BinanceAuth {
public:
    BinanceAuth(const std::string& api_key, const std::string& secret_key);

    // Appends timestamp= and signature= to the query string.
    // Returns the full signed query string ready for the request.
    std::string sign_request(const std::string& query_string) const;

    // Returns the API key for the X-MBX-APIKEY header.
    const std::string& get_api_key() const;

private:
    std::string api_key_;
    std::string secret_key_;
};
