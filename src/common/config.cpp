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
    if (exch.contains("mexc")) {
        auto& m = exch["mexc"];
        config.exchanges[Exchange::MEXC] = {
            resolve_env(m.value("api_key", "")),
            resolve_env(m.value("secret_key", "")),
            m.value("rest_base_url", "https://api.mexc.com"),
            m.value("ws_base_url", "wss://wbs.mexc.com/ws")
        };
    }
    if (exch.contains("gateio")) {
        auto& g = exch["gateio"];
        config.exchanges[Exchange::GATEIO] = {
            resolve_env(g.value("api_key", "")),
            resolve_env(g.value("secret_key", "")),
            g.value("rest_base_url", "https://api.gateio.ws"),
            g.value("ws_base_url", "wss://api.gateio.ws/ws/v4/")
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
        config.ws_port = srv.value("ws_port", 9003);
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

    // Paper trading excluded exchanges
    if (j.contains("paper_trading") && j["paper_trading"].contains("excluded_exchanges")) {
        for (auto& name : j["paper_trading"]["excluded_exchanges"]) {
            std::string s = name.get<std::string>();
            // Uppercase for exchange_from_string
            for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            try {
                config.paper_excluded_exchanges.insert(exchange_from_string(s));
            } catch (...) {
                // Ignore unknown exchange names
            }
        }
    }

    // Paper realism configuration (omitting section = all defaults = realism enabled)
    if (j.contains("paper_realism")) {
        auto& pr = j["paper_realism"];
        auto& r = config.paper_realism;

        r.enable_latency = pr.value("enable_latency", r.enable_latency);
        r.latency_mean_ms = pr.value("latency_mean_ms", r.latency_mean_ms);
        r.latency_stddev_ms = pr.value("latency_stddev_ms", r.latency_stddev_ms);

        r.enable_adverse_slippage = pr.value("enable_adverse_slippage", r.enable_adverse_slippage);
        r.slippage_bps_mean = pr.value("slippage_bps_mean", r.slippage_bps_mean);
        r.slippage_bps_stddev = pr.value("slippage_bps_stddev", r.slippage_bps_stddev);

        r.enable_staleness_penalty = pr.value("enable_staleness_penalty", r.enable_staleness_penalty);
        r.staleness_penalty_bps_per_sec = pr.value("staleness_penalty_bps_per_sec", r.staleness_penalty_bps_per_sec);
        r.max_book_age_ms = pr.value("max_book_age_ms", r.max_book_age_ms);

        r.enable_realistic_rebalance = pr.value("enable_realistic_rebalance", r.enable_realistic_rebalance);
        r.rebalance_delay_minutes = pr.value("rebalance_delay_minutes", r.rebalance_delay_minutes);

        r.enable_withdrawal_fees = pr.value("enable_withdrawal_fees", r.enable_withdrawal_fees);
        r.withdrawal_fee_pct = pr.value("withdrawal_fee_pct", r.withdrawal_fee_pct);
        if (pr.contains("withdrawal_flat_fees")) {
            for (auto& [asset, fee] : pr["withdrawal_flat_fees"].items()) {
                r.withdrawal_flat_fees[asset] = fee.get<double>();
            }
        }

        r.enable_market_impact = pr.value("enable_market_impact", r.enable_market_impact);
        r.impact_decay_seconds = pr.value("impact_decay_seconds", r.impact_decay_seconds);

        r.enable_competition = pr.value("enable_competition", r.enable_competition);
        r.competition_base_prob = pr.value("competition_base_prob", r.competition_base_prob);
        r.competition_decay_bps = pr.value("competition_decay_bps", r.competition_decay_bps);

        r.enable_rate_limits = pr.value("enable_rate_limits", r.enable_rate_limits);
        r.max_orders_per_second = pr.value("max_orders_per_second", r.max_orders_per_second);
        r.max_orders_per_minute = pr.value("max_orders_per_minute", r.max_orders_per_minute);

        r.enable_min_order_size = pr.value("enable_min_order_size", r.enable_min_order_size);
        if (pr.contains("min_order_sizes")) {
            for (auto& [pair, sz] : pr["min_order_sizes"].items()) {
                r.min_order_sizes[pair] = sz.get<double>();
            }
        }
        r.default_min_notional_usd = pr.value("default_min_notional_usd", r.default_min_notional_usd);

        r.enable_one_leg_risk = pr.value("enable_one_leg_risk", r.enable_one_leg_risk);
        r.one_leg_probability = pr.value("one_leg_probability", r.one_leg_probability);
        r.one_leg_unwind_slippage_bps = pr.value("one_leg_unwind_slippage_bps", r.one_leg_unwind_slippage_bps);
    }

    return config;
}
