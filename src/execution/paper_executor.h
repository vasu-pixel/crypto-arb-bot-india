#pragma once
#include "common/config.h"
#include "common/types.h"
#include "strategy/fee_manager.h"
#include "orderbook/order_book_aggregator.h"
#include "persistence/trade_logger.h"
#include <map>
#include <deque>
#include <unordered_map>
#include <shared_mutex>
#include <random>
#include <vector>
#include <chrono>
#include <cmath>

class PaperExecutor {
public:
    PaperExecutor(const std::map<std::string, double>& initial_balances,
                  const std::vector<Exchange>& active_exchanges,
                  OrderBookAggregator& aggregator,
                  FeeManager& fee_manager,
                  TradeLogger& trade_logger,
                  const PaperRealismConfig& realism = PaperRealismConfig{});

    TradeRecord execute(const ArbitrageOpportunity& opp);

    // Redistribute assets across exchanges (delayed if realism enabled)
    void rebalance();

    // Settle any pending transfers whose delay has elapsed
    void settle_pending_transfers();

    std::map<Exchange, std::unordered_map<std::string, double>> get_virtual_balances() const;
    double get_virtual_pnl() const;

private:
    // --- Fill simulation ---
    OrderResult simulate_fill(const OrderRequest& req, const OrderBookSnapshot& book,
                              double taker_fee_rate, double additional_adverse_bps,
                              double net_spread_bps);
    bool check_virtual_balance(Exchange exch, const std::string& asset, double amount) const;
    double get_virtual_balance_amount(Exchange exch, const std::string& asset) const;
    void update_virtual_balance(Exchange exch, const std::string& asset, double delta);
    std::string extract_base_asset(const std::string& pair) const;
    std::string extract_quote_asset(const std::string& pair) const;

    // --- Realism helpers ---
    double sample_latency_ms(Exchange exch);
    OrderBookSnapshot apply_phantom_to_snapshot(const OrderBookSnapshot& snap,
                                                 Exchange exch, const std::string& pair,
                                                 Side side);
    void record_phantom_fill(Exchange exch, const std::string& pair, Side side, double qty);
    void cleanup_phantom_fills();
    bool check_rate_limit(Exchange exch);
    void record_rate_limit_hit(Exchange exch);

    // --- Core state ---
    std::map<Exchange, std::unordered_map<std::string, double>> virtual_balances_;
    OrderBookAggregator& aggregator_;
    FeeManager& fee_manager_;
    TradeLogger& trade_logger_;
    mutable std::shared_mutex mutex_;
    double total_pnl_ = 0.0;
    std::mt19937 rng_;
    PaperRealismConfig realism_;

    // --- Gap 4/5: Pending transfers ---
    struct PendingTransfer {
        Exchange to_exchange;
        std::string asset;
        double amount;  // Net of fees
        std::chrono::steady_clock::time_point available_at;
    };
    std::vector<PendingTransfer> pending_transfers_;

    // --- Gap 6: Market impact tracking ---
    struct PhantomFill {
        Exchange exchange;
        std::string pair;
        Side side;
        double quantity;
        std::chrono::steady_clock::time_point filled_at;
    };
    std::vector<PhantomFill> recent_fills_;

    // --- Gap 8: Rate limit state ---
    struct RateLimitState {
        std::deque<std::chrono::steady_clock::time_point> order_timestamps;
    };
    std::map<Exchange, RateLimitState> rate_limit_state_;
};
