#pragma once
#include "common/types.h"
#include "strategy/fee_manager.h"
#include "orderbook/order_book_aggregator.h"
#include "persistence/trade_logger.h"
#include <map>
#include <unordered_map>
#include <shared_mutex>
#include <random>

class PaperExecutor {
public:
    PaperExecutor(const std::map<std::string, double>& initial_balances,
                  const std::vector<Exchange>& active_exchanges,
                  OrderBookAggregator& aggregator,
                  FeeManager& fee_manager,
                  TradeLogger& trade_logger);

    TradeRecord execute(const ArbitrageOpportunity& opp);

    // Redistribute all assets equally across exchanges (simulates internal transfers)
    void rebalance();

    std::map<Exchange, std::unordered_map<std::string, double>> get_virtual_balances() const;
    double get_virtual_pnl() const;

private:
    OrderResult simulate_fill(const OrderRequest& req, const OrderBookSnapshot& book,
                              double taker_fee_rate);
    bool check_virtual_balance(Exchange exch, const std::string& asset, double amount) const;
    void update_virtual_balance(Exchange exch, const std::string& asset, double delta);
    std::string extract_base_asset(const std::string& pair) const;
    std::string extract_quote_asset(const std::string& pair) const;

    std::map<Exchange, std::unordered_map<std::string, double>> virtual_balances_;
    OrderBookAggregator& aggregator_;
    FeeManager& fee_manager_;
    TradeLogger& trade_logger_;
    mutable std::shared_mutex mutex_;
    double total_pnl_ = 0.0;
    std::mt19937 rng_;
};
