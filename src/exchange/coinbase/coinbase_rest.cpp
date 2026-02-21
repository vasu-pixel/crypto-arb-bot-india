#include "exchange/coinbase/coinbase_rest.h"
#include "common/logger.h"
#include <nlohmann/json.hpp>

CoinbaseRest::CoinbaseRest(RestClient& client, CoinbaseAuth& auth)
    : client_(client), auth_(auth) {}

std::map<std::string, std::string> CoinbaseRest::auth_headers(
    const std::string& method, const std::string& path) {
    return {
        {"Authorization", auth_.auth_header(method, path)},
        {"Content-Type", "application/json"}
    };
}

OrderBookSnapshot CoinbaseRest::fetch_product_book(const std::string& product_id, int limit) {
    std::string path = "/api/v3/brokerage/product_book";
    auto headers = auth_headers("GET", path);
    auto resp = client_.get(path,
                            {{"product_id", product_id}, {"limit", std::to_string(limit)}},
                            headers);

    OrderBookSnapshot snap;
    snap.exchange = Exchange::COINBASE;
    snap.pair = product_id;
    snap.local_timestamp = std::chrono::steady_clock::now();

    try {
        auto j = nlohmann::json::parse(resp.body);
        if (j.contains("pricebook")) {
            auto& book = j["pricebook"];
            if (book.contains("bids")) {
                for (auto& bid : book["bids"]) {
                    double price = std::stod(bid.value("price", "0"));
                    double size = std::stod(bid.value("size", "0"));
                    snap.bids.push_back({price, size});
                }
            }
            if (book.contains("asks")) {
                for (auto& ask : book["asks"]) {
                    double price = std::stod(ask.value("price", "0"));
                    double size = std::stod(ask.value("size", "0"));
                    snap.asks.push_back({price, size});
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Coinbase book parse error: {}", e.what());
    }
    return snap;
}

std::vector<std::pair<std::string, double>> CoinbaseRest::fetch_products() {
    std::string path = "/api/v3/brokerage/products";
    auto headers = auth_headers("GET", path);
    auto resp = client_.get(path, {}, headers);

    std::vector<std::pair<std::string, double>> result;
    try {
        auto j = nlohmann::json::parse(resp.body);
        if (j.contains("products")) {
            for (auto& prod : j["products"]) {
                std::string id = prod.value("product_id", "");
                double volume = 0.0;
                if (prod.contains("volume_24h")) {
                    volume = std::stod(prod["volume_24h"].get<std::string>());
                }
                result.push_back({id, volume});
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Coinbase products parse error: {}", e.what());
    }
    return result;
}

FeeInfo CoinbaseRest::fetch_transaction_summary() {
    std::string path = "/api/v3/brokerage/transaction_summary";
    auto headers = auth_headers("GET", path);
    auto resp = client_.get(path, {}, headers);

    FeeInfo info;
    info.exchange = Exchange::COINBASE;
    info.maker_fee = 0.004;
    info.taker_fee = 0.006;

    try {
        auto j = nlohmann::json::parse(resp.body);
        if (j.contains("fee_tier")) {
            auto& tier = j["fee_tier"];
            if (tier.contains("maker_fee_rate")) {
                info.maker_fee = std::stod(tier["maker_fee_rate"].get<std::string>());
            }
            if (tier.contains("taker_fee_rate")) {
                info.taker_fee = std::stod(tier["taker_fee_rate"].get<std::string>());
            }
        }
    } catch (const std::exception& e) {
        LOG_WARN("Coinbase fee parse error, using defaults: {}", e.what());
    }
    return info;
}

OrderResult CoinbaseRest::create_order(const std::string& product_id, const std::string& side,
                                        double price, double size, const std::string& client_oid) {
    std::string path = "/api/v3/brokerage/orders";
    auto headers = auth_headers("POST", path);

    nlohmann::json body = {
        {"client_order_id", client_oid},
        {"product_id", product_id},
        {"side", side},
        {"order_configuration", {
            {"limit_limit_gtc", {
                {"base_size", std::to_string(size)},
                {"limit_price", std::to_string(price)}
            }}
        }}
    };

    auto resp = client_.post(path, body.dump(), headers);
    OrderResult result;

    try {
        auto j = nlohmann::json::parse(resp.body);
        if (j.contains("success") && j["success"].get<bool>()) {
            result.exchange_order_id = j.value("order_id", "");
            result.status = OrderStatus::OPEN;
        } else {
            result.status = OrderStatus::REJECTED;
            result.error_message = j.value("error_response", nlohmann::json{}).value("message", "Unknown error");
        }
    } catch (const std::exception& e) {
        result.status = OrderStatus::REJECTED;
        result.error_message = e.what();
    }
    return result;
}

OrderResult CoinbaseRest::cancel_order(const std::string& order_id) {
    std::string path = "/api/v3/brokerage/orders/batch_cancel";
    auto headers = auth_headers("POST", path);

    nlohmann::json body = {{"order_ids", {order_id}}};
    auto resp = client_.post(path, body.dump(), headers);

    OrderResult result;
    result.exchange_order_id = order_id;
    result.status = OrderStatus::CANCELLED;
    return result;
}

OrderResult CoinbaseRest::get_order(const std::string& order_id) {
    std::string path = "/api/v3/brokerage/orders/historical/" + order_id;
    auto headers = auth_headers("GET", path);
    auto resp = client_.get(path, {}, headers);

    OrderResult result;
    result.exchange_order_id = order_id;

    try {
        auto j = nlohmann::json::parse(resp.body);
        if (j.contains("order")) {
            auto& order = j["order"];
            std::string status = order.value("status", "");
            if (status == "FILLED") result.status = OrderStatus::FILLED;
            else if (status == "CANCELLED") result.status = OrderStatus::CANCELLED;
            else if (status == "OPEN" || status == "PENDING") result.status = OrderStatus::OPEN;
            else result.status = OrderStatus::PENDING;

            if (order.contains("filled_size")) {
                result.filled_quantity = std::stod(order["filled_size"].get<std::string>());
            }
            if (order.contains("average_filled_price")) {
                result.avg_fill_price = std::stod(order["average_filled_price"].get<std::string>());
            }
            if (order.contains("total_fees")) {
                result.fee_paid = std::stod(order["total_fees"].get<std::string>());
            }
        }
    } catch (const std::exception& e) {
        result.error_message = e.what();
    }
    return result;
}

std::vector<BalanceInfo> CoinbaseRest::fetch_accounts() {
    std::string path = "/api/v3/brokerage/accounts";
    auto headers = auth_headers("GET", path);
    auto resp = client_.get(path, {}, headers);

    std::vector<BalanceInfo> balances;
    try {
        auto j = nlohmann::json::parse(resp.body);
        if (j.contains("accounts")) {
            for (auto& acct : j["accounts"]) {
                BalanceInfo b;
                b.exchange = Exchange::COINBASE;
                b.asset = acct.value("currency", "");
                if (acct.contains("available_balance")) {
                    b.free = std::stod(acct["available_balance"].value("value", "0"));
                }
                if (acct.contains("hold")) {
                    b.locked = std::stod(acct["hold"].value("value", "0"));
                }
                if (b.free > 0 || b.locked > 0) {
                    balances.push_back(b);
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Coinbase accounts parse error: {}", e.what());
    }
    return balances;
}
