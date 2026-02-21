#include "exchange/kraken/kraken_ws.h"
#include "common/logger.h"
#include <nlohmann/json.hpp>

KrakenWs::KrakenWs(const std::string &ws_base_url, ExchangeWsClient &ws_client)
    : ws_base_url_(ws_base_url), ws_client_(ws_client) {
  ws_client_.set_on_message(
      [this](const std::string &msg) { on_message(msg); });
  // on_connect is set by KrakenAdapter to call ws_->on_connected()
  // Do NOT set it here — the adapter's set_on_connect would overwrite it
}

void KrakenWs::on_connected() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (pending_subs_.empty())
    return;

  nlohmann::json sub = {
      {"method", "subscribe"},
      {"params",
       {{"channel", "book"}, {"symbol", pending_subs_}, {"depth", 25}}}};
  ws_client_.send(sub.dump());
  LOG_INFO("Kraken: sent batched subscription for {} pairs",
           pending_subs_.size());
}

void KrakenWs::subscribe_depth(const std::string &pair,
                               OrderBookCallback callback) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_[pair] = std::move(callback);
    pending_subs_.push_back(pair);
  }

  if (ws_client_.is_connected()) {
    nlohmann::json sub = {
        {"method", "subscribe"},
        {"params", {{"channel", "book"}, {"symbol", {pair}}, {"depth", 25}}}};
    ws_client_.send(sub.dump());
    LOG_INFO("Kraken: subscribed to book for {}", pair);
  }
}

void KrakenWs::unsubscribe_depth(const std::string &pair) {
  nlohmann::json unsub = {
      {"method", "unsubscribe"},
      {"params", {{"channel", "book"}, {"symbol", {pair}}}}};
  ws_client_.send(unsub.dump());

  std::lock_guard<std::mutex> lock(mutex_);
  callbacks_.erase(pair);
  pending_subs_.erase(
      std::remove(pending_subs_.begin(), pending_subs_.end(), pair),
      pending_subs_.end());
  LOG_INFO("Kraken: unsubscribed from book for {}", pair);
}

void KrakenWs::on_message(const std::string &msg) {
  try {
    auto j = nlohmann::json::parse(msg);

    // Kraken WS v2 book channel: "type" is "snapshot" or "update"
    if (j.contains("channel") && j["channel"] == "book" && j.contains("data")) {
      std::string msg_type = j.value("type", "update");
      bool is_snapshot = (msg_type == "snapshot");

      for (auto &data : j["data"]) {
        std::string symbol = data.value("symbol", "");
        if (symbol.empty())
          continue;

        OrderBookSnapshot snap;
        snap.exchange = Exchange::KRAKEN;
        snap.pair = symbol;
        snap.local_timestamp = std::chrono::steady_clock::now();
        snap.is_delta = !is_snapshot;

        if (data.contains("bids")) {
          for (auto &bid : data["bids"]) {
            double price = bid.value("price", 0.0);
            double qty = bid.value("qty", 0.0);
            if (price > 0 || snap.is_delta)
              snap.bids.push_back({price, qty});
          }
        }
        if (data.contains("asks")) {
          for (auto &ask : data["asks"]) {
            double price = ask.value("price", 0.0);
            double qty = ask.value("qty", 0.0);
            if (price > 0 || snap.is_delta)
              snap.asks.push_back({price, qty});
          }
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = callbacks_.find(symbol);
        if (it != callbacks_.end()) {
          it->second(snap);
        }
      }
    }
  } catch (const std::exception &e) {
    LOG_DEBUG("Kraken WS parse error: {}", e.what());
  }
}
