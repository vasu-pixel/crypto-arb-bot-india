#pragma once

#include "common/types.h"

#include <map>
#include <string>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>

/// Helpers for creating type-tagged JSON envelopes destined for the Streamlit
/// dashboard WebSocket channel.
namespace MessageTypes {

/// Wrap arbitrary data inside a standard envelope:
///   { "type": <type>, "timestamp": <ISO-8601 now>, "data": <data> }
nlohmann::json make_envelope(const std::string& type, const nlohmann::json& data);

/// Envelope for a single completed trade.
nlohmann::json make_trade_message(const TradeRecord& trade);

/// Envelope for the current spread matrix for a single pair.
/// Key layout: data[pair]["BUY->SELL"] = {gross_bps, net_bps}
nlohmann::json make_spreads_message(
    const std::string& pair,
    const std::map<std::string, std::map<std::string, std::pair<double, double>>>& spreads);

/// Envelope for overall and per-pair PnL with trade stats.
nlohmann::json make_pnl_message(double total_pnl,
                                 const std::map<std::string, double>& pnl_per_pair,
                                 int total_trades, double win_rate,
                                 double total_fees);

/// Envelope for the latest balances across all exchanges.
nlohmann::json make_balances_message(
    const std::map<Exchange, std::unordered_map<std::string, double>>& balances);

/// Heartbeat envelope (carries a monotonic sequence number and the number of
/// messages dropped by the broadcast queue since start).
nlohmann::json make_heartbeat_message(uint64_t seq, size_t dropped_count);

/// Envelope for a balance-drift alert.
nlohmann::json make_alert_message(const DriftAlert& alert);

}  // namespace MessageTypes
