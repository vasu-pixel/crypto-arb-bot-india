#include "exchange/bybit/bybit_ws.h"
#include "common/logger.h"
#include <algorithm>
#include <nlohmann/json.hpp>

BybitWs::BybitWs(const std::string& ws_base_url, ExchangeWsClient& ws_client)
    : ws_base_url_(ws_base_url), ws_client_(ws_client) {
    ws_client_.set_on_message(
        [this](const std::string& msg) { on_message(msg); });
}

void BybitWs::on_connected() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_subs_.empty()) return;

    // Bybit v5 subscribe: {"op":"subscribe","args":["orderbook.50.BTCUSDT"]}
    nlohmann::json args = nlohmann::json::array();
    for (auto& symbol : pending_subs_) {
        args.push_back("orderbook.50." + symbol);
    }
    nlohmann::json sub = {{"op", "subscribe"}, {"args", args}};
    ws_client_.send(sub.dump());
    LOG_INFO("Bybit: sent subscription for {} symbols", pending_subs_.size());
}

void BybitWs::subscribe_depth(const std::string& symbol, OrderBookCallback callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_[symbol] = std::move(callback);
        pending_subs_.push_back(symbol);
    }

    if (ws_client_.is_connected()) {
        nlohmann::json sub = {
            {"op", "subscribe"},
            {"args", {"orderbook.50." + symbol}}
        };
        ws_client_.send(sub.dump());
        LOG_INFO("Bybit: subscribed to orderbook.50 for {}", symbol);
    }
}

void BybitWs::unsubscribe_depth(const std::string& symbol) {
    nlohmann::json unsub = {
        {"op", "unsubscribe"},
        {"args", {"orderbook.50." + symbol}}
    };
    ws_client_.send(unsub.dump());

    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.erase(symbol);
    pending_subs_.erase(
        std::remove(pending_subs_.begin(), pending_subs_.end(), symbol),
        pending_subs_.end());
    LOG_INFO("Bybit: unsubscribed from orderbook.50 for {}", symbol);
}

void BybitWs::on_message(const std::string& msg) {
    try {
        auto j = nlohmann::json::parse(msg);

        // Handle ping/pong heartbeat
        if (j.contains("op") && j["op"] == "ping") {
            nlohmann::json pong = {{"op", "pong"}};
            ws_client_.send(pong.dump());
            return;
        }

        // Bybit orderbook data: {"topic":"orderbook.50.BTCUSDT","type":"snapshot","data":{...}}
        if (!j.contains("topic") || !j.contains("data")) return;

        std::string topic = j["topic"].get<std::string>();
        if (topic.find("orderbook.50.") != 0) return;

        // Extract symbol from topic: "orderbook.50.BTCUSDT" -> "BTCUSDT"
        std::string symbol = topic.substr(13);

        std::string msg_type = j.value("type", "snapshot");
        bool is_delta = (msg_type == "delta");

        auto& data = j["data"];
        OrderBookSnapshot snap;
        snap.exchange = Exchange::BYBIT;
        snap.pair = symbol;
        snap.local_timestamp = std::chrono::steady_clock::now();
        snap.is_delta = is_delta;

        if (data.contains("b")) {
            for (auto& bid : data["b"]) {
                if (bid.size() >= 2) {
                    double price = std::stod(bid[0].get<std::string>());
                    double qty = std::stod(bid[1].get<std::string>());
                    snap.bids.push_back({price, qty});
                }
            }
        }
        if (data.contains("a")) {
            for (auto& ask : data["a"]) {
                if (ask.size() >= 2) {
                    double price = std::stod(ask[0].get<std::string>());
                    double qty = std::stod(ask[1].get<std::string>());
                    snap.asks.push_back({price, qty});
                }
            }
        }

        if (data.contains("u")) {
            snap.sequence_id = data["u"].get<uint64_t>();
        } else if (data.contains("seq")) {
            snap.sequence_id = data["seq"].get<uint64_t>();
        }

        // Sort: bids descending, asks ascending
        if (!is_delta) {
            std::sort(snap.bids.begin(), snap.bids.end(),
                      [](const auto& a, const auto& b) { return a.price > b.price; });
            std::sort(snap.asks.begin(), snap.asks.end(),
                      [](const auto& a, const auto& b) { return a.price < b.price; });
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = callbacks_.find(symbol);
        if (it != callbacks_.end()) {
            it->second(snap);
        }
    } catch (const std::exception& e) {
        LOG_DEBUG("Bybit WS parse error: {}", e.what());
    }
}
