#pragma once
#include "exchange/rest_client.h"
#include "exchange/gateio/gateio_auth.h"
#include "common/types.h"
#include <string>
#include <vector>
#include <map>

class GateioRest {
public:
    GateioRest(RestClient& client, GateioAuth& auth);

    // Public endpoints
    OrderBookSnapshot fetch_order_book(const std::string& currency_pair, int depth = 20);
    std::vector<std::pair<std::string, double>> fetch_tickers();
    FeeInfo fetch_trade_fee(const std::string& currency_pair);

    // Private endpoints
    OrderResult place_order(const std::string& currency_pair, const std::string& side,
                           double price, double amount);
    OrderResult cancel_order(const std::string& currency_pair, const std::string& order_id);
    OrderResult query_order(const std::string& currency_pair, const std::string& order_id);
    std::vector<BalanceInfo> fetch_balances();

private:
    std::map<std::string, std::string> auth_headers(const std::string& method,
                                                     const std::string& path,
                                                     const std::string& query = "",
                                                     const std::string& body = "");
    RestClient& client_;
    GateioAuth& auth_;
};
