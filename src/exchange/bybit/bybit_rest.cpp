#include "exchange/bybit/bybit_rest.h"
#include "common/logger.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

BybitRest::BybitRest(RestClient& client, BybitAuth& auth)
    : client_(client), auth_(auth) {}

std::map<std::string, std::string> BybitRest::auth_headers(const std::string& payload) {
    std::string ts = auth_.get_timestamp();
    std::string sign = auth_.sign_request(ts, RECV_WINDOW, payload);
    return {
        {"X-BAPI-API-KEY", auth_.get_api_key()},
        {"X-BAPI-SIGN", sign},
        {"X-BAPI-TIMESTAMP", ts},
        {"X-BAPI-RECV-WINDOW", RECV_WINDOW},
        {"Content-Type", "application/json"}
    };
}

OrderBookSnapshot BybitRest::fetch_order_book(const std::string& symbol, int depth) {
    auto resp = client_.get("/v5/market/orderbook",
                            {{"category", "spot"}, {"symbol", symbol},
                             {"limit", std::to_string(depth)}});

    OrderBookSnapshot snap;
    snap.exchange = Exchange::BYBIT;
    snap.pair = symbol;
    snap.local_timestamp = std::chrono::steady_clock::now();

    try {
        auto j = json::parse(resp.body);
        if (j.contains("result")) {
            auto& result = j["result"];
            if (result.contains("b")) {
                for (auto& bid : result["b"]) {
                    if (bid.size() >= 2) {
                        snap.bids.push_back({std::stod(bid[0].get<std::string>()),
                                             std::stod(bid[1].get<std::string>())});
                    }
                }
            }
            if (result.contains("a")) {
                for (auto& ask : result["a"]) {
                    if (ask.size() >= 2) {
                        snap.asks.push_back({std::stod(ask[0].get<std::string>()),
                                             std::stod(ask[1].get<std::string>())});
                    }
                }
            }
            if (result.contains("ts")) {
                snap.sequence_id = std::stoull(result["ts"].get<std::string>());
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Bybit order book parse error: {}", e.what());
    }
    return snap;
}

std::vector<std::pair<std::string, double>> BybitRest::fetch_tickers() {
    auto resp = client_.get("/v5/market/tickers", {{"category", "spot"}});
    std::vector<std::pair<std::string, double>> result;

    try {
        auto j = json::parse(resp.body);
        if (j.contains("result") && j["result"].contains("list")) {
            for (auto& item : j["result"]["list"]) {
                std::string symbol = item.value("symbol", "");
                double vol = std::stod(item.value("turnover24h", "0"));
                result.push_back({symbol, vol});
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Bybit tickers parse error: {}", e.what());
    }
    return result;
}

FeeInfo BybitRest::fetch_trade_fee(const std::string& symbol) {
    FeeInfo info;
    info.exchange = Exchange::BYBIT;
    info.pair = symbol;
    info.maker_fee = 0.001;
    info.taker_fee = 0.001;

    try {
        std::string query = "category=spot&symbol=" + symbol;
        auto headers = auth_headers(query);
        auto resp = client_.get("/v5/account/fee-rate",
                                {{"category", "spot"}, {"symbol", symbol}}, headers);
        auto j = json::parse(resp.body);
        if (j.contains("result") && j["result"].contains("list") && !j["result"]["list"].empty()) {
            auto& fee_data = j["result"]["list"][0];
            if (fee_data.contains("makerFeeRate")) {
                info.maker_fee = std::abs(std::stod(fee_data["makerFeeRate"].get<std::string>()));
            }
            if (fee_data.contains("takerFeeRate")) {
                info.taker_fee = std::abs(std::stod(fee_data["takerFeeRate"].get<std::string>()));
            }
        }
    } catch (const std::exception& e) {
        LOG_WARN("Bybit fee fetch failed, using defaults: {}", e.what());
    }
    return info;
}

OrderResult BybitRest::place_order(const std::string& symbol, const std::string& side,
                                    double price, double qty) {
    json body = {
        {"category", "spot"},
        {"symbol", symbol},
        {"side", side == "buy" ? "Buy" : "Sell"},
        {"orderType", "Limit"},
        {"price", std::to_string(price)},
        {"qty", std::to_string(qty)}
    };
    std::string body_str = body.dump();
    auto headers = auth_headers(body_str);
    auto resp = client_.post("/v5/order/create", body_str, headers);

    OrderResult result;
    try {
        auto j = json::parse(resp.body);
        int ret_code = j.value("retCode", -1);
        if (ret_code == 0 && j.contains("result")) {
            result.exchange_order_id = j["result"].value("orderId", "");
            result.status = OrderStatus::OPEN;
        } else {
            result.status = OrderStatus::REJECTED;
            result.error_message = j.value("retMsg", "Unknown error");
        }
    } catch (const std::exception& e) {
        result.status = OrderStatus::REJECTED;
        result.error_message = e.what();
    }
    return result;
}

OrderResult BybitRest::cancel_order(const std::string& symbol, const std::string& order_id) {
    json body = {
        {"category", "spot"},
        {"symbol", symbol},
        {"orderId", order_id}
    };
    std::string body_str = body.dump();
    auto headers = auth_headers(body_str);
    auto resp = client_.post("/v5/order/cancel", body_str, headers);

    OrderResult result;
    result.exchange_order_id = order_id;
    result.status = OrderStatus::CANCELLED;
    return result;
}

OrderResult BybitRest::query_order(const std::string& symbol, const std::string& order_id) {
    std::string query = "category=spot&orderId=" + order_id;
    auto headers = auth_headers(query);
    auto resp = client_.get("/v5/order/realtime",
                            {{"category", "spot"}, {"orderId", order_id}}, headers);

    OrderResult result;
    result.exchange_order_id = order_id;
    try {
        auto j = json::parse(resp.body);
        if (j.contains("result") && j["result"].contains("list") && !j["result"]["list"].empty()) {
            auto& d = j["result"]["list"][0];
            std::string status = d.value("orderStatus", "");
            if (status == "Filled") result.status = OrderStatus::FILLED;
            else if (status == "Cancelled") result.status = OrderStatus::CANCELLED;
            else if (status == "New") result.status = OrderStatus::OPEN;
            else if (status == "PartiallyFilled") result.status = OrderStatus::PARTIALLY_FILLED;
            else result.status = OrderStatus::PENDING;

            if (d.contains("cumExecQty")) result.filled_quantity = std::stod(d["cumExecQty"].get<std::string>());
            if (d.contains("avgPrice") && d["avgPrice"].get<std::string>() != "")
                result.avg_fill_price = std::stod(d["avgPrice"].get<std::string>());
            if (d.contains("cumExecFee")) result.fee_paid = std::abs(std::stod(d["cumExecFee"].get<std::string>()));
        }
    } catch (const std::exception& e) {
        result.error_message = e.what();
    }
    return result;
}

std::vector<BalanceInfo> BybitRest::fetch_balances() {
    std::string query = "accountType=UNIFIED";
    auto headers = auth_headers(query);
    auto resp = client_.get("/v5/account/wallet-balance", {{"accountType", "UNIFIED"}}, headers);

    std::vector<BalanceInfo> balances;
    try {
        auto j = json::parse(resp.body);
        if (j.contains("result") && j["result"].contains("list") && !j["result"]["list"].empty()) {
            auto& coins = j["result"]["list"][0]["coin"];
            for (auto& c : coins) {
                BalanceInfo b;
                b.exchange = Exchange::BYBIT;
                b.asset = c.value("coin", "");
                b.free = std::stod(c.value("availableToWithdraw", "0"));
                b.locked = std::stod(c.value("locked", "0"));
                if (b.free > 0 || b.locked > 0) {
                    balances.push_back(b);
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Bybit balance parse error: {}", e.what());
    }
    return balances;
}
