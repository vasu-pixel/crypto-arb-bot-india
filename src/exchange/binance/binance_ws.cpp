#include "exchange/binance/binance_ws.h"
#include "common/logger.h"
#include <algorithm>
#include <nlohmann/json.hpp>

BinanceWs::BinanceWs(const std::string &ws_base_url,
                     ExchangeWsClient &ws_client)
    : ws_base_url_(ws_base_url), ws_client_(ws_client) {
  ws_client_.set_on_message(
      [this](const std::string &msg) { on_message(msg); });
}

void BinanceWs::subscribe_depth(const std::string &symbol,
                                OrderBookCallback callback) {
  std::string lower_symbol = symbol;
  std::transform(lower_symbol.begin(), lower_symbol.end(), lower_symbol.begin(),
                 ::tolower);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_[lower_symbol] = std::move(callback);
    pending_subs_.push_back(lower_symbol + "@depth@100ms");
  }

  if (ws_client_.is_connected()) {
    nlohmann::json sub_msg = {{"method", "SUBSCRIBE"},
                              {"params", {lower_symbol + "@depth@100ms"}},
                              {"id", 1}};
    ws_client_.send(sub_msg.dump());
    LOG_INFO("Binance: subscribed to depth for {}", symbol);
  }
}

void BinanceWs::on_connected() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (pending_subs_.empty())
    return;

  nlohmann::json sub_msg = {
      {"method", "SUBSCRIBE"}, {"params", pending_subs_}, {"id", 1}};
  ws_client_.send(sub_msg.dump());
  LOG_INFO("Binance: sent batched subscription for {} streams",
           pending_subs_.size());
}

void BinanceWs::unsubscribe_depth(const std::string &symbol) {
  std::string lower_symbol = symbol;
  std::transform(lower_symbol.begin(), lower_symbol.end(), lower_symbol.begin(),
                 ::tolower);

  nlohmann::json unsub_msg = {{"method", "UNSUBSCRIBE"},
                              {"params", {lower_symbol + "@depth@100ms"}},
                              {"id", 2}};
  ws_client_.send(unsub_msg.dump());

  std::lock_guard<std::mutex> lock(mutex_);
  callbacks_.erase(lower_symbol);
  LOG_INFO("Binance: unsubscribed from depth for {}", symbol);
}

void BinanceWs::on_message(const std::string &msg) {
  try {
    auto j = nlohmann::json::parse(msg);
    if (j.contains("stream")) {
      std::string stream = j["stream"];
      if (stream.find("@depth") != std::string::npos) {
        parse_depth_update(j.contains("data") ? j["data"] : j);
      }
    } else if (j.contains("e") && j["e"] == "depthUpdate") {
      parse_depth_update(j);
    }
  } catch (const std::exception &e) {
    LOG_DEBUG("Binance WS parse error: {}", e.what());
  }
}

void BinanceWs::parse_depth_update(const nlohmann::json &j) {
  try {
    std::string symbol = j.value("s", "");
    std::string lower_symbol = symbol;
    std::transform(lower_symbol.begin(), lower_symbol.end(),
                   lower_symbol.begin(), ::tolower);

    OrderBookSnapshot snap;
    snap.exchange = Exchange::BINANCE_US;
    snap.pair = symbol;
    snap.sequence_id = j.value("u", 0ULL);
    snap.local_timestamp = std::chrono::steady_clock::now();

    if (j.contains("b")) {
      for (auto &bid : j["b"]) {
        if (bid.size() >= 2) {
          double price = std::stod(bid[0].get<std::string>());
          double qty = std::stod(bid[1].get<std::string>());
          snap.bids.push_back({price, qty});
        }
      }
    }
    if (j.contains("a")) {
      for (auto &ask : j["a"]) {
        if (ask.size() >= 2) {
          double price = std::stod(ask[0].get<std::string>());
          double qty = std::stod(ask[1].get<std::string>());
          snap.asks.push_back({price, qty});
        }
      }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = callbacks_.find(lower_symbol);
    if (it != callbacks_.end()) {
      it->second(snap);
    }
  } catch (const std::exception &e) {
    LOG_DEBUG("Binance depth parse error: {}", e.what());
  }
}
