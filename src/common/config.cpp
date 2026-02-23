#include "common/config.h"
#include <fstream>
#include <stdexcept>
#include <cstdlib>
#include <nlohmann/json.hpp>

static std::string resolve_env(const std::string& value) {
    if (value.substr(0, 4) == "ENV:") {
        const char* env_val = std::getenv(value.substr(4).c_str());
        if (!env_val) {
            // Return empty — API keys are optional for paper/backtest modes.
            // Fee fetching and authenticated REST calls will gracefully fail.
            return "";
        }
        return std::string(env_val);
    }
    return value;
}

Config Config::load(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + filepath);
    }

    nlohmann::json j;
    file >> j;

    Config config;

    // Mode
    if (j.contains("mode")) {
        std::string mode_str = j["mode"];
        if (mode_str == "live") config.mode = TradingMode::LIVE;
        else if (mode_str == "paper") config.mode = TradingMode::PAPER;
        else if (mode_str == "backtest") config.mode = TradingMode::BACKTEST;
    }

    // Exchanges
    auto& exch = j["exchanges"];
    if (exch.contains("binance")) {
        auto& b = exch["binance"];
        config.exchanges[Exchange::BINANCE] = {
            resolve_env(b.value("api_key", "")),
            resolve_env(b.value("secret_key", "")),
            b.value("rest_base_url", "https://api.binance.com"),
            b.value("ws_base_url", "wss://stream.binance.com:9443/ws")
        };
    }
    if (exch.contains("okx")) {
        auto& k = exch["okx"];
        config.exchanges[Exchange::OKX] = {
            resolve_env(k.value("api_key", "")),
            resolve_env(k.value("secret_key", "")),
            k.value("rest_base_url", "https://www.okx.com"),
            k.value("ws_base_url", "wss://ws.okx.com:8443/ws/v5/public")
        };
    }
    if (exch.contains("bybit")) {
        auto& c = exch["bybit"];
        config.exchanges[Exchange::BYBIT] = {
            resolve_env(c.value("api_key", "")),
            resolve_env(c.value("secret_key", "")),
            c.value("rest_base_url", "https://api.bybit.com"),
            c.value("ws_base_url", "wss://stream.bybit.com/v5/public/spot")
        };
    }

    // Strategy
    if (j.contains("strategy")) {
        auto& s = j["strategy"];
        config.min_net_spread_bps = s.value("min_net_spread_bps", 3.0);
        config.min_trade_size_usd = s.value("min_trade_size_usd", 50.0);
        config.max_trade_size_usd = s.value("max_trade_size_usd", 5000.0);
        config.max_open_positions = s.value("max_open_positions", 3);
        config.order_timeout_ms = s.value("order_timeout_ms", 5000);
        config.partial_fill_wait_ms = s.value("partial_fill_wait_ms", 2000);
    }

    // Inventory
    if (j.contains("inventory")) {
        auto& inv = j["inventory"];
        config.drift_threshold_pct = inv.value("drift_threshold_pct", 20.0);
        config.balance_refresh_interval_s = inv.value("balance_refresh_interval_s", 30);
    }

    // Server
    if (j.contains("server")) {
        auto& srv = j["server"];
        config.ws_port = srv.value("ws_port", 9002);
        config.heartbeat_interval_s = srv.value("heartbeat_interval_s", 5);
    }

    // Persistence
    if (j.contains("persistence")) {
        auto& p = j["persistence"];
        config.trades_file = p.value("trades_file", "data/trades.json");
        config.paper_trades_file = p.value("paper_trades_file", "data/paper_trades.json");
    }

    // Logging
    if (j.contains("logging")) {
        auto& l = j["logging"];
        config.log_level = l.value("level", "info");
        config.log_file = l.value("file", "logs/arb_bot.log");
    }

    // Data recording
    if (j.contains("data_recording")) {
        auto& dr = j["data_recording"];
        config.record_order_books = dr.value("enabled", false);
        config.historical_data_dir = dr.value("directory", "data/historical");
    }

    // Paper trading initial balances
    if (j.contains("paper_trading") && j["paper_trading"].contains("initial_balances")) {
        for (auto& [key, val] : j["paper_trading"]["initial_balances"].items()) {
            config.paper_initial_balances[key] = val.get<double>();
        }
    }

    return config;
}
