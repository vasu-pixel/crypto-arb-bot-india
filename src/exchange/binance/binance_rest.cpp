#include "exchange/binance/binance_rest.h"
#include "common/logger.h"

#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

BinanceRest::BinanceRest(RestClient& client, BinanceAuth& auth)
    : client_(client), auth_(auth) {
    LOG_DEBUG("BinanceRest initialised");
}

std::map<std::string, std::string> BinanceRest::auth_headers() const {
    return {{"X-MBX-APIKEY", auth_.get_api_key()}};
}

// ─── Public: Order Book ─────────────────────────────────────────────────────

OrderBookSnapshot BinanceRest::fetch_order_book(const std::string& symbol, int depth) {
    std::map<std::string, std::string> params = {
        {"symbol", symbol},
        {"limit",  std::to_string(depth)}
    };

    auto resp = client_.get("/api/v3/depth", params);
    if (resp.status_code != 200) {
        LOG_ERROR("BinanceRest: fetch_order_book failed: HTTP {} - {}", resp.status_code, resp.body);
        throw std::runtime_error("Binance fetch_order_book HTTP " + std::to_string(resp.status_code));
    }

    auto j = json::parse(resp.body);
    OrderBookSnapshot snap;
    snap.exchange = Exchange::BINANCE;
    snap.pair = symbol;
    snap.sequence_id = j.value("lastUpdateId", uint64_t(0));
    snap.local_timestamp = std::chrono::steady_clock::now();

    for (const auto& bid : j["bids"]) {
        snap.bids.push_back({std::stod(bid[0].get<std::string>()),
                             std::stod(bid[1].get<std::string>())});
    }
    for (const auto& ask : j["asks"]) {
        snap.asks.push_back({std::stod(ask[0].get<std::string>()),
                             std::stod(ask[1].get<std::string>())});
    }

    LOG_DEBUG("BinanceRest: fetched order book for {} ({} bids, {} asks)",
              symbol, snap.bids.size(), snap.asks.size());
    return snap;
}

// ─── Public: 24h Tickers ────────────────────────────────────────────────────

std::vector<BinanceTicker> BinanceRest::fetch_24h_tickers() {
    auto resp = client_.get("/api/v3/ticker/24hr");
    if (resp.status_code != 200) {
        LOG_ERROR("BinanceRest: fetch_24h_tickers failed: HTTP {} - {}", resp.status_code, resp.body);
        throw std::runtime_error("Binance fetch_24h_tickers HTTP " + std::to_string(resp.status_code));
    }

    auto arr = json::parse(resp.body);
    std::vector<BinanceTicker> tickers;
    tickers.reserve(arr.size());

    for (const auto& item : arr) {
        BinanceTicker t;
        t.symbol = item.value("symbol", "");
        t.volume = std::stod(item.value("quoteVolume", "0"));
        t.last_price = std::stod(item.value("lastPrice", "0"));
        tickers.push_back(std::move(t));
    }

    LOG_DEBUG("BinanceRest: fetched {} tickers", tickers.size());
    return tickers;
}

// ─── Private: Trade Fee ─────────────────────────────────────────────────────

FeeInfo BinanceRest::fetch_trade_fee(const std::string& symbol) {
    std::string qs = "symbol=" + symbol;
    std::string signed_qs = auth_.sign_request(qs);

    auto resp = client_.get("/sapi/v1/asset/tradeFee?" + signed_qs, {}, auth_headers());
    if (resp.status_code != 200) {
        LOG_ERROR("BinanceRest: fetch_trade_fee failed: HTTP {} - {}", resp.status_code, resp.body);
        throw std::runtime_error("Binance fetch_trade_fee HTTP " + std::to_string(resp.status_code));
    }

    auto arr = json::parse(resp.body);
    FeeInfo fee;
    fee.exchange = Exchange::BINANCE;
    fee.pair = symbol;

    if (!arr.empty()) {
        const auto& item = arr[0];
        fee.maker_fee = std::stod(item.value("makerCommission", "0.001"));
        fee.taker_fee = std::stod(item.value("takerCommission", "0.001"));
    } else {
        // Default Binance Global fees
        fee.maker_fee = 0.001;
        fee.taker_fee = 0.001;
    }

    LOG_DEBUG("BinanceRest: fees for {}: maker={}, taker={}", symbol, fee.maker_fee, fee.taker_fee);
    return fee;
}

// ─── Private: Place Order ───────────────────────────────────────────────────

