#pragma once
#include "exchange/rest_client.h"
#include "exchange/mexc/mexc_auth.h"
#include "common/types.h"
#include <string>
#include <vector>
#include <map>

class MexcRest {
public:
    MexcRest(RestClient& client, MexcAuth& auth);

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
    std::map<std::string, std::string> auth_headers(const std::string& query_string);
    RestClient& client_;
    MexcAuth& auth_;
};
