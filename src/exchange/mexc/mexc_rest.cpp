#include "exchange/mexc/mexc_rest.h"
#include "common/logger.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

MexcRest::MexcRest(RestClient& client, MexcAuth& auth)
    : client_(client), auth_(auth) {}

std::map<std::string, std::string> MexcRest::auth_headers(const std::string& query_string) {
    std::string ts = auth_.get_timestamp();
    std::string full_query = query_string.empty()
        ? "timestamp=" + ts
        : query_string + "&timestamp=" + ts;
    std::string signature = auth_.sign_request(full_query);
    return {
        {"X-MEXC-APIKEY", auth_.get_api_key()},
        {"Content-Type", "application/json"}
    };
}

OrderBookSnapshot MexcRest::fetch_order_book(const std::string& symbol, int depth) {
    auto resp = client_.get("/api/v3/depth",
                            {{"symbol", symbol}, {"limit", std::to_string(depth)}});

    OrderBookSnapshot snap;
    snap.exchange = Exchange::MEXC;
    snap.pair = symbol;
    snap.local_timestamp = std::chrono::steady_clock::now();

    try {
        auto j = json::parse(resp.body);
        if (j.contains("bids")) {
            for (auto& bid : j["bids"]) {
                if (bid.size() >= 2) {
                    snap.bids.push_back({std::stod(bid[0].get<std::string>()),
                                         std::stod(bid[1].get<std::string>())});
                }
            }
        }
        if (j.contains("asks")) {
            for (auto& ask : j["asks"]) {
                if (ask.size() >= 2) {
                    snap.asks.push_back({std::stod(ask[0].get<std::string>()),
                                         std::stod(ask[1].get<std::string>())});
                }
            }
        }
        if (j.contains("lastUpdateId")) {
            snap.sequence_id = j["lastUpdateId"].get<uint64_t>();
        }
    } catch (const std::exception& e) {
        LOG_ERROR("MEXC order book parse error: {}", e.what());
    }
    return snap;
}

