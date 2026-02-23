#pragma once
#include "exchange/rest_client.h"
#include "exchange/bybit/bybit_auth.h"
#include "common/types.h"
#include <string>
#include <vector>
#include <map>

class BybitRest {
public:
    BybitRest(RestClient& client, BybitAuth& auth);

    // Public endpoints
    OrderBookSnapshot fetch_order_book(const std::string& symbol, int depth = 20);
    std::vector<std::pair<std::string, double>> fetch_tickers();
    FeeInfo fetch_trade_fee(const std::string& symbol);

    // Private endpoints
    OrderResult place_order(const std::string& symbol, const std::string& side,
                           double price, double qty);
    OrderResult cancel_order(const std::string& symbol, const std::string& order_id);
    OrderResult query_order(const std::string& symbol, const std::string& order_id);
    std::vector<BalanceInfo> fetch_balances();

private:
    static constexpr const char* RECV_WINDOW = "5000";
    std::map<std::string, std::string> auth_headers(const std::string& payload);
    RestClient& client_;
    BybitAuth& auth_;
};