OrderResult BinanceRest::place_order(const std::string& symbol, Side side,
                                     const std::string& type,
                                     double price, double quantity) {
    std::ostringstream qs;
    qs << "symbol=" << symbol
       << "&side=" << (side == Side::BUY ? "BUY" : "SELL")
       << "&type=" << type
       << "&timeInForce=GTC"
       << "&quantity=" << quantity
       << "&price=" << price;

    std::string signed_qs = auth_.sign_request(qs.str());

    auto resp = client_.post("/api/v3/order?" + signed_qs, "", auth_headers());
    if (resp.status_code != 200) {
        LOG_ERROR("BinanceRest: place_order failed: HTTP {} - {}", resp.status_code, resp.body);
        OrderResult result;
        result.status = OrderStatus::REJECTED;
        result.error_message = "HTTP " + std::to_string(resp.status_code) + ": " + resp.body;
        return result;
    }

    auto j = json::parse(resp.body);
    return parse_order_response(j);
}

// ─── Private: Cancel Order ──────────────────────────────────────────────────

OrderResult BinanceRest::cancel_order(const std::string& symbol, const std::string& order_id) {
    std::string qs = "symbol=" + symbol + "&orderId=" + order_id;
    std::string signed_qs = auth_.sign_request(qs);

    auto resp = client_.del("/api/v3/order?" + signed_qs, auth_headers());
    if (resp.status_code != 200) {
        LOG_ERROR("BinanceRest: cancel_order failed: HTTP {} - {}", resp.status_code, resp.body);
        OrderResult result;
        result.status = OrderStatus::REJECTED;
        result.error_message = "HTTP " + std::to_string(resp.status_code) + ": " + resp.body;
        return result;
    }

    auto j = json::parse(resp.body);
    return parse_order_response(j);
}

// ─── Private: Query Order ───────────────────────────────────────────────────

OrderResult BinanceRest::query_order(const std::string& symbol, const std::string& order_id) {
    std::string qs = "symbol=" + symbol + "&orderId=" + order_id;
    std::string signed_qs = auth_.sign_request(qs);

    auto resp = client_.get("/api/v3/order?" + signed_qs, {}, auth_headers());
    if (resp.status_code != 200) {
        LOG_ERROR("BinanceRest: query_order failed: HTTP {} - {}", resp.status_code, resp.body);
        OrderResult result;
        result.status = OrderStatus::REJECTED;
        result.error_message = "HTTP " + std::to_string(resp.status_code) + ": " + resp.body;
        return result;
    }

    auto j = json::parse(resp.body);
    return parse_order_response(j);
}

// ─── Private: Account / Balances ────────────────────────────────────────────

std::vector<BalanceInfo> BinanceRest::fetch_account() {
    std::string signed_qs = auth_.sign_request("");

    auto resp = client_.get("/api/v3/account?" + signed_qs, {}, auth_headers());
    if (resp.status_code != 200) {
        LOG_ERROR("BinanceRest: fetch_account failed: HTTP {} - {}", resp.status_code, resp.body);
        throw std::runtime_error("Binance fetch_account HTTP " + std::to_string(resp.status_code));
    }

    auto j = json::parse(resp.body);
    std::vector<BalanceInfo> balances;

    for (const auto& bal : j["balances"]) {
        double free_amt = std::stod(bal.value("free", "0"));
        double locked_amt = std::stod(bal.value("locked", "0"));

        // Only include non-zero balances
        if (free_amt > 0.0 || locked_amt > 0.0) {
            BalanceInfo info;
            info.exchange = Exchange::BINANCE;
            info.asset = bal.value("asset", "");
            info.free = free_amt;
            info.locked = locked_amt;
            balances.push_back(std::move(info));
        }
    }

    LOG_DEBUG("BinanceRest: fetched {} non-zero balances", balances.size());
    return balances;
}

// ─── Helpers ────────────────────────────────────────────────────────────────

OrderResult BinanceRest::parse_order_response(const json& j) const {
    OrderResult result;
    result.exchange_order_id = std::to_string(j.value("orderId", int64_t(0)));

    std::string status_str = j.value("status", "");
    if (status_str == "NEW") {
        result.status = OrderStatus::OPEN;
    } else if (status_str == "PARTIALLY_FILLED") {
        result.status = OrderStatus::PARTIALLY_FILLED;
    } else if (status_str == "FILLED") {
        result.status = OrderStatus::FILLED;
    } else if (status_str == "CANCELED") {
        result.status = OrderStatus::CANCELLED;
    } else if (status_str == "REJECTED" || status_str == "EXPIRED") {
        result.status = OrderStatus::REJECTED;
    } else {
        result.status = OrderStatus::PENDING;
    }

    result.filled_quantity = std::stod(j.value("executedQty", "0"));

    // Calculate average fill price from cummulativeQuoteQty / executedQty
    double cum_quote = std::stod(j.value("cummulativeQuoteQty", "0"));
    if (result.filled_quantity > 0.0) {
        result.avg_fill_price = cum_quote / result.filled_quantity;
    }

    // Sum fills to get total commission
    if (j.contains("fills")) {
        double total_fee = 0.0;
        for (const auto& fill : j["fills"]) {
            total_fee += std::stod(fill.value("commission", "0"));
        }
        result.fee_paid = total_fee;
    }

    return result;
}
