#include "server/message_types.h"
#include "common/time_utils.h"

namespace MessageTypes {

// ── helpers ────────────────────────────────────────────────────────────────

nlohmann::json make_envelope(const std::string& type, const nlohmann::json& data) {
    return {
        {"type",      type},
        {"timestamp", TimeUtils::now_iso8601()},
        {"data",      data}
    };
}

// ── trade ──────────────────────────────────────────────────────────────────

nlohmann::json make_trade_message(const TradeRecord& trade) {
    nlohmann::json data;
    data["id"]                = trade.id;
    data["pair"]              = trade.pair;
    data["buy_exchange"]      = exchange_to_string(trade.buy_exchange);
    data["sell_exchange"]     = exchange_to_string(trade.sell_exchange);
    data["buy_price"]         = trade.buy_price;
    data["sell_price"]        = trade.sell_price;
    data["quantity"]          = trade.quantity;
    data["gross_spread_bps"]  = trade.gross_spread_bps;
    data["net_spread_bps"]    = trade.net_spread_bps;
    data["realized_pnl"]     = trade.realized_pnl;
    data["timestamp"]         = trade.timestamp_iso;
    data["mode"]              = trading_mode_to_string(trade.mode);

    // Buy result
    data["buy_result"]["order_id"]        = trade.buy_result.exchange_order_id;
    data["buy_result"]["status"]          = order_status_to_string(trade.buy_result.status);
    data["buy_result"]["filled_quantity"] = trade.buy_result.filled_quantity;
    data["buy_result"]["avg_fill_price"]  = trade.buy_result.avg_fill_price;
    data["buy_result"]["fee_paid"]        = trade.buy_result.fee_paid;

    // Sell result
    data["sell_result"]["order_id"]        = trade.sell_result.exchange_order_id;
    data["sell_result"]["status"]          = order_status_to_string(trade.sell_result.status);
    data["sell_result"]["filled_quantity"] = trade.sell_result.filled_quantity;
    data["sell_result"]["avg_fill_price"]  = trade.sell_result.avg_fill_price;
    data["sell_result"]["fee_paid"]        = trade.sell_result.fee_paid;

    return make_envelope("trade", data);
}

// ── spreads ────────────────────────────────────────────────────────────────

nlohmann::json make_spreads_message(
    const std::map<std::string, std::map<std::string, std::pair<double, double>>>& spreads)
{
    nlohmann::json data = nlohmann::json::object();

    for (const auto& [buy_exch, sell_map] : spreads) {
        nlohmann::json inner = nlohmann::json::object();
        for (const auto& [sell_exch, bps_pair] : sell_map) {
            inner[sell_exch] = {
                {"gross_bps", bps_pair.first},
                {"net_bps",   bps_pair.second}
            };
        }
        data[buy_exch] = inner;
    }

    return make_envelope("spreads", data);
}

// ── balances ───────────────────────────────────────────────────────────────

nlohmann::json make_balances_message(
    const std::map<Exchange, std::unordered_map<std::string, double>>& balances)
{
    nlohmann::json data = nlohmann::json::object();

    for (const auto& [exch, asset_map] : balances) {
        nlohmann::json inner = nlohmann::json::object();
        for (const auto& [asset, amount] : asset_map) {
            inner[asset] = amount;
        }
        data[exchange_to_string(exch)] = inner;
    }

    return make_envelope("balances", data);
}

// ── pnl ────────────────────────────────────────────────────────────────────

nlohmann::json make_pnl_message(double total_pnl,
                                 const std::map<std::string, double>& pnl_per_pair)
{
    nlohmann::json data;
    data["total_pnl"] = total_pnl;

    nlohmann::json per_pair = nlohmann::json::object();
    for (const auto& [pair, pnl] : pnl_per_pair) {
        per_pair[pair] = pnl;
    }
    data["pnl_per_pair"] = per_pair;

    return make_envelope("pnl", data);
}

// ── heartbeat ──────────────────────────────────────────────────────────────

nlohmann::json make_heartbeat_message(uint64_t seq, size_t dropped_count) {
    nlohmann::json data;
    data["seq"]           = seq;
    data["dropped_count"] = dropped_count;
    return make_envelope("heartbeat", data);
}

// ── alert ──────────────────────────────────────────────────────────────────

nlohmann::json make_alert_message(const DriftAlert& alert) {
    nlohmann::json data;
    data["asset"]            = alert.asset;
    data["excess_exchange"]  = exchange_to_string(alert.excess_exchange);
    data["deficit_exchange"] = exchange_to_string(alert.deficit_exchange);
    data["imbalance_pct"]    = alert.imbalance_pct;
    data["message"]          = alert.message;
    return make_envelope("alert", data);
}

}  // namespace MessageTypes
