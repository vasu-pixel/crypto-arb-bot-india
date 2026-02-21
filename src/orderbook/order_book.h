#pragma once

#include "common/types.h"

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <shared_mutex>
#include <cstdint>

class OrderBook {
public:
    OrderBook(Exchange exchange, const std::string& pair);

    // Writers (called from WS feed threads)
    void apply_snapshot(const std::vector<PriceLevel>& bids,
                        const std::vector<PriceLevel>& asks,
                        uint64_t sequence_id);

    void apply_delta(const std::vector<PriceLevel>& bid_updates,
                     const std::vector<PriceLevel>& ask_updates,
                     uint64_t sequence_id);

    // Readers (called from strategy thread)
    std::optional<double> best_bid() const;
    std::optional<double> best_ask() const;
    double mid_price() const;
    OrderBookSnapshot snapshot(int depth = 20) const;
    bool is_stale(std::chrono::milliseconds threshold) const;

    Exchange exchange() const;
    const std::string& pair() const;

private:
    Exchange exchange_;
    std::string pair_;
    std::map<double, double, std::greater<>> bids_; // descending by price
    std::map<double, double> asks_;                  // ascending by price
    uint64_t last_sequence_id_ = 0;
    std::chrono::steady_clock::time_point last_update_time_;
    mutable std::shared_mutex mutex_;

    void prune_zero_levels();
};
