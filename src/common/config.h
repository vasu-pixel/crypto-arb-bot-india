#pragma once
#include "types.h"
#include <string>
#include <map>
#include <set>

struct ExchangeConfig {
    std::string api_key;
    std::string secret_key;
    std::string rest_base_url;
    std::string ws_base_url;
};

struct PaperRealismConfig {
    // Gap 1: Latency simulation (no actual sleep — re-snapshots book)
    bool enable_latency = true;
    double latency_mean_ms = 150.0;
    double latency_stddev_ms = 80.0;

    // Gap 2: Adverse slippage beyond VWAP (half-normal, adverse direction only)
    bool enable_adverse_slippage = true;
    double slippage_bps_mean = 1.0;
    double slippage_bps_stddev = 0.5;

    // Gap 3: Staleness penalty at execution time
    bool enable_staleness_penalty = true;
    double staleness_penalty_bps_per_sec = 2.0;
    double max_book_age_ms = 3000.0;

    // Gap 4: Realistic rebalance with transfer delays
    bool enable_realistic_rebalance = true;
    double rebalance_delay_minutes = 30.0;

    // Gap 5: Withdrawal fees (flat per-asset + percentage)
    bool enable_withdrawal_fees = true;
    double withdrawal_fee_pct = 0.1;
    std::map<std::string, double> withdrawal_flat_fees;

    // Gap 6: Market impact — phantom depth consumption with exponential decay
    bool enable_market_impact = true;
    double impact_decay_seconds = 5.0;

    // Gap 7: Competition modeling — fill probability based on spread attractiveness
    bool enable_competition = true;
    double competition_base_prob = 0.85;
    double competition_decay_bps = 5.0;

    // Gap 8: Rate limits per exchange
    bool enable_rate_limits = true;
    int max_orders_per_second = 10;
    int max_orders_per_minute = 300;

    // Gap 9: Minimum order sizes
    bool enable_min_order_size = true;
    std::map<std::string, double> min_order_sizes;
    double default_min_notional_usd = 5.0;

    // Gap 10: One-leg risk — probability of second leg failing
    bool enable_one_leg_risk = true;
    double one_leg_probability = 0.05;
    double one_leg_unwind_slippage_bps = 5.0;
};

struct Config {
    std::map<Exchange, ExchangeConfig> exchanges;
    TradingMode mode = TradingMode::PAPER;

    // Strategy
    double min_net_spread_bps = 3.0;
    double min_trade_size_usd = 50.0;
    double max_trade_size_usd = 5000.0;
    int max_open_positions = 3;
    int order_timeout_ms = 5000;
    int partial_fill_wait_ms = 2000;

    // Inventory
    double drift_threshold_pct = 20.0;
    int balance_refresh_interval_s = 30;

    // Server
    uint16_t ws_port = 9003;
    int heartbeat_interval_s = 5;

    // Persistence
    std::string trades_file = "data/trades.json";
    std::string paper_trades_file = "data/paper_trades.json";

    // Logging
    std::string log_level = "info";
    std::string log_file = "logs/arb_bot.log";

    // Data recording (for backtesting)
    bool record_order_books = false;
    std::string historical_data_dir = "data/historical";

    // Paper trading
    std::map<std::string, double> paper_initial_balances;
    std::set<Exchange> paper_excluded_exchanges;
    PaperRealismConfig paper_realism;

    static Config load(const std::string& filepath);
};
