#include "backtest/simulated_exchange.h"
#include "common/crypto_utils.h"
#include "common/logger.h"
#include <algorithm>

SimulatedExchange::SimulatedExchange(Exchange exch_id, const std::string& name,
                                       const std::map<std::string, double>& initial_balances,
                                       double maker_fee, double taker_fee)
    : exch_id_(exch_id), name_(name), balances_(initial_balances)
    , maker_fee_(maker_fee), taker_fee_(taker_fee) {}

void SimulatedExchange::load_snapshots(const std::vector<HistoricalSnapshot>& snapshots) {
    for (auto& snap : snapshots) {
        if (snap.exchange == exch_id_) {
            snapshots_by_pair_[snap.pair].push_back(snap);
        }
    }
    // Sort each pair's snapshots by timestamp
    for (auto& [pair, snaps] : snapshots_by_pair_) {
        std::sort(snaps.begin(), snaps.end(),
                  [](const auto& a, const auto& b) { return a.timestamp_ms < b.timestamp_ms; });
        current_index_[pair] = 0;
    }
}

void SimulatedExchange::advance_to(uint64_t timestamp_ms) {
    current_time_ms_ = timestamp_ms;
    for (auto& [pair, snaps] : snapshots_by_pair_) {
        auto& idx = current_index_[pair];
        while (idx + 1 < snaps.size() && snaps[idx + 1].timestamp_ms <= timestamp_ms) {
            idx++;
        }
    }
}

OrderBookSnapshot SimulatedExchange::current_book(const std::string& pair) const {
    OrderBookSnapshot snap;
    snap.exchange = exch_id_;
    snap.pair = pair;
    snap.local_timestamp = std::chrono::steady_clock::now();

    auto it = snapshots_by_pair_.find(pair);
    if (it == snapshots_by_pair_.end()) return snap;

    auto idx_it = current_index_.find(pair);
    if (idx_it == current_index_.end()) return snap;

    auto& hist = it->second[idx_it->second];
    snap.bids = hist.bids;
    snap.asks = hist.asks;
    snap.sequence_id = hist.timestamp_ms;
    return snap;
}

OrderBookSnapshot SimulatedExchange::fetch_order_book(const std::string& pair, int depth) {
    auto snap = current_book(pair);
    if (static_cast<int>(snap.bids.size()) > depth) snap.bids.resize(depth);
    if (static_cast<int>(snap.asks.size()) > depth) snap.asks.resize(depth);
    return snap;
}

std::vector<std::pair<std::string, double>> SimulatedExchange::fetch_top_pairs_by_volume(int limit) {
    std::vector<std::pair<std::string, double>> result;
    for (auto& [pair, _] : snapshots_by_pair_) {
        result.push_back({pair, 1000000.0}); // Dummy volume for backtest
    }
    if (static_cast<int>(result.size()) > limit) result.resize(limit);
    return result;
}

FeeInfo SimulatedExchange::fetch_fees(const std::string& pair) {
    return {maker_fee_, taker_fee_, exch_id_, pair};
}

OrderResult SimulatedExchange::place_limit_order(const OrderRequest& req) {
    OrderResult result;
    result.exchange_order_id = "SIM-" + CryptoUtils::generate_uuid();

    auto book = current_book(req.pair);

    // Extract asset names
    auto dash_pos = req.pair.find('-');
    std::string base = (dash_pos != std::string::npos) ? req.pair.substr(0, dash_pos) : req.pair;
    std::string quote = (dash_pos != std::string::npos) ? req.pair.substr(dash_pos + 1) : "USD";

    if (req.side == Side::BUY) {
        // Check if buy price crosses the spread (>= best ask)
        if (book.asks.empty()) {
            result.status = OrderStatus::REJECTED;
            result.error_message = "No asks in book";
            return result;
        }

        double cost_needed = req.price * req.quantity;
        double available = balances_.count(quote) ? balances_[quote] : 0.0;
        if (available < cost_needed) {
            result.status = OrderStatus::REJECTED;
            result.error_message = "Insufficient " + quote;
            return result;
        }

        // Walk asks to fill
        double filled_qty = 0;
        double total_cost = 0;
        for (auto& ask : book.asks) {
            if (ask.price > req.price) break; // Our limit price won't match
            double remaining = req.quantity - filled_qty;
            double take = std::min(ask.quantity, remaining);
            total_cost += take * ask.price;
            filled_qty += take;
            if (filled_qty >= req.quantity) break;
        }

        if (filled_qty > 0) {
            result.filled_quantity = filled_qty;
            result.avg_fill_price = total_cost / filled_qty;
            result.fee_paid = total_cost * taker_fee_;
            result.status = (filled_qty >= req.quantity) ?
                            OrderStatus::FILLED : OrderStatus::PARTIALLY_FILLED;

            // Update balances
            balances_[base] += filled_qty;
            balances_[quote] -= (total_cost + result.fee_paid);
        } else {
            result.status = OrderStatus::OPEN; // Resting order (won't fill in backtest)
        }
    } else {
        // SELL
        if (book.bids.empty()) {
            result.status = OrderStatus::REJECTED;
            result.error_message = "No bids in book";
            return result;
        }

        double available = balances_.count(base) ? balances_[base] : 0.0;
        if (available < req.quantity) {
            result.status = OrderStatus::REJECTED;
            result.error_message = "Insufficient " + base;
            return result;
        }

        // Walk bids to fill
        double filled_qty = 0;
        double total_proceeds = 0;
        for (auto& bid : book.bids) {
            if (bid.price < req.price) break;
            double remaining = req.quantity - filled_qty;
            double take = std::min(bid.quantity, remaining);
            total_proceeds += take * bid.price;
            filled_qty += take;
            if (filled_qty >= req.quantity) break;
        }

        if (filled_qty > 0) {
            result.filled_quantity = filled_qty;
            result.avg_fill_price = total_proceeds / filled_qty;
            result.fee_paid = total_proceeds * taker_fee_;
            result.status = (filled_qty >= req.quantity) ?
                            OrderStatus::FILLED : OrderStatus::PARTIALLY_FILLED;

            balances_[base] -= filled_qty;
            balances_[quote] += (total_proceeds - result.fee_paid);
        } else {
            result.status = OrderStatus::OPEN;
        }
    }

    return result;
}

OrderResult SimulatedExchange::cancel_order(const std::string& pair, const std::string& order_id) {
    return {order_id, OrderStatus::CANCELLED, 0, 0, 0, ""};
}

OrderResult SimulatedExchange::query_order(const std::string& pair, const std::string& order_id) {
    return {order_id, OrderStatus::FILLED, 0, 0, 0, ""};
}

std::vector<BalanceInfo> SimulatedExchange::fetch_balances() {
    std::vector<BalanceInfo> result;
    for (auto& [asset, amount] : balances_) {
        result.push_back({exch_id_, asset, amount, 0.0});
    }
    return result;
}
