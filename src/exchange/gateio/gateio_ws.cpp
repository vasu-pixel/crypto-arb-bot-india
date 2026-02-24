#include "exchange/gateio/gateio_ws.h"
#include "common/logger.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <chrono>

GateioWs::GateioWs(const std::string& ws_base_url, ExchangeWsClient& ws_client)
    : ws_base_url_(ws_base_url), ws_client_(ws_client) {
    ws_client_.set_on_message(
        [this](const std::string& msg) { on_message(msg); });
}

void GateioWs::on_connected() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_subs_.empty()) return;

    auto now = std::chrono::system_clock::now();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());

    // Gate.io WS v4: {"time":123,"channel":"spot.order_book","event":"subscribe","payload":["BTC_USDT","20","100ms"]}
    for (auto& pair : pending_subs_) {
        nlohmann::json sub = {
            {"time", secs.count()},
            {"channel", "spot.order_book"},
            {"event", "subscribe"},
            {"payload", {pair, "20", "100ms"}}
        };
        ws_client_.send(sub.dump());
    }
    LOG_INFO("Gate.io: sent subscription for {} pairs", pending_subs_.size());
}

void GateioWs::subscribe_depth(const std::string& currency_pair, OrderBookCallback callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_[currency_pair] = std::move(callback);
        pending_subs_.push_back(currency_pair);
    }

    if (ws_client_.is_connected()) {
        auto now = std::chrono::system_clock::now();
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
        nlohmann::json sub = {
            {"time", secs.count()},
            {"channel", "spot.order_book"},
            {"event", "subscribe"},
            {"payload", {currency_pair, "20", "100ms"}}
        };
        ws_client_.send(sub.dump());
        LOG_INFO("Gate.io: subscribed to order_book for {}", currency_pair);
    }
}

void GateioWs::unsubscribe_depth(const std::string& currency_pair) {
    auto now = std::chrono::system_clock::now();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
    nlohmann::json unsub = {
        {"time", secs.count()},
        {"channel", "spot.order_book"},
        {"event", "unsubscribe"},
        {"payload", {currency_pair, "20", "100ms"}}
    };
    ws_client_.send(unsub.dump());

    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.erase(currency_pair);
    pending_subs_.erase(
        std::remove(pending_subs_.begin(), pending_subs_.end(), currency_pair),
        pending_subs_.end());
    LOG_INFO("Gate.io: unsubscribed from order_book for {}", currency_pair);
}

void GateioWs::on_message(const std::string& msg) {
    try {
        auto j = nlohmann::json::parse(msg);

        // Gate.io order book: {"channel":"spot.order_book","event":"update","result":{...}}
        if (!j.contains("channel") || !j.contains("event") || !j.contains("result")) return;

        std::string channel = j["channel"].get<std::string>();
        std::string event = j["event"].get<std::string>();

        if (channel != "spot.order_book") return;
        if (event != "update" && event != "all") return;

        auto& result = j["result"];
        // The currency_pair may be in the result or we extract from subscription
        std::string currency_pair;
        if (result.contains("s")) {
            currency_pair = result["s"].get<std::string>();
        } else if (result.contains("currency_pair")) {
            currency_pair = result["currency_pair"].get<std::string>();
        } else {
            return;
        }

        OrderBookSnapshot snap;
        snap.exchange = Exchange::GATEIO;
        snap.pair = currency_pair;
        snap.local_timestamp = std::chrono::steady_clock::now();
        snap.is_delta = (event == "update");

        if (result.contains("bids")) {
            for (auto& bid : result["bids"]) {
                if (bid.is_array() && bid.size() >= 2) {
                    double price = std::stod(bid[0].get<std::string>());
                    double qty = std::stod(bid[1].get<std::string>());
                    if (qty > 0) snap.bids.push_back({price, qty});
                }
            }
        }
        if (result.contains("asks")) {
            for (auto& ask : result["asks"]) {
                if (ask.is_array() && ask.size() >= 2) {
                    double price = std::stod(ask[0].get<std::string>());
                    double qty = std::stod(ask[1].get<std::string>());
                    if (qty > 0) snap.asks.push_back({price, qty});
                }
            }
        }

        if (result.contains("t")) {
            snap.sequence_id = result["t"].get<uint64_t>();
        } else if (result.contains("id")) {
            snap.sequence_id = result["id"].get<uint64_t>();
        }

        // Sort: bids descending, asks ascending
        std::sort(snap.bids.begin(), snap.bids.end(),
                  [](const auto& a, const auto& b) { return a.price > b.price; });
        std::sort(snap.asks.begin(), snap.asks.end(),
                  [](const auto& a, const auto& b) { return a.price < b.price; });

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = callbacks_.find(currency_pair);
        if (it != callbacks_.end()) {
            it->second(snap);
        }
    } catch (const std::exception& e) {
        LOG_DEBUG("Gate.io WS parse error: {}", e.what());
    }
}
