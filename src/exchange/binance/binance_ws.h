#pragma once
#include "exchange/ws_client.h"
#include "common/types.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <mutex>

class BinanceWs {
public:
    using OrderBookCallback = std::function<void(const OrderBookSnapshot&)>;

    BinanceWs(const std::string& ws_base_url, ExchangeWsClient& ws_client);

    void subscribe_depth(const std::string& symbol, OrderBookCallback callback);
    void unsubscribe_depth(const std::string& symbol);

private:
    void on_message(const std::string& msg);
    void parse_depth_update(const nlohmann::json& j);

    std::string ws_base_url_;
    ExchangeWsClient& ws_client_;
    std::unordered_map<std::string, OrderBookCallback> callbacks_;
    std::mutex mutex_;
};
