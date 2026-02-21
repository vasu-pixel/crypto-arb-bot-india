#pragma once
#include "common/types.h"
#include "exchange/coinbase/coinbase_auth.h"
#include "exchange/ws_client.h"
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

class CoinbaseWs {
public:
  using OrderBookCallback = std::function<void(const OrderBookSnapshot &)>;

  CoinbaseWs(const std::string &ws_base_url, ExchangeWsClient &ws_client,
             CoinbaseAuth &auth);

  void subscribe_level2(const std::string &product_id,
                        OrderBookCallback callback);
  void unsubscribe_level2(const std::string &product_id);

  auto get_callbacks() const
      -> const std::unordered_map<std::string, OrderBookCallback> & {
    return callbacks_;
  }

private:
  void on_message(const std::string &msg);
  void on_connected();

  std::string ws_base_url_;
  ExchangeWsClient &ws_client_;
  CoinbaseAuth &auth_;
  std::unordered_map<std::string, OrderBookCallback> callbacks_;
  std::mutex mutex_;
  std::vector<std::string> pending_subs_;
};
