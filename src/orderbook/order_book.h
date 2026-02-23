#pragma once

#include "common/types.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

class OrderBook {
public:
  OrderBook(Exchange exchange, const std::string &pair);

  // Writers (called from WS feed threads)
  void apply_snapshot(const std::vector<PriceLevel> &bids,
                      const std::vector<PriceLevel> &asks,
                      uint64_t sequence_id);

  void apply_delta(const std::vector<PriceLevel> &bid_updates,
                   const std::vector<PriceLevel> &ask_updates,
                   uint64_t sequence_id);

  // Readers (called from strategy thread)
  std::optional<double> best_bid() const;
  std::optional<double> best_ask() const;
  double mid_price() const;
  OrderBookSnapshot snapshot(int depth = 20) const;
  bool is_stale(std::chrono::milliseconds threshold) const;

  Exchange exchange() const;
  const std::string &pair() const;

private:
  Exchange exchange_;
  std::string pair_;

  std::vector<PriceLevel> bids_; // sorted descending by price
  std::vector<PriceLevel> asks_; // sorted ascending by price

  static constexpr int TOP_LEVELS = 20;
  PriceLevel top_bids_[TOP_LEVELS];
  PriceLevel top_asks_[TOP_LEVELS];
  int num_top_bids_ = 0;
  int num_top_asks_ = 0;

  alignas(64) mutable std::atomic<uint64_t> seqlock_{0};
  uint64_t last_sequence_id_ = 0;
  std::chrono::steady_clock::time_point last_update_time_;
  mutable std::shared_mutex mutex_;

  void sync_top_levels();
};
