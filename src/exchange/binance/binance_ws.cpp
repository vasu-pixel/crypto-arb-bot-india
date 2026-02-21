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

  std::lock_guard<std::mutex> lock(mutex_);
  callbacks_[lower_symbol] = std::move(callback);
  // Use depth20@100ms for full top-20 snapshot stream (not delta stream)
  // Streams are specified in the URL via /stream?streams=... at connect time
  pending_subs_.push_back(lower_symbol + "@depth20@100ms");
  LOG_INFO("Binance: registered depth20 stream for {} (will connect via URL)",
           symbol);
}

void BinanceWs::on_connected() {
  std::lock_guard<std::mutex> lock(mutex_);
  // When using the combined stream URL (/stream?streams=...), no SUBSCRIBE
  // message is needed — just log that we're connected and ready.
  LOG_INFO("Binance: WebSocket connected, {} streams registered",
           pending_subs_.size());
}

void BinanceWs::unsubscribe_depth(const std::string &symbol) {
  std::string lower_symbol = symbol;
  std::transform(lower_symbol.begin(), lower_symbol.end(), lower_symbol.begin(),
                 ::tolower);

  nlohmann::json unsub_msg = {{"method", "UNSUBSCRIBE"},
                              {"params", {lower_symbol + "@depth20@100ms"}},
                              {"id", 2}};
  ws_client_.send(unsub_msg.dump());

  std::lock_guard<std::mutex> lock(mutex_);
  callbacks_.erase(lower_symbol);
  pending_subs_.erase(
      std::remove(pending_subs_.begin(), pending_subs_.end(),
                  lower_symbol + "@depth20@100ms"),
      pending_subs_.end());
  LOG_INFO("Binance: unsubscribed from depth20 for {}", symbol);
}

void BinanceWs::on_message(const std::string &msg) {
  try {
    auto j = nlohmann::json::parse(msg);

    // Combined stream format: {"stream":"btcusd@depth20@100ms","data":{...}}
    if (j.contains("stream")) {
      std::string stream = j["stream"];
      if (stream.find("@depth") != std::string::npos) {
        // Extract symbol from stream name: "btcusd@depth20@100ms" -> "btcusd"
        std::string symbol = stream.substr(0, stream.find('@'));
        parse_depth_snapshot(j.contains("data") ? j["data"] : j, symbol);
      }
    }
    // Single stream format (partial book depth): {"lastUpdateId":..., "bids":..., "asks":...}
    else if (j.contains("lastUpdateId") && j.contains("bids")) {
      // Single-stream mode doesn't have the symbol in the message,
      // so we use the first callback (only works with single subscriptions)
      std::lock_guard<std::mutex> lock(mutex_);
      if (!callbacks_.empty()) {
        parse_depth_snapshot(j, callbacks_.begin()->first);
      }
    }
  } catch (const std::exception &e) {
    LOG_DEBUG("Binance WS parse error: {}", e.what());
  }
}

void BinanceWs::parse_depth_snapshot(const nlohmann::json &j,
                                     const std::string &symbol) {
  try {
    OrderBookSnapshot snap;
    snap.exchange = Exchange::BINANCE_US;
    snap.pair = symbol;
    snap.sequence_id = j.value("lastUpdateId", 0ULL);
    snap.local_timestamp = std::chrono::steady_clock::now();

    if (j.contains("bids")) {
      for (auto &bid : j["bids"]) {
        if (bid.size() >= 2) {
          double price = std::stod(bid[0].get<std::string>());
          double qty = std::stod(bid[1].get<std::string>());
          if (qty > 0)
            snap.bids.push_back({price, qty});
        }
      }
    }
    if (j.contains("asks")) {
      for (auto &ask : j["asks"]) {
        if (ask.size() >= 2) {
          double price = std::stod(ask[0].get<std::string>());
          double qty = std::stod(ask[1].get<std::string>());
          if (qty > 0)
            snap.asks.push_back({price, qty});
        }
      }
    }

    // Sort: bids descending, asks ascending
    std::sort(snap.bids.begin(), snap.bids.end(),
              [](const auto &a, const auto &b) { return a.price > b.price; });
    std::sort(snap.asks.begin(), snap.asks.end(),
              [](const auto &a, const auto &b) { return a.price < b.price; });

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = callbacks_.find(symbol);
    if (it != callbacks_.end()) {
      it->second(snap);
    }
  } catch (const std::exception &e) {
    LOG_DEBUG("Binance depth snapshot parse error: {}", e.what());
  }
}
