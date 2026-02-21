#pragma once
#include "exchange/rest_client.h"
#include "exchange/coinbase/coinbase_auth.h"
#include "common/types.h"
#include <string>
#include <vector>
#include <map>

class CoinbaseRest {
public:
    CoinbaseRest(RestClient& client, CoinbaseAuth& auth);

    OrderBookSnapshot fetch_product_book(const std::string& product_id, int limit = 20);
    std::vector<std::pair<std::string, double>> fetch_products();
    FeeInfo fetch_transaction_summary();
    OrderResult create_order(const std::string& product_id, const std::string& side,
                            double price, double size, const std::string& client_oid);
    OrderResult cancel_order(const std::string& order_id);
    OrderResult get_order(const std::string& order_id);
    std::vector<BalanceInfo> fetch_accounts();

private:
    RestClient& client_;
    CoinbaseAuth& auth_;

    std::map<std::string, std::string> auth_headers(const std::string& method,
                                                     const std::string& path);
};
