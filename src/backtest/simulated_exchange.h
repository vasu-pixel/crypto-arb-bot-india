#pragma once
#include "exchange/exchange_interface.h"
#include "common/types.h"
#include <map>
#include <vector>
#include <string>

class SimulatedExchange : public IExchange {
public:
    SimulatedExchange(Exchange exch_id, const std::string& name,
                      const std::map<std::string, double>& initial_balances,
                      double maker_fee = 0.001, double taker_fee = 0.001);

    void load_snapshots(const std::vector<HistoricalSnapshot>& snapshots);
    void advance_to(uint64_t timestamp_ms);

    // IExchange interface
    Exchange exchange_id() const override { return exch_id_; }
    std::string exchange_name() const override { return name_; }
    std::string normalize_pair(const std::string& canonical_pair) const override { return canonical_pair; }
    std::string canonical_pair(const std::string& native_pair) const override { return native_pair; }

    OrderBookSnapshot fetch_order_book(const std::string& pair, int depth = 20) override;
    std::vector<std::pair<std::string, double>> fetch_top_pairs_by_volume(int limit = 10) override;
    FeeInfo fetch_fees(const std::string& pair) override;
    OrderResult place_limit_order(const OrderRequest& req) override;
    OrderResult cancel_order(const std::string& pair, const std::string& order_id) override;
    OrderResult query_order(const std::string& pair, const std::string& order_id) override;
    std::vector<BalanceInfo> fetch_balances() override;

    void subscribe_order_book(const std::string& pair, OrderBookCallback cb) override {}
    void unsubscribe_order_book(const std::string& pair) override {}
    void connect() override {}
    void disconnect() override {}
    bool is_connected() const override { return true; }

private:
    Exchange exch_id_;
    std::string name_;
    std::map<std::string, double> balances_;
    double maker_fee_, taker_fee_;

    std::map<std::string, std::vector<HistoricalSnapshot>> snapshots_by_pair_;
    std::map<std::string, size_t> current_index_;
    uint64_t current_time_ms_ = 0;

    OrderBookSnapshot current_book(const std::string& pair) const;
};
