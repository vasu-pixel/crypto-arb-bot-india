#pragma once
#include "common/types.h"
#include "exchange/ws_client.h"
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

class BinanceWs {
public:
  using OrderBookCallback = std::function<void(const OrderBookSnapshot &)>;

  BinanceWs(const std::string &ws_base_url, ExchangeWsClient &ws_client);

  void subscribe_depth(const std::string &symbol, OrderBookCallback callback);
  void unsubscribe_depth(const std::string &symbol);
  void on_connected();

  auto get_callbacks() const
      -> const std::unordered_map<std::string, OrderBookCallback> & {
    return callbacks_;
  }

private:
  void on_message(const std::string &msg);
  void parse_depth_snapshot(const nlohmann::json &j, const std::string &symbol);

  std::string ws_base_url_;
  ExchangeWsClient &ws_client_;
  std::unordered_map<std::string, OrderBookCallback> callbacks_;
  std::vector<std::string> pending_subs_;
  std::mutex mutex_;
};
