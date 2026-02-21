#include "orderbook/order_book.h"
#include "common/logger.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

OrderBook::OrderBook(Exchange exchange, const std::string& pair)
    : exchange_(exchange)
    , pair_(pair)
    , last_update_time_(std::chrono::steady_clock::now())
{
}

void OrderBook::apply_snapshot(const std::vector<PriceLevel>& bids,
                               const std::vector<PriceLevel>& asks,
                               uint64_t sequence_id)
{
    std::unique_lock lock(mutex_);

    bids_.clear();
    asks_.clear();

    for (const auto& level : bids) {
        if (level.quantity > 0.0) {
            bids_[level.price] = level.quantity;
        }
    }
    for (const auto& level : asks) {
        if (level.quantity > 0.0) {
            asks_[level.price] = level.quantity;
        }
    }

    last_sequence_id_ = sequence_id;
    last_update_time_ = std::chrono::steady_clock::now();

    LOG_DEBUG("[{}:{}] Snapshot applied: {} bids, {} asks, seq={}",
              exchange_to_string(exchange_), pair_,
              bids_.size(), asks_.size(), sequence_id);
}

void OrderBook::apply_delta(const std::vector<PriceLevel>& bid_updates,
                            const std::vector<PriceLevel>& ask_updates,
                            uint64_t sequence_id)
{
    std::unique_lock lock(mutex_);

    // Guard against out-of-order updates
    if (sequence_id != 0 && sequence_id <= last_sequence_id_) {
        LOG_WARN("[{}:{}] Stale delta ignored: seq={} <= last={}",
                 exchange_to_string(exchange_), pair_,
                 sequence_id, last_sequence_id_);
        return;
    }

    for (const auto& level : bid_updates) {
        if (level.quantity <= 0.0) {
            bids_.erase(level.price);
        } else {
            bids_[level.price] = level.quantity;
        }
    }

    for (const auto& level : ask_updates) {
        if (level.quantity <= 0.0) {
            asks_.erase(level.price);
        } else {
            asks_[level.price] = level.quantity;
        }
    }

    last_sequence_id_ = sequence_id;
    last_update_time_ = std::chrono::steady_clock::now();
}

std::optional<double> OrderBook::best_bid() const
{
    std::shared_lock lock(mutex_);
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<double> OrderBook::best_ask() const
{
    std::shared_lock lock(mutex_);
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

double OrderBook::mid_price() const
{
    std::shared_lock lock(mutex_);
    if (bids_.empty() || asks_.empty()) {
        throw std::runtime_error("Cannot compute mid price: empty order book for " +
                                 exchange_to_string(exchange_) + ":" + pair_);
    }
    return (bids_.begin()->first + asks_.begin()->first) / 2.0;
}

OrderBookSnapshot OrderBook::snapshot(int depth) const
{
    std::shared_lock lock(mutex_);

    OrderBookSnapshot snap;
    snap.exchange = exchange_;
    snap.pair = pair_;
    snap.sequence_id = last_sequence_id_;
    snap.local_timestamp = last_update_time_;

    snap.bids.reserve(static_cast<size_t>(depth));
    snap.asks.reserve(static_cast<size_t>(depth));

    int count = 0;
    for (const auto& [price, qty] : bids_) {
        if (count >= depth) break;
        snap.bids.push_back({price, qty});
        ++count;
    }

    count = 0;
    for (const auto& [price, qty] : asks_) {
        if (count >= depth) break;
        snap.asks.push_back({price, qty});
        ++count;
    }

    return snap;
}

bool OrderBook::is_stale(std::chrono::milliseconds threshold) const
{
    std::shared_lock lock(mutex_);
    auto elapsed = std::chrono::steady_clock::now() - last_update_time_;
    return elapsed > threshold;
}

Exchange OrderBook::exchange() const
{
    // exchange_ is immutable after construction, no lock needed
    return exchange_;
}

const std::string& OrderBook::pair() const
{
    // pair_ is immutable after construction, no lock needed
    return pair_;
}

void OrderBook::prune_zero_levels()
{
    // Internal helper -- caller must already hold a unique lock
    for (auto it = bids_.begin(); it != bids_.end(); ) {
        if (it->second <= 0.0)
            it = bids_.erase(it);
        else
            ++it;
    }
    for (auto it = asks_.begin(); it != asks_.end(); ) {
        if (it->second <= 0.0)
            it = asks_.erase(it);
        else
            ++it;
    }
}
