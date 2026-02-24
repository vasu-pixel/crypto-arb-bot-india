#include "exchange/mexc/mexc_ws.h"
#include "common/logger.h"
#include <algorithm>
#include <nlohmann/json.hpp>

MexcWs::MexcWs(const std::string& ws_base_url, ExchangeWsClient& ws_client)
    : ws_base_url_(ws_base_url), ws_client_(ws_client) {
    ws_client_.set_on_message(
        [this](const std::string& msg) { on_message(msg); });
}

void MexcWs::on_connected() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_subs_.empty()) return;

    // MEXC WS v3: {"method":"SUBSCRIPTION","params":["spot@public.bookTicker.v3.api@BTCUSDT"]}
    nlohmann::json sub;
    sub["method"] = "SUBSCRIPTION";
    sub["params"] = nlohmann::json::array();
    for (auto& symbol : pending_subs_) {
        // Use partial depth stream for top 20 levels
        sub["params"].push_back("spot@public.limit.depth.v3.api@" + symbol + "@20");
    }
    ws_client_.send(sub.dump());
    LOG_INFO("MEXC: sent subscription for {} symbols", pending_subs_.size());
}

void MexcWs::subscribe_depth(const std::string& symbol, OrderBookCallback callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_[symbol] = std::move(callback);
        pending_subs_.push_back(symbol);
    }

    if (ws_client_.is_connected()) {
        nlohmann::json sub;
        sub["method"] = "SUBSCRIPTION";
        sub["params"] = nlohmann::json::array();
        sub["params"].push_back("spot@public.limit.depth.v3.api@" + symbol + "@20");
        ws_client_.send(sub.dump());
        LOG_INFO("MEXC: subscribed to depth for {}", symbol);
    }
}

void MexcWs::unsubscribe_depth(const std::string& symbol) {
    nlohmann::json unsub;
    unsub["method"] = "UNSUBSCRIPTION";
    unsub["params"] = nlohmann::json::array();
    unsub["params"].push_back("spot@public.limit.depth.v3.api@" + symbol + "@20");
    ws_client_.send(unsub.dump());

    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.erase(symbol);
    pending_subs_.erase(
        std::remove(pending_subs_.begin(), pending_subs_.end(), symbol),
        pending_subs_.end());
    LOG_INFO("MEXC: unsubscribed from depth for {}", symbol);
}

void MexcWs::on_message(const std::string& msg) {
    try {
        auto j = nlohmann::json::parse(msg);

        // Handle ping/pong keepalive
        if (j.contains("ping")) {
            nlohmann::json pong;
            pong["pong"] = j["ping"];
            ws_client_.send(pong.dump());
            return;
        }

        // MEXC depth data: {"c":"spot@public.limit.depth.v3.api@BTCUSDT@20","d":{"asks":[...],"bids":[...]}}
        if (!j.contains("c") || !j.contains("d")) return;

        std::string channel = j["c"].get<std::string>();
        // Extract symbol from channel: "spot@public.limit.depth.v3.api@BTCUSDT@20"
        // Split by @ and get the symbol part (index 3)
        size_t pos1 = channel.find("@");
        if (pos1 == std::string::npos) return;
        size_t pos2 = channel.find("@", pos1 + 1);
        if (pos2 == std::string::npos) return;
        size_t pos3 = channel.find("@", pos2 + 1);
        if (pos3 == std::string::npos) return;
        size_t pos4 = channel.find("@", pos3 + 1);
        std::string symbol = channel.substr(pos3 + 1, pos4 - pos3 - 1);

        auto& data = j["d"];

        OrderBookSnapshot snap;
        snap.exchange = Exchange::MEXC;
        snap.pair = symbol;
        snap.local_timestamp = std::chrono::steady_clock::now();
        snap.is_delta = false;

        if (data.contains("bids")) {
            for (auto& bid : data["bids"]) {
                if (bid.contains("p") && bid.contains("v")) {
                    double price = std::stod(bid["p"].get<std::string>());
                    double qty = std::stod(bid["v"].get<std::string>());
                    if (qty > 0) snap.bids.push_back({price, qty});
                } else if (bid.is_array() && bid.size() >= 2) {
                    double price = std::stod(bid[0].get<std::string>());
                    double qty = std::stod(bid[1].get<std::string>());
                    if (qty > 0) snap.bids.push_back({price, qty});
                }
            }
        }
        if (data.contains("asks")) {
            for (auto& ask : data["asks"]) {
                if (ask.contains("p") && ask.contains("v")) {
                    double price = std::stod(ask["p"].get<std::string>());
                    double qty = std::stod(ask["v"].get<std::string>());
                    if (qty > 0) snap.asks.push_back({price, qty});
                } else if (ask.is_array() && ask.size() >= 2) {
                    double price = std::stod(ask[0].get<std::string>());
                    double qty = std::stod(ask[1].get<std::string>());
                    if (qty > 0) snap.asks.push_back({price, qty});
                }
            }
        }

        // Sort: bids descending, asks ascending
        std::sort(snap.bids.begin(), snap.bids.end(),
                  [](const auto& a, const auto& b) { return a.price > b.price; });
        std::sort(snap.asks.begin(), snap.asks.end(),
                  [](const auto& a, const auto& b) { return a.price < b.price; });

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = callbacks_.find(symbol);
        if (it != callbacks_.end()) {
            it->second(snap);
        }
    } catch (const std::exception& e) {
        LOG_DEBUG("MEXC WS parse error: {}", e.what());
    }
}
