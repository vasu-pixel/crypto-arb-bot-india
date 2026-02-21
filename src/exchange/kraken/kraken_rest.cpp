#include "exchange/kraken/kraken_rest.h"
#include "common/logger.h"
#include <nlohmann/json.hpp>

KrakenRest::KrakenRest(RestClient& client, KrakenAuth& auth)
    : client_(client), auth_(auth) {}

std::map<std::string, std::string> KrakenRest::make_auth_headers(
    const std::string& path, const std::string& post_data, uint64_t nonce) {
    std::string signature = auth_.sign_request(path, nonce, post_data);
    return {
        {"API-Key", auth_.get_api_key()},
        {"API-Sign", signature},
        {"Content-Type", "application/x-www-form-urlencoded"}
    };
}

OrderBookSnapshot KrakenRest::fetch_order_book(const std::string& pair, int depth) {
    auto resp = client_.get("/0/public/Depth",
                            {{"pair", pair}, {"count", std::to_string(depth)}});
    OrderBookSnapshot snap;
    snap.exchange = Exchange::KRAKEN;
    snap.pair = pair;
    snap.local_timestamp = std::chrono::steady_clock::now();

    auto j = nlohmann::json::parse(resp.body);
    if (j.contains("error") && !j["error"].empty()) {
        LOG_ERROR("Kraken order book error: {}", j["error"][0].get<std::string>());
        return snap;
    }

    auto& result = j["result"];
    for (auto& [key, data] : result.items()) {
        if (data.contains("bids")) {
            for (auto& bid : data["bids"]) {
                double price = std::stod(bid[0].get<std::string>());
                double qty = std::stod(bid[1].get<std::string>());
                snap.bids.push_back({price, qty});
            }
        }
        if (data.contains("asks")) {
            for (auto& ask : data["asks"]) {
                double price = std::stod(ask[0].get<std::string>());
                double qty = std::stod(ask[1].get<std::string>());
                snap.asks.push_back({price, qty});
            }
        }
    }
    return snap;
}

std::vector<std::pair<std::string, double>> KrakenRest::fetch_all_tickers() {
    auto resp = client_.get("/0/public/Ticker", {});
    std::vector<std::pair<std::string, double>> result;

    auto j = nlohmann::json::parse(resp.body);
    if (j.contains("error") && !j["error"].empty()) {
        LOG_ERROR("Kraken tickers error: {}", j["error"][0].get<std::string>());
        return result;
    }

    for (auto& [pair, data] : j["result"].items()) {
        if (data.contains("v") && data["v"].size() >= 2) {
            double volume = std::stod(data["v"][1].get<std::string>());
            result.push_back({pair, volume});
        }
    }
    return result;
}

std::vector<std::pair<std::string, double>> KrakenRest::fetch_tickers(
    const std::vector<std::string>& pairs) {
    std::string pair_str;
    for (size_t i = 0; i < pairs.size(); ++i) {
        if (i > 0) pair_str += ",";
        pair_str += pairs[i];
    }
    auto resp = client_.get("/0/public/Ticker", {{"pair", pair_str}});
    std::vector<std::pair<std::string, double>> result;

    auto j = nlohmann::json::parse(resp.body);
    if (j.contains("result")) {
        for (auto& [pair, data] : j["result"].items()) {
            if (data.contains("v") && data["v"].size() >= 2) {
                double volume = std::stod(data["v"][1].get<std::string>());
                result.push_back({pair, volume});
            }
        }
    }
    return result;
}

FeeInfo KrakenRest::fetch_trade_fees(const std::string& pair) {
    uint64_t nonce = auth_.next_nonce();
    std::string post_data = "nonce=" + std::to_string(nonce) + "&pair=" + pair;
    auto headers = make_auth_headers("/0/private/TradeVolume", post_data, nonce);
    auto resp = client_.post("/0/private/TradeVolume", post_data, headers);

    FeeInfo info;
    info.exchange = Exchange::KRAKEN;
    info.pair = pair;
    info.maker_fee = 0.0016;
    info.taker_fee = 0.0026;

    try {
        auto j = nlohmann::json::parse(resp.body);
        if (j.contains("result") && j["result"].contains("fees")) {
            auto& fees = j["result"]["fees"];
            for (auto& [p, fee_data] : fees.items()) {
                info.taker_fee = std::stod(fee_data["fee"].get<std::string>()) / 100.0;
                break;
            }
        }
        if (j.contains("result") && j["result"].contains("fees_maker")) {
            auto& fees = j["result"]["fees_maker"];
            for (auto& [p, fee_data] : fees.items()) {
                info.maker_fee = std::stod(fee_data["fee"].get<std::string>()) / 100.0;
                break;
            }
        }
    } catch (const std::exception& e) {
        LOG_WARN("Kraken fee parse error, using defaults: {}", e.what());
    }
    return info;
}

