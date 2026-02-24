#include "exchange/gateio/gateio_auth.h"
#include "common/crypto_utils.h"
#include <chrono>

GateioAuth::GateioAuth(const std::string& api_key, const std::string& secret_key)
    : api_key_(api_key), secret_key_(secret_key) {}

std::string GateioAuth::get_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
    return std::to_string(secs.count());
}

std::string GateioAuth::sign_request(const std::string& method, const std::string& path,
                                      const std::string& query, const std::string& body) const {
    // Gate.io v4 API signature:
    // sign = HexEncode(HMAC-SHA512(
    //   method\npath\nquery_string\nhex(SHA512(body))\ntimestamp, secret))
    std::string body_hash = CryptoUtils::sha512(body);
    std::string ts = get_timestamp();
    std::string prehash = method + "\n" + path + "\n" + query + "\n" + body_hash + "\n" + ts;
    return CryptoUtils::hmac_sha512_hex(secret_key_, prehash);
}
