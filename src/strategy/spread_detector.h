#pragma once

#include "common/types.h"
#include "orderbook/order_book_aggregator.h"
#include "strategy/fee_manager.h"

#include <functional>
#include <string>
#include <thread>
#include <atomic>

class SpreadDetector {
public:
    using OpportunityCallback = std::function<void(const ArbitrageOpportunity&)>;

    SpreadDetector(OrderBookAggregator& aggregator,
                   FeeManager& fee_manager,
                   double min_net_spread_bps,
                   double min_trade_size_usd,
                   double max_trade_size_usd);

    ~SpreadDetector();

    void set_opportunity_callback(OpportunityCallback cb);

    // Start the monitoring thread (~100ms poll interval).
    void start();

    // Stop the monitoring thread.
    void stop();

private:
    // Main loop: iterates all pairs, calls scan_pair on each.
    void monitor_loop();

    // For a given pair, checks all exchange permutations for arbitrage.
    void scan_pair(const std::string& pair);

    OrderBookAggregator& aggregator_;
    FeeManager& fee_manager_;
    double min_net_spread_bps_;
    double min_trade_size_usd_;
    double max_trade_size_usd_;

    OpportunityCallback callback_;
    std::atomic<bool> running_{false};
    std::thread monitor_thread_;
};
