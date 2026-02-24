#include "exchange/gateio/gateio_rest.h"
#include "common/logger.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

GateioRest::GateioRest(RestClient& client, GateioAuth& auth)
    : client_(client), auth_(auth) {}

std::map<std::string, std::string> GateioRest::auth_headers(
    const std::string& method, const std::string& path,
    const std::string& query, const std::string& body) {
    std::string ts = auth_.get_timestamp();
    std::string sign = auth_.sign_request(method, path, query, body);
    return {
        {"KEY", auth_.get_api_key()},
        {"SIGN", sign},
        {"Timestamp", ts},
        {"Content-Type", "application/json"}
    };
}

OrderBookSnapshot GateioRest::fetch_order_book(const std::string& currency_pair, int depth) {
    auto resp = client_.get("/api/v4/spot/order_book",
                            {{"currency_pair", currency_pair},
                             {"limit", std::to_string(depth)}});

    OrderBookSnapshot snap;
    snap.exchange = Exchange::GATEIO;
    snap.pair = currency_pair;
    snap.local_timestamp = std::chrono::steady_clock::now();

    try {
        auto j = json::parse(resp.body);
        if (j.contains("bids")) {
            for (auto& bid : j["bids"]) {
                if (bid.is_array() && bid.size() >= 2) {
                    snap.bids.push_back({std::stod(bid[0].get<std::string>()),
                                         std::stod(bid[1].get<std::string>())});
                }
            }
        }
        if (j.contains("asks")) {
            for (auto& ask : j["asks"]) {
                if (ask.is_array() && ask.size() >= 2) {
                    snap.asks.push_back({std::stod(ask[0].get<std::string>()),
                                         std::stod(ask[1].get<std::string>())});
                }
            }
        }
        if (j.contains("id")) {
            snap.sequence_id = j["id"].get<uint64_t>();
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Gate.io order book parse error: {}", e.what());
    }
    return snap;
}

std::vector<std::pair<std::string, double>> GateioRest::fetch_tickers() {
    auto resp = client_.get("/api/v4/spot/tickers");
    std::vector<std::pair<std::string, double>> result;

    try {
        auto j = json::parse(resp.body);
        if (j.is_array()) {
            for (auto& item : j) {
                std::string pair = item.value("currency_pair", "");
                double vol = std::stod(item.value("quote_volume", "0"));
                result.push_back({pair, vol});
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Gate.io tickers parse error: {}", e.what());
    }
    return result;
}

FeeInfo GateioRest::fetch_trade_fee(const std::string& currency_pair) {
    FeeInfo info;
    info.exchange = Exchange::GATEIO;
    info.pair = currency_pair;
    // Gate.io default: 0.2% maker, 0.2% taker (VIP0); commonly 0.02%/0.075% for moderate volume
    info.maker_fee = 0.0002;
    info.taker_fee = 0.00075;

    if (!auth_.get_api_key().empty()) {
        try {
            auto headers = auth_headers("GET", "/api/v4/wallet/fee",
                                        "currency_pair=" + currency_pair);
            auto resp = client_.get("/api/v4/wallet/fee",
                                    {{"currency_pair", currency_pair}}, headers);
            auto j = json::parse(resp.body);
            if (j.contains("maker_fee")) {
                info.maker_fee = std::stod(j["maker_fee"].get<std::string>());
            }
            if (j.contains("taker_fee")) {
                info.taker_fee = std::stod(j["taker_fee"].get<std::string>());
            }
        } catch (const std::exception& e) {
            LOG_WARN("Gate.io fee fetch failed, using defaults: {}", e.what());
        }
    }
    return info;
}

OrderResult GateioRest::place_order(const std::string& currency_pair, const std::string& side,
                                     double price, double amount) {
    json body = {
        {"currency_pair", currency_pair},
        {"side", side},
        {"type", "limit"},
        {"price", std::to_string(price)},
        {"amount", std::to_string(amount)},
        {"time_in_force", "gtc"}
    };
    std::string body_str = body.dump();
    auto headers = auth_headers("POST", "/api/v4/spot/orders", "", body_str);
    auto resp = client_.post("/api/v4/spot/orders", body_str, headers);

    OrderResult result;
    try {
        auto j = json::parse(resp.body);
        result.exchange_order_id = j.value("id", "");
        std::string status = j.value("status", "");
        if (status == "open") result.status = OrderStatus::OPEN;
        else if (status == "closed") result.status = OrderStatus::FILLED;
        else {
            result.status = OrderStatus::REJECTED;
            result.error_message = j.value("message", "Unknown error");
        }
    } catch (const std::exception& e) {
        result.status = OrderStatus::REJECTED;
        result.error_message = e.what();
    }
    return result;
}

OrderResult GateioRest::cancel_order(const std::string& currency_pair, const std::string& order_id) {
    auto headers = auth_headers("DELETE", "/api/v4/spot/orders/" + order_id,
                                "currency_pair=" + currency_pair);
    client_.del("/api/v4/spot/orders/" + order_id + "?currency_pair=" + currency_pair, headers);

    OrderResult result;
    result.exchange_order_id = order_id;
    result.status = OrderStatus::CANCELLED;
    return result;
}

OrderResult GateioRest::query_order(const std::string& currency_pair, const std::string& order_id) {
    auto headers = auth_headers("GET", "/api/v4/spot/orders/" + order_id,
                                "currency_pair=" + currency_pair);
    auto resp = client_.get("/api/v4/spot/orders/" + order_id,
                            {{"currency_pair", currency_pair}}, headers);

    OrderResult result;
    result.exchange_order_id = order_id;
    try {
        auto j = json::parse(resp.body);
        std::string status = j.value("status", "");
        if (status == "closed") result.status = OrderStatus::FILLED;
        else if (status == "cancelled") result.status = OrderStatus::CANCELLED;
        else if (status == "open") result.status = OrderStatus::OPEN;
        else result.status = OrderStatus::PENDING;

        if (j.contains("filled_total"))
            result.filled_quantity = std::stod(j["filled_total"].get<std::string>());
        if (j.contains("avg_deal_price"))
            result.avg_fill_price = std::stod(j["avg_deal_price"].get<std::string>());
        if (j.contains("fee"))
            result.fee_paid = std::stod(j["fee"].get<std::string>());
    } catch (const std::exception& e) {
        result.error_message = e.what();
    }
    return result;
}

std::vector<BalanceInfo> GateioRest::fetch_balances() {
    auto headers = auth_headers("GET", "/api/v4/spot/accounts");
    auto resp = client_.get("/api/v4/spot/accounts", {}, headers);

    std::vector<BalanceInfo> balances;
    try {
        auto j = json::parse(resp.body);
        if (j.is_array()) {
            for (auto& item : j) {
                BalanceInfo b;
                b.exchange = Exchange::GATEIO;
                b.asset = item.value("currency", "");
                b.free = std::stod(item.value("available", "0"));
                b.locked = std::stod(item.value("locked", "0"));
                if (b.free > 0 || b.locked > 0) {
                    balances.push_back(b);
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Gate.io balance parse error: {}", e.what());
    }
    return balances;
}
