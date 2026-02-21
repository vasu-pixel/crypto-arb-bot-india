#pragma once
#include "types.h"
#include <string>
#include <map>

struct ExchangeConfig {
    std::string api_key;
    std::string secret_key;
    std::string rest_base_url;
    std::string ws_base_url;
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
    uint16_t ws_port = 9002;
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

    static Config load(const std::string& filepath);
};