OrderResult KrakenRest::place_order(const std::string& pair, const std::string& side,
                                     const std::string& type, double price, double volume) {
    uint64_t nonce = auth_.next_nonce();
    std::string post_data = "nonce=" + std::to_string(nonce) +
                            "&pair=" + pair +
                            "&type=" + side +
                            "&ordertype=" + type +
                            "&price=" + std::to_string(price) +
                            "&volume=" + std::to_string(volume);
    auto headers = make_auth_headers("/0/private/AddOrder", post_data, nonce);
    auto resp = client_.post("/0/private/AddOrder", post_data, headers);

    OrderResult result;
    try {
        auto j = nlohmann::json::parse(resp.body);
        if (j.contains("error") && !j["error"].empty()) {
            result.status = OrderStatus::REJECTED;
            result.error_message = j["error"][0].get<std::string>();
        } else if (j.contains("result")) {
            auto& r = j["result"];
            if (r.contains("txid") && !r["txid"].empty()) {
                result.exchange_order_id = r["txid"][0].get<std::string>();
                result.status = OrderStatus::OPEN;
            }
        }
    } catch (const std::exception& e) {
        result.status = OrderStatus::REJECTED;
        result.error_message = e.what();
    }
    return result;
}

OrderResult KrakenRest::cancel_order(const std::string& txid) {
    uint64_t nonce = auth_.next_nonce();
    std::string post_data = "nonce=" + std::to_string(nonce) + "&txid=" + txid;
    auto headers = make_auth_headers("/0/private/CancelOrder", post_data, nonce);
    auto resp = client_.post("/0/private/CancelOrder", post_data, headers);

    OrderResult result;
    result.exchange_order_id = txid;
    result.status = OrderStatus::CANCELLED;

    auto j = nlohmann::json::parse(resp.body);
    if (j.contains("error") && !j["error"].empty()) {
        result.error_message = j["error"][0].get<std::string>();
    }
    return result;
}

OrderResult KrakenRest::query_order(const std::string& txid) {
    uint64_t nonce = auth_.next_nonce();
    std::string post_data = "nonce=" + std::to_string(nonce) + "&txid=" + txid;
    auto headers = make_auth_headers("/0/private/QueryOrders", post_data, nonce);
    auto resp = client_.post("/0/private/QueryOrders", post_data, headers);

    OrderResult result;
    result.exchange_order_id = txid;

    try {
        auto j = nlohmann::json::parse(resp.body);
        if (j.contains("result")) {
            for (auto& [id, order] : j["result"].items()) {
                std::string status = order.value("status", "");
                if (status == "closed") result.status = OrderStatus::FILLED;
                else if (status == "canceled") result.status = OrderStatus::CANCELLED;
                else if (status == "open") result.status = OrderStatus::OPEN;
                else result.status = OrderStatus::PENDING;

                if (order.contains("vol_exec")) {
                    result.filled_quantity = std::stod(order["vol_exec"].get<std::string>());
                }
                if (order.contains("price")) {
                    result.avg_fill_price = std::stod(order["price"].get<std::string>());
                }
                if (order.contains("fee")) {
                    result.fee_paid = std::stod(order["fee"].get<std::string>());
                }
            }
        }
    } catch (const std::exception& e) {
        result.error_message = e.what();
    }
    return result;
}

std::vector<BalanceInfo> KrakenRest::fetch_balances() {
    uint64_t nonce = auth_.next_nonce();
    std::string post_data = "nonce=" + std::to_string(nonce);
    auto headers = make_auth_headers("/0/private/Balance", post_data, nonce);
    auto resp = client_.post("/0/private/Balance", post_data, headers);

    std::vector<BalanceInfo> balances;
    try {
        auto j = nlohmann::json::parse(resp.body);
        if (j.contains("result")) {
            for (auto& [asset, amount] : j["result"].items()) {
                BalanceInfo b;
                b.exchange = Exchange::KRAKEN;
                // Normalize Kraken asset names: XXBT->BTC, ZUSD->USD, XETH->ETH
                std::string norm_asset = asset;
                if (asset == "XXBT") norm_asset = "BTC";
                else if (asset == "ZUSD") norm_asset = "USD";
                else if (asset == "XETH") norm_asset = "ETH";
                else if (asset.size() > 1 && (asset[0] == 'X' || asset[0] == 'Z')) {
                    norm_asset = asset.substr(1);
                }
                b.asset = norm_asset;
                b.free = std::stod(amount.get<std::string>());
                b.locked = 0.0;
                balances.push_back(b);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Kraken balance parse error: {}", e.what());
    }
    return balances;
}

std::map<std::string, std::string> KrakenRest::fetch_asset_pairs() {
    auto resp = client_.get("/0/public/AssetPairs", {});
    std::map<std::string, std::string> pairs;
    try {
        auto j = nlohmann::json::parse(resp.body);
        if (j.contains("result")) {
            for (auto& [name, data] : j["result"].items()) {
                if (data.contains("wsname")) {
                    pairs[name] = data["wsname"].get<std::string>();
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Kraken asset pairs error: {}", e.what());
    }
    return pairs;
}
