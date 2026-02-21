#pragma once
#include "exchange/ws_client.h"
#include "common/types.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <mutex>

class KrakenWs {
public:
    using OrderBookCallback = std::function<void(const OrderBookSnapshot&)>;

    KrakenWs(const std::string& ws_base_url, ExchangeWsClient& ws_client);

    void subscribe_depth(const std::string& pair, OrderBookCallback callback);
    void unsubscribe_depth(const std::string& pair);

private:
    void on_message(const std::string& msg);
    void on_connected();

    std::string ws_base_url_;
    ExchangeWsClient& ws_client_;
    std::unordered_map<std::string, OrderBookCallback> callbacks_;
    std::mutex mutex_;
    std::vector<std::string> pending_subs_;
};
