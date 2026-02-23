#pragma once
#include "common/types.h"
#include "exchange/ws_client.h"
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class OkxWs {
public:
    using OrderBookCallback = std::function<void(const OrderBookSnapshot&)>;

    OkxWs(const std::string& ws_base_url, ExchangeWsClient& ws_client);

    void subscribe_depth(const std::string& inst_id, OrderBookCallback callback);
    void unsubscribe_depth(const std::string& inst_id);
    void on_connected();

private:
    void on_message(const std::string& msg);

    std::string ws_base_url_;
    ExchangeWsClient& ws_client_;
    std::unordered_map<std::string, OrderBookCallback> callbacks_;
    std::vector<std::string> pending_subs_;
    mutable std::mutex mutex_;
};
