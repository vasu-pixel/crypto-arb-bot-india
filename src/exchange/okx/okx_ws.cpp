#include "exchange/okx/okx_ws.h"
#include "common/logger.h"
#include <algorithm>
#include <nlohmann/json.hpp>

OkxWs::OkxWs(const std::string& ws_base_url, ExchangeWsClient& ws_client)
    : ws_base_url_(ws_base_url), ws_client_(ws_client) {
    ws_client_.set_on_message(
        [this](const std::string& msg) { on_message(msg); });
}

void OkxWs::on_connected() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_subs_.empty()) return;

    // OKX WS subscribe: {"op":"subscribe","args":[{"channel":"books5","instId":"BTC-USDT"}]}
    for (auto& inst_id : pending_subs_) {
        nlohmann::json sub = {
            {"op", "subscribe"},
            {"args", {{{"channel", "books5"}, {"instId", inst_id}}}}
        };
        ws_client_.send(sub.dump());
    }
    LOG_INFO("OKX: sent subscription for {} instruments", pending_subs_.size());
}

void OkxWs::subscribe_depth(const std::string& inst_id, OrderBookCallback callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_[inst_id] = std::move(callback);
        pending_subs_.push_back(inst_id);
    }

    if (ws_client_.is_connected()) {
        nlohmann::json sub = {
            {"op", "subscribe"},
            {"args", {{{"channel", "books5"}, {"instId", inst_id}}}}
        };
        ws_client_.send(sub.dump());
        LOG_INFO("OKX: subscribed to books5 for {}", inst_id);
    }
}

void OkxWs::unsubscribe_depth(const std::string& inst_id) {
    nlohmann::json unsub = {
        {"op", "unsubscribe"},
        {"args", {{{"channel", "books5"}, {"instId", inst_id}}}}
    };
    ws_client_.send(unsub.dump());

    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.erase(inst_id);
    pending_subs_.erase(
        std::remove(pending_subs_.begin(), pending_subs_.end(), inst_id),
        pending_subs_.end());
    LOG_INFO("OKX: unsubscribed from books5 for {}", inst_id);
}

void OkxWs::on_message(const std::string& msg) {
    try {
        auto j = nlohmann::json::parse(msg);

        // OKX book data: {"arg":{"channel":"books5","instId":"BTC-USDT"},"data":[{...}]}
        if (j.contains("arg") && j.contains("data")) {
            std::string channel = j["arg"].value("channel", "");
            if (channel != "books5") return;

            std::string inst_id = j["arg"].value("instId", "");
            if (inst_id.empty()) return;

            for (auto& data : j["data"]) {
                OrderBookSnapshot snap;
                snap.exchange = Exchange::OKX;
                snap.pair = inst_id;
                snap.local_timestamp = std::chrono::steady_clock::now();
                snap.is_delta = false; // books5 sends full top-5 snapshots

                if (data.contains("bids")) {
                    for (auto& bid : data["bids"]) {
                        if (bid.size() >= 2) {
                            double price = std::stod(bid[0].get<std::string>());
                            double qty = std::stod(bid[1].get<std::string>());
                            if (qty > 0) snap.bids.push_back({price, qty});
                        }
                    }
                }
                if (data.contains("asks")) {
                    for (auto& ask : data["asks"]) {
                        if (ask.size() >= 2) {
                            double price = std::stod(ask[0].get<std::string>());
                            double qty = std::stod(ask[1].get<std::string>());
                            if (qty > 0) snap.asks.push_back({price, qty});
                        }
                    }
                }

                if (data.contains("ts")) {
                    snap.sequence_id = std::stoull(data["ts"].get<std::string>());
                }

                // Sort: bids descending, asks ascending
                std::sort(snap.bids.begin(), snap.bids.end(),
                          [](const auto& a, const auto& b) { return a.price > b.price; });
                std::sort(snap.asks.begin(), snap.asks.end(),
                          [](const auto& a, const auto& b) { return a.price < b.price; });

                std::lock_guard<std::mutex> lock(mutex_);
                auto it = callbacks_.find(inst_id);
                if (it != callbacks_.end()) {
                    it->second(snap);
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_DEBUG("OKX WS parse error: {}", e.what());
    }
}
