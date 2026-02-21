#pragma once
#include "exchange/rest_client.h"
#include "exchange/kraken/kraken_auth.h"
#include "common/types.h"
#include <string>
#include <vector>
#include <map>

class KrakenRest {
public:
    KrakenRest(RestClient& client, KrakenAuth& auth);

    OrderBookSnapshot fetch_order_book(const std::string& pair, int depth = 20);
    std::vector<std::pair<std::string, double>> fetch_tickers(const std::vector<std::string>& pairs);
    std::vector<std::pair<std::string, double>> fetch_all_tickers();
    FeeInfo fetch_trade_fees(const std::string& pair);
    OrderResult place_order(const std::string& pair, const std::string& side,
                           const std::string& type, double price, double volume);
    OrderResult cancel_order(const std::string& txid);
    OrderResult query_order(const std::string& txid);
    std::vector<BalanceInfo> fetch_balances();
    std::map<std::string, std::string> fetch_asset_pairs();

private:
    RestClient& client_;
    KrakenAuth& auth_;

    std::map<std::string, std::string> make_auth_headers(const std::string& path,
                                                          const std::string& post_data,
                                                          uint64_t nonce);
};
