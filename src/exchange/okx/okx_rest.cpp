#include "exchange/okx/okx_rest.h"
#include "common/logger.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

OkxRest::OkxRest(RestClient& client, OkxAuth& auth)
    : client_(client), auth_(auth) {}

std::map<std::string, std::string> OkxRest::auth_headers(
    const std::string& method, const std::string& path, const std::string& body) {
    std::string ts = auth_.get_timestamp();
    std::string sign = auth_.sign_request(ts, method, path, body);
    return {
        {"OK-ACCESS-KEY", auth_.get_api_key()},
        {"OK-ACCESS-SIGN", sign},
        {"OK-ACCESS-TIMESTAMP", ts},
        {"OK-ACCESS-PASSPHRASE", ""},
        {"Content-Type", "application/json"}
    };
}

OrderBookSnapshot OkxRest::fetch_order_book(const std::string& inst_id, int depth) {
    auto resp = client_.get("/api/v5/market/books",
                            {{"instId", inst_id}, {"sz", std::to_string(depth)}});

    OrderBookSnapshot snap;
    snap.exchange = Exchange::OKX;
    snap.pair = inst_id;
    snap.local_timestamp = std::chrono::steady_clock::now();

    try {
        auto j = json::parse(resp.body);
        if (j.contains("data") && !j["data"].empty()) {
            auto& book = j["data"][0];
            if (book.contains("bids")) {
                for (auto& bid : book["bids"]) {
                    if (bid.size() >= 2) {
                        snap.bids.push_back({std::stod(bid[0].get<std::string>()),
                                             std::stod(bid[1].get<std::string>())});
                    }
                }
            }
            if (book.contains("asks")) {
                for (auto& ask : book["asks"]) {
                    if (ask.size() >= 2) {
                        snap.asks.push_back({std::stod(ask[0].get<std::string>()),
                                             std::stod(ask[1].get<std::string>())});
                    }
                }
            }
            if (book.contains("ts")) {
                snap.sequence_id = std::stoull(book["ts"].get<std::string>());
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("OKX order book parse error: {}", e.what());
    }
    return snap;
}

std::vector<std::pair<std::string, double>> OkxRest::fetch_tickers() {
    auto resp = client_.get("/api/v5/market/tickers", {{"instType", "SPOT"}});
    std::vector<std::pair<std::string, double>> result;

    try {
        auto j = json::parse(resp.body);
        if (j.contains("data")) {
            for (auto& item : j["data"]) {
                std::string inst_id = item.value("instId", "");
                double vol = std::stod(item.value("volCcy24h", "0"));
                result.push_back({inst_id, vol});
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("OKX tickers parse error: {}", e.what());
    }
    return result;
}

FeeInfo OkxRest::fetch_trade_fee(const std::string& inst_id) {
    FeeInfo info;
    info.exchange = Exchange::OKX;
    info.pair = inst_id;
    info.maker_fee = 0.0008;
    info.taker_fee = 0.001;

    try {
        auto headers = auth_headers("GET", "/api/v5/account/trade-fee?instType=SPOT");
        auto resp = client_.get("/api/v5/account/trade-fee",
                                {{"instType", "SPOT"}}, headers);
        auto j = json::parse(resp.body);
        if (j.contains("data") && !j["data"].empty()) {
            auto& fee_data = j["data"][0];
            if (fee_data.contains("maker")) {
                info.maker_fee = std::abs(std::stod(fee_data["maker"].get<std::string>()));
            }
            if (fee_data.contains("taker")) {
                info.taker_fee = std::abs(std::stod(fee_data["taker"].get<std::string>()));
            }
        }
    } catch (const std::exception& e) {
        LOG_WARN("OKX fee fetch failed, using defaults: {}", e.what());
    }
    return info;
}

OrderResult OkxRest::place_order(const std::string& inst_id, const std::string& side,
                                  double price, double size) {
    json body = {
        {"instId", inst_id},
        {"tdMode", "cash"},
        {"side", side},
        {"ordType", "limit"},
        {"px", std::to_string(price)},
        {"sz", std::to_string(size)}
    };
    std::string body_str = body.dump();
    auto headers = auth_headers("POST", "/api/v5/trade/order", body_str);
    auto resp = client_.post("/api/v5/trade/order", body_str, headers);

    OrderResult result;
    try {
        auto j = json::parse(resp.body);
        if (j.contains("data") && !j["data"].empty()) {
            auto& d = j["data"][0];
            result.exchange_order_id = d.value("ordId", "");
            std::string sCode = d.value("sCode", "1");
            if (sCode == "0") {
                result.status = OrderStatus::OPEN;
            } else {
                result.status = OrderStatus::REJECTED;
                result.error_message = d.value("sMsg", "Unknown error");
            }
        }
    } catch (const std::exception& e) {
        result.status = OrderStatus::REJECTED;
        result.error_message = e.what();
    }
    return result;
}

OrderResult OkxRest::cancel_order(const std::string& inst_id, const std::string& order_id) {
    json body = {{"instId", inst_id}, {"ordId", order_id}};
    std::string body_str = body.dump();
    auto headers = auth_headers("POST", "/api/v5/trade/cancel-order", body_str);
    auto resp = client_.post("/api/v5/trade/cancel-order", body_str, headers);

    OrderResult result;
    result.exchange_order_id = order_id;
    result.status = OrderStatus::CANCELLED;
    return result;
}

OrderResult OkxRest::query_order(const std::string& inst_id, const std::string& order_id) {
    auto headers = auth_headers("GET", "/api/v5/trade/order?instId=" + inst_id + "&ordId=" + order_id);
    auto resp = client_.get("/api/v5/trade/order",
                            {{"instId", inst_id}, {"ordId", order_id}}, headers);

    OrderResult result;
    result.exchange_order_id = order_id;
    try {
        auto j = json::parse(resp.body);
        if (j.contains("data") && !j["data"].empty()) {
            auto& d = j["data"][0];
            std::string state = d.value("state", "");
            if (state == "filled") result.status = OrderStatus::FILLED;
            else if (state == "canceled") result.status = OrderStatus::CANCELLED;
            else if (state == "live") result.status = OrderStatus::OPEN;
            else if (state == "partially_filled") result.status = OrderStatus::PARTIALLY_FILLED;
            else result.status = OrderStatus::PENDING;

            if (d.contains("fillSz")) result.filled_quantity = std::stod(d["fillSz"].get<std::string>());
            if (d.contains("avgPx") && d["avgPx"].get<std::string>() != "")
                result.avg_fill_price = std::stod(d["avgPx"].get<std::string>());
            if (d.contains("fee")) result.fee_paid = std::abs(std::stod(d["fee"].get<std::string>()));
        }
    } catch (const std::exception& e) {
        result.error_message = e.what();
    }
    return result;
}

std::vector<BalanceInfo> OkxRest::fetch_balances() {
    auto headers = auth_headers("GET", "/api/v5/account/balance");
    auto resp = client_.get("/api/v5/account/balance", {}, headers);

    std::vector<BalanceInfo> balances;
    try {
        auto j = json::parse(resp.body);
        if (j.contains("data") && !j["data"].empty()) {
            auto& details = j["data"][0]["details"];
            for (auto& d : details) {
                BalanceInfo b;
                b.exchange = Exchange::OKX;
                b.asset = d.value("ccy", "");
                b.free = std::stod(d.value("availBal", "0"));
                b.locked = std::stod(d.value("frozenBal", "0"));
                if (b.free > 0 || b.locked > 0) {
                    balances.push_back(b);
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("OKX balance parse error: {}", e.what());
    }
    return balances;
}
