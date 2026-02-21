#include "exchange/coinbase/coinbase_ws.h"
#include "common/logger.h"
#include <nlohmann/json.hpp>

CoinbaseWs::CoinbaseWs(const std::string& ws_base_url, ExchangeWsClient& ws_client,
                         CoinbaseAuth& auth)
    : ws_base_url_(ws_base_url), ws_client_(ws_client), auth_(auth) {
    ws_client_.set_on_message([this](const std::string& msg) { on_message(msg); });
    ws_client_.set_on_connect([this]() { on_connected(); });
}

void CoinbaseWs::on_connected() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_subs_.empty()) return;

    std::string jwt = auth_.generate_jwt("GET", "/ws");

    // Subscribe to heartbeats to keep connection alive
    nlohmann::json heartbeat_sub = {
        {"type", "subscribe"},
        {"product_ids", pending_subs_},
        {"channel", "heartbeats"},
        {"jwt", jwt}
    };
    ws_client_.send(heartbeat_sub.dump());

    // Subscribe to level2 for order book
    nlohmann::json level2_sub = {
        {"type", "subscribe"},
        {"product_ids", pending_subs_},
        {"channel", "level2"},
        {"jwt", jwt}
    };
    ws_client_.send(level2_sub.dump());
}

void CoinbaseWs::subscribe_level2(const std::string& product_id, OrderBookCallback callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_[product_id] = std::move(callback);
        pending_subs_.push_back(product_id);
    }

    if (ws_client_.is_connected()) {
        std::string jwt = auth_.generate_jwt("GET", "/ws");
        nlohmann::json sub = {
            {"type", "subscribe"},
            {"product_ids", {product_id}},
            {"channel", "level2"},
            {"jwt", jwt}
        };
        ws_client_.send(sub.dump());
        LOG_INFO("Coinbase: subscribed to level2 for {}", product_id);
    }
}

void CoinbaseWs::unsubscribe_level2(const std::string& product_id) {
    std::string jwt = auth_.generate_jwt("GET", "/ws");
    nlohmann::json unsub = {
        {"type", "unsubscribe"},
        {"product_ids", {product_id}},
        {"channel", "level2"},
        {"jwt", jwt}
    };
    ws_client_.send(unsub.dump());

    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.erase(product_id);
    pending_subs_.erase(
        std::remove(pending_subs_.begin(), pending_subs_.end(), product_id),
        pending_subs_.end());
    LOG_INFO("Coinbase: unsubscribed from level2 for {}", product_id);
}

void CoinbaseWs::on_message(const std::string& msg) {
    try {
        auto j = nlohmann::json::parse(msg);
        std::string channel = j.value("channel", "");

        if (channel == "l2_data" && j.contains("events")) {
            for (auto& event : j["events"]) {
                std::string product_id = event.value("product_id", "");
                if (product_id.empty()) continue;

                OrderBookSnapshot snap;
                snap.exchange = Exchange::COINBASE;
                snap.pair = product_id;
                snap.local_timestamp = std::chrono::steady_clock::now();

                if (event.contains("updates")) {
                    for (auto& update : event["updates"]) {
                        std::string side = update.value("side", "");
                        double price = std::stod(update.value("price_level", "0"));
                        double qty = std::stod(update.value("new_quantity", "0"));

                        if (side == "bid") snap.bids.push_back({price, qty});
                        else if (side == "offer") snap.asks.push_back({price, qty});
                    }
                }

                std::lock_guard<std::mutex> lock(mutex_);
                auto it = callbacks_.find(product_id);
                if (it != callbacks_.end()) {
                    it->second(snap);
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_DEBUG("Coinbase WS parse error: {}", e.what());
    }
}