std::vector<std::pair<std::string, double>> MexcRest::fetch_tickers() {
    auto resp = client_.get("/api/v3/ticker/24hr");
    std::vector<std::pair<std::string, double>> result;

    try {
        auto j = json::parse(resp.body);
        if (j.is_array()) {
            for (auto& item : j) {
                std::string symbol = item.value("symbol", "");
                double vol = std::stod(item.value("quoteVolume", "0"));
                result.push_back({symbol, vol});
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("MEXC tickers parse error: {}", e.what());
    }
    return result;
}

FeeInfo MexcRest::fetch_trade_fee(const std::string& symbol) {
    FeeInfo info;
    info.exchange = Exchange::MEXC;
    info.pair = symbol;
    // MEXC has 0% maker, 0.05% taker for spot
    info.maker_fee = 0.0;
    info.taker_fee = 0.0005;

    // Try authenticated fee endpoint
    if (!auth_.get_api_key().empty()) {
        try {
            std::string ts = auth_.get_timestamp();
            std::string query = "symbol=" + symbol + "&timestamp=" + ts;
            std::string sig = auth_.sign_request(query);
            auto resp = client_.get("/api/v3/account/commission",
                                    {{"symbol", symbol}, {"timestamp", ts}, {"signature", sig}},
                                    {{"X-MEXC-APIKEY", auth_.get_api_key()}});
            auto j = json::parse(resp.body);
            if (j.contains("makerCommission")) {
                info.maker_fee = j["makerCommission"].get<double>() / 10000.0;
            }
            if (j.contains("takerCommission")) {
                info.taker_fee = j["takerCommission"].get<double>() / 10000.0;
            }
        } catch (const std::exception& e) {
            LOG_WARN("MEXC fee fetch failed, using defaults: {}", e.what());
        }
    }
    return info;
}

OrderResult MexcRest::place_order(const std::string& symbol, const std::string& side,
                                   double price, double qty) {
    std::string ts = auth_.get_timestamp();
    std::string query = "symbol=" + symbol + "&side=" + side + "&type=LIMIT"
        + "&price=" + std::to_string(price) + "&quantity=" + std::to_string(qty)
        + "&timestamp=" + ts;
    std::string sig = auth_.sign_request(query);

    auto resp = client_.post("/api/v3/order?" + query + "&signature=" + sig, "",
                             {{"X-MEXC-APIKEY", auth_.get_api_key()}});

    OrderResult result;
    try {
        auto j = json::parse(resp.body);
        result.exchange_order_id = j.value("orderId", "");
        std::string status = j.value("status", "");
        if (status == "NEW") result.status = OrderStatus::OPEN;
        else if (status == "FILLED") result.status = OrderStatus::FILLED;
        else if (status == "PARTIALLY_FILLED") result.status = OrderStatus::PARTIALLY_FILLED;
        else {
            result.status = OrderStatus::REJECTED;
            result.error_message = j.value("msg", "Unknown error");
        }
    } catch (const std::exception& e) {
        result.status = OrderStatus::REJECTED;
        result.error_message = e.what();
    }
    return result;
}

OrderResult MexcRest::cancel_order(const std::string& symbol, const std::string& order_id) {
    std::string ts = auth_.get_timestamp();
    std::string query = "symbol=" + symbol + "&orderId=" + order_id + "&timestamp=" + ts;
    std::string sig = auth_.sign_request(query);

    client_.del("/api/v3/order?" + query + "&signature=" + sig,
                {{"X-MEXC-APIKEY", auth_.get_api_key()}});

    OrderResult result;
    result.exchange_order_id = order_id;
    result.status = OrderStatus::CANCELLED;
    return result;
}

OrderResult MexcRest::query_order(const std::string& symbol, const std::string& order_id) {
    std::string ts = auth_.get_timestamp();
    std::string query = "symbol=" + symbol + "&orderId=" + order_id + "&timestamp=" + ts;
    std::string sig = auth_.sign_request(query);

    auto resp = client_.get("/api/v3/order",
                            {{"symbol", symbol}, {"orderId", order_id},
                             {"timestamp", ts}, {"signature", sig}},
                            {{"X-MEXC-APIKEY", auth_.get_api_key()}});

    OrderResult result;
    result.exchange_order_id = order_id;
    try {
        auto j = json::parse(resp.body);
        std::string status = j.value("status", "");
        if (status == "FILLED") result.status = OrderStatus::FILLED;
        else if (status == "CANCELED") result.status = OrderStatus::CANCELLED;
        else if (status == "NEW") result.status = OrderStatus::OPEN;
        else if (status == "PARTIALLY_FILLED") result.status = OrderStatus::PARTIALLY_FILLED;
        else result.status = OrderStatus::PENDING;

        if (j.contains("executedQty"))
            result.filled_quantity = std::stod(j["executedQty"].get<std::string>());
        if (j.contains("cummulativeQuoteQty") && j.contains("executedQty")) {
            double exec_qty = std::stod(j["executedQty"].get<std::string>());
            if (exec_qty > 0) {
                double cum_quote = std::stod(j["cummulativeQuoteQty"].get<std::string>());
                result.avg_fill_price = cum_quote / exec_qty;
            }
        }
    } catch (const std::exception& e) {
        result.error_message = e.what();
    }
    return result;
}

std::vector<BalanceInfo> MexcRest::fetch_balances() {
    std::string ts = auth_.get_timestamp();
    std::string query = "timestamp=" + ts;
    std::string sig = auth_.sign_request(query);

    auto resp = client_.get("/api/v3/account",
                            {{"timestamp", ts}, {"signature", sig}},
                            {{"X-MEXC-APIKEY", auth_.get_api_key()}});

    std::vector<BalanceInfo> balances;
    try {
        auto j = json::parse(resp.body);
        if (j.contains("balances")) {
            for (auto& b : j["balances"]) {
                BalanceInfo bi;
                bi.exchange = Exchange::MEXC;
                bi.asset = b.value("asset", "");
                bi.free = std::stod(b.value("free", "0"));
                bi.locked = std::stod(b.value("locked", "0"));
                if (bi.free > 0 || bi.locked > 0) {
                    balances.push_back(bi);
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("MEXC balance parse error: {}", e.what());
    }
    return balances;
}
