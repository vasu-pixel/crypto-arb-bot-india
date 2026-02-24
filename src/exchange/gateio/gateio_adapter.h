#pragma once
#include "exchange/exchange_interface.h"
#include "exchange/rest_client.h"
#include "exchange/ws_client.h"
#include "exchange/gateio/gateio_auth.h"
#include "exchange/gateio/gateio_rest.h"
#include "exchange/gateio/gateio_ws.h"
#include "common/config.h"
#include <memory>
#include <unordered_map>

class GateioAdapter : public IExchange {
public:
    explicit GateioAdapter(const Config& config);
    ~GateioAdapter() override = default;

    Exchange exchange_id() const override { return Exchange::GATEIO; }
    std::string exchange_name() const override { return "GATEIO"; }
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
    std::unique_ptr<GateioAuth> auth_;
    std::unique_ptr<GateioRest> rest_;
    std::unique_ptr<ExchangeWsClient> ws_client_;
    std::unique_ptr<GateioWs> ws_;
    std::unordered_map<std::string, std::string> pair_map_;
    void build_pair_map();
};
