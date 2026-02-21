#pragma once

#include "common/types.h"
#include "exchange/rest_client.h"
#include "exchange/binance/binance_auth.h"

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// Raw ticker data returned by Binance 24hr endpoint
struct BinanceTicker {
    std::string symbol;
    double volume = 0.0;        // quoteVolume (USD volume)
    double last_price = 0.0;
};

class BinanceRest {
public:
    BinanceRest(RestClient& client, BinanceAuth& auth);

    // ── Public endpoints ────────────────────────────────────────────────
    OrderBookSnapshot fetch_order_book(const std::string& symbol, int depth = 20);
    std::vector<BinanceTicker> fetch_24h_tickers();

    // ── Private endpoints ───────────────────────────────────────────────
    FeeInfo fetch_trade_fee(const std::string& symbol);
    OrderResult place_order(const std::string& symbol, Side side,
                            const std::string& type,
                            double price, double quantity);
    OrderResult cancel_order(const std::string& symbol, const std::string& order_id);
    OrderResult query_order(const std::string& symbol, const std::string& order_id);
    std::vector<BalanceInfo> fetch_account();

private:
    std::map<std::string, std::string> auth_headers() const;
    OrderResult parse_order_response(const nlohmann::json& j) const;

    RestClient& client_;
    BinanceAuth& auth_;
};
