#pragma once
#include "exchange/exchange_interface.h"
#include "exchange/rest_client.h"
#include "exchange/ws_client.h"
#include "exchange/kraken/kraken_auth.h"
#include "exchange/kraken/kraken_rest.h"
#include "exchange/kraken/kraken_ws.h"
#include "common/config.h"
#include <memory>
#include <unordered_map>

class KrakenAdapter : public IExchange {
public:
    explicit KrakenAdapter(const Config& config);
    ~KrakenAdapter() override = default;

    Exchange exchange_id() const override { return Exchange::KRAKEN; }
    std::string exchange_name() const override { return "KRAKEN"; }
    std::string normalize_pair(const std::string& canonical_pair) const override;
    std::string canonical_pair(const std::string& native_pair) const override;

    OrderBookSnapshot fetch_order_book(const std::string& pair, int depth = 20) override;
    std::vector<std::pair<std::string, double>> fetch_top_pairs_by_volume(int limit = 10) override;
    FeeInfo fetch_fees(const std::string& pair) override;
    OrderResult place_limit_order(const OrderRequest& req) override;
    OrderResult cancel_order(const std::string& pair, const std::string& order_id) override;
    OrderResult query_order(const std::string& pair, const std::string& order_id) override;
    std::vector<BalanceInfo> fetch_balances() override;

    void subscribe_order_book(const std::string& pair, OrderBookCallback cb) override;
    void unsubscribe_order_book(const std::string& pair) override;

    void connect() override;
    void disconnect() override;
    bool is_connected() const override;

private:
    ExchangeConfig config_;
    std::unique_ptr<RestClient> rest_client_;
    std::unique_ptr<KrakenAuth> auth_;
    std::unique_ptr<KrakenRest> rest_;
    std::unique_ptr<ExchangeWsClient> ws_client_;
    std::unique_ptr<KrakenWs> ws_;

    // Maps: canonical -> Kraken REST pair, canonical -> Kraken WS pair
    std::unordered_map<std::string, std::string> rest_pair_map_;
    std::unordered_map<std::string, std::string> ws_pair_map_;

    void build_pair_maps();
};
