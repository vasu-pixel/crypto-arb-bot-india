#include "persistence/trade_logger.h"
#include "common/logger.h"
#include "common/time_utils.h"
#include <fstream>
#include <sstream>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>

// JSON serialization for TradeRecord
void to_json(nlohmann::json& j, const TradeRecord& r) {
    j = {
        {"id", r.id},
        {"pair", r.pair},
        {"buy_exchange", exchange_to_string(r.buy_exchange)},
        {"sell_exchange", exchange_to_string(r.sell_exchange)},
        {"buy_price", r.buy_price},
        {"sell_price", r.sell_price},
        {"quantity", r.quantity},
        {"gross_spread_bps", r.gross_spread_bps},
        {"net_spread_bps", r.net_spread_bps},
        {"buy_result", {
            {"exchange_order_id", r.buy_result.exchange_order_id},
            {"status", order_status_to_string(r.buy_result.status)},
            {"filled_quantity", r.buy_result.filled_quantity},
            {"avg_fill_price", r.buy_result.avg_fill_price},
            {"fee_paid", r.buy_result.fee_paid},
            {"error_message", r.buy_result.error_message}
        }},
        {"sell_result", {
            {"exchange_order_id", r.sell_result.exchange_order_id},
            {"status", order_status_to_string(r.sell_result.status)},
            {"filled_quantity", r.sell_result.filled_quantity},
            {"avg_fill_price", r.sell_result.avg_fill_price},
            {"fee_paid", r.sell_result.fee_paid},
            {"error_message", r.sell_result.error_message}
        }},
        {"realized_pnl", r.realized_pnl},
        {"timestamp", r.timestamp_iso},
        {"mode", trading_mode_to_string(r.mode)}
    };
}

static OrderStatus parse_order_status(const std::string& s) {
    if (s == "FILLED") return OrderStatus::FILLED;
    if (s == "PARTIALLY_FILLED") return OrderStatus::PARTIALLY_FILLED;
    if (s == "OPEN") return OrderStatus::OPEN;
    if (s == "CANCELLED") return OrderStatus::CANCELLED;
    if (s == "REJECTED") return OrderStatus::REJECTED;
    return OrderStatus::PENDING;
}

void from_json(const nlohmann::json& j, TradeRecord& r) {
    r.id = j.value("id", "");
    r.pair = j.value("pair", "");
    r.buy_exchange = exchange_from_string(j.value("buy_exchange", "BINANCE_US"));
    r.sell_exchange = exchange_from_string(j.value("sell_exchange", "KRAKEN"));
    r.buy_price = j.value("buy_price", 0.0);
    r.sell_price = j.value("sell_price", 0.0);
    r.quantity = j.value("quantity", 0.0);
    r.gross_spread_bps = j.value("gross_spread_bps", 0.0);
    r.net_spread_bps = j.value("net_spread_bps", 0.0);
    r.realized_pnl = j.value("realized_pnl", 0.0);
    r.timestamp_iso = j.value("timestamp", "");

    std::string mode_str = j.value("mode", "LIVE");
    if (mode_str == "PAPER") r.mode = TradingMode::PAPER;
    else if (mode_str == "BACKTEST") r.mode = TradingMode::BACKTEST;
    else r.mode = TradingMode::LIVE;

    if (j.contains("buy_result")) {
        auto& br = j["buy_result"];
        r.buy_result.exchange_order_id = br.value("exchange_order_id", "");
        r.buy_result.status = parse_order_status(br.value("status", "PENDING"));
        r.buy_result.filled_quantity = br.value("filled_quantity", 0.0);
        r.buy_result.avg_fill_price = br.value("avg_fill_price", 0.0);
        r.buy_result.fee_paid = br.value("fee_paid", 0.0);
        r.buy_result.error_message = br.value("error_message", "");
    }
    if (j.contains("sell_result")) {
        auto& sr = j["sell_result"];
        r.sell_result.exchange_order_id = sr.value("exchange_order_id", "");
        r.sell_result.status = parse_order_status(sr.value("status", "PENDING"));
        r.sell_result.filled_quantity = sr.value("filled_quantity", 0.0);
        r.sell_result.avg_fill_price = sr.value("avg_fill_price", 0.0);
        r.sell_result.fee_paid = sr.value("fee_paid", 0.0);
        r.sell_result.error_message = sr.value("error_message", "");
    }
}

void to_json(nlohmann::json& j, const BacktestMetrics& m) {
    j = {
        {"total_pnl", m.total_pnl},
        {"total_trades", m.total_trades},
        {"winning_trades", m.winning_trades},
        {"win_rate", m.win_rate},
        {"sharpe_ratio", m.sharpe_ratio},
        {"max_drawdown", m.max_drawdown},
        {"max_drawdown_pct", m.max_drawdown_pct},
        {"profit_factor", m.profit_factor},
        {"avg_trade_pnl", m.avg_trade_pnl},
        {"total_fees_paid", m.total_fees_paid},
        {"pnl_per_pair", m.pnl_per_pair},
        {"equity_curve", m.equity_curve}
    };
}

void to_json(nlohmann::json& j, const BalanceInfo& b) {
    j = {
        {"exchange", exchange_to_string(b.exchange)},
        {"asset", b.asset},
        {"free", b.free},
        {"locked", b.locked}
    };
}

void to_json(nlohmann::json& j, const DriftAlert& d) {
    j = {
        {"asset", d.asset},
        {"excess_exchange", exchange_to_string(d.excess_exchange)},
        {"deficit_exchange", exchange_to_string(d.deficit_exchange)},
        {"imbalance_pct", d.imbalance_pct},
        {"message", d.message}
    };
}

// TradeLogger implementation
TradeLogger::TradeLogger(const std::string& filepath)
    : filepath_(filepath) {}

void TradeLogger::append_to_file(const std::string& json_line) {
    int fd = open(filepath_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        LOG_ERROR("Failed to open trade log: {}", filepath_);
        return;
    }
    flock(fd, LOCK_EX);
    std::string line = json_line + "\n";
    [[maybe_unused]] auto written = write(fd, line.c_str(), line.size());
    flock(fd, LOCK_UN);
    close(fd);
}

void TradeLogger::log_trade(const TradeRecord& record) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    nlohmann::json j;
    to_json(j, record);
    append_to_file(j.dump());
}

std::vector<TradeRecord> TradeLogger::load_all_trades() const {
    std::vector<TradeRecord> trades;
    std::ifstream file(filepath_);
    if (!file.is_open()) return trades;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        try {
            auto j = nlohmann::json::parse(line);
            TradeRecord record;
            from_json(j, record);
            trades.push_back(std::move(record));
        } catch (const std::exception& e) {
            LOG_WARN("Failed to parse trade record: {}", e.what());
        }
    }
    return trades;
}

double TradeLogger::total_realized_pnl() const {
    auto trades = load_all_trades();
    double total = 0.0;
    for (auto& t : trades) total += t.realized_pnl;
    return total;
}

double TradeLogger::pnl_for_pair(const std::string& pair) const {
    auto trades = load_all_trades();
    double total = 0.0;
    for (auto& t : trades) {
        if (t.pair == pair) total += t.realized_pnl;
    }
    return total;
}
