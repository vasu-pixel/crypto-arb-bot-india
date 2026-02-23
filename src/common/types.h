#pragma once
#include <chrono>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

enum class Exchange { BINANCE, OKX, BYBIT, COUNT };

// Hash specialization so Exchange can be used as key in unordered_map
namespace std {
template <> struct hash<Exchange> {
  size_t operator()(Exchange e) const noexcept {
    return hash<int>{}(static_cast<int>(e));
  }
};
} // namespace std
enum class Side { BUY, SELL };
enum class OrderStatus {
  PENDING,
  OPEN,
  PARTIALLY_FILLED,
  FILLED,
  CANCELLED,
  REJECTED
};
enum class TradingMode { LIVE, PAPER, BACKTEST };

inline std::string_view exchange_to_string(Exchange e) {
  switch (e) {
  case Exchange::BINANCE:
    return "BINANCE";
  case Exchange::OKX:
    return "OKX";
  case Exchange::BYBIT:
    return "BYBIT";
  case Exchange::COUNT:
    return "COUNT";
  }
  return "UNKNOWN";
}

inline Exchange exchange_from_string(const std::string &s) {
  if (s == "BINANCE")
    return Exchange::BINANCE;
  if (s == "OKX")
    return Exchange::OKX;
  if (s == "BYBIT")
    return Exchange::BYBIT;
  throw std::runtime_error("Unknown exchange: " + s);
}

inline std::string side_to_string(Side s) {
  return s == Side::BUY ? "BUY" : "SELL";
}

inline std::string order_status_to_string(OrderStatus s) {
  switch (s) {
  case OrderStatus::PENDING:
    return "PENDING";
  case OrderStatus::OPEN:
    return "OPEN";
  case OrderStatus::PARTIALLY_FILLED:
    return "PARTIALLY_FILLED";
  case OrderStatus::FILLED:
    return "FILLED";
  case OrderStatus::CANCELLED:
    return "CANCELLED";
  case OrderStatus::REJECTED:
    return "REJECTED";
  }
  return "UNKNOWN";
}

inline std::string trading_mode_to_string(TradingMode m) {
  switch (m) {
  case TradingMode::LIVE:
    return "LIVE";
  case TradingMode::PAPER:
    return "PAPER";
  case TradingMode::BACKTEST:
    return "BACKTEST";
  }
  return "UNKNOWN";
}

struct PriceLevel {
  double price = 0.0;
  double quantity = 0.0;
};

struct OrderBookSnapshot {
  Exchange exchange;
  std::string pair;
  std::vector<PriceLevel> bids;
  std::vector<PriceLevel> asks;
  uint64_t sequence_id = 0;
  std::chrono::steady_clock::time_point local_timestamp;
  bool is_delta = false; // true = incremental update, false = full snapshot
};

struct FeeInfo {
  double maker_fee = 0.0;
  double taker_fee = 0.0;
  Exchange exchange;
  std::string pair;
};

struct OrderRequest {
  Exchange exchange;
  std::string pair;
  Side side;
  double price = 0.0;
  double quantity = 0.0;
  std::string client_order_id;
};

struct OrderResult {
  std::string exchange_order_id;
  OrderStatus status = OrderStatus::PENDING;
  double filled_quantity = 0.0;
  double avg_fill_price = 0.0;
  double fee_paid = 0.0;
  std::string error_message;
};

struct ArbitrageOpportunity {
  std::string pair;
  Exchange buy_exchange;
  Exchange sell_exchange;
  double buy_price = 0.0;
  double sell_price = 0.0;
  double quantity = 0.0;
  double gross_spread_bps = 0.0;
  double net_spread_bps = 0.0;
  std::chrono::steady_clock::time_point detected_at;
};

struct TradeRecord {
  std::string id;
  std::string pair;
  Exchange buy_exchange;
  Exchange sell_exchange;
  double buy_price = 0.0;
  double sell_price = 0.0;
  double quantity = 0.0;
  double gross_spread_bps = 0.0;
  double net_spread_bps = 0.0;
  OrderResult buy_result;
  OrderResult sell_result;
  double realized_pnl = 0.0;
  std::string timestamp_iso;
  TradingMode mode = TradingMode::LIVE;
};

struct BalanceInfo {
  Exchange exchange;
  std::string asset;
  double free = 0.0;
  double locked = 0.0;
};

struct DriftAlert {
  std::string asset;
  Exchange excess_exchange;
  Exchange deficit_exchange;
  double imbalance_pct = 0.0;
  std::string message;
};

struct HistoricalSnapshot {
  std::string timestamp_iso;
  uint64_t timestamp_ms = 0;
  Exchange exchange;
  std::string pair;
  std::vector<PriceLevel> bids;
  std::vector<PriceLevel> asks;
};

struct BacktestConfig {
  std::string from_date;
  std::string to_date;
  std::vector<std::string> pairs;
  double min_net_spread_bps = 3.0;
  double min_trade_size_usd = 50.0;
  double max_trade_size_usd = 5000.0;
  std::map<std::string, double> initial_balances;
};

struct BacktestMetrics {
  double total_pnl = 0.0;
  int total_trades = 0;
  int winning_trades = 0;
  double win_rate = 0.0;
  double sharpe_ratio = 0.0;
  double max_drawdown = 0.0;
  double max_drawdown_pct = 0.0;
  double profit_factor = 0.0;
  double avg_trade_pnl = 0.0;
  double total_fees_paid = 0.0;
  std::map<std::string, double> pnl_per_pair;
  std::vector<std::pair<std::string, double>> equity_curve;
};

// JSON serialization helpers
void to_json(nlohmann::json &j, const TradeRecord &r);
void from_json(const nlohmann::json &j, TradeRecord &r);
void to_json(nlohmann::json &j, const BacktestMetrics &m);
void to_json(nlohmann::json &j, const BalanceInfo &b);
void to_json(nlohmann::json &j, const DriftAlert &d);
