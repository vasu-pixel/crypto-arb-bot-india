#include "orderbook/order_book.h"
#include "common/logger.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

OrderBook::OrderBook(Exchange exchange, const std::string &pair)
    : exchange_(exchange), pair_(pair),
      last_update_time_(std::chrono::steady_clock::now()) {}

void OrderBook::sync_top_levels() {
  num_top_bids_ = std::min(TOP_LEVELS, static_cast<int>(bids_.size()));
  for (int i = 0; i < num_top_bids_; ++i) {
    top_bids_[i] = bids_[i];
  }
  num_top_asks_ = std::min(TOP_LEVELS, static_cast<int>(asks_.size()));
  for (int i = 0; i < num_top_asks_; ++i) {
    top_asks_[i] = asks_[i];
  }
}

void OrderBook::apply_snapshot(const std::vector<PriceLevel> &bids,
                               const std::vector<PriceLevel> &asks,
                               uint64_t sequence_id) {
  std::unique_lock lock(mutex_);

  bids_.clear();
  asks_.clear();

  for (const auto &level : bids) {
    if (level.quantity > 0.0)
      bids_.push_back(level);
  }
  for (const auto &level : asks) {
    if (level.quantity > 0.0)
      asks_.push_back(level);
  }

  std::sort(bids_.begin(), bids_.end(),
            [](auto &a, auto &b) { return a.price > b.price; });
  std::sort(asks_.begin(), asks_.end(),
            [](auto &a, auto &b) { return a.price < b.price; });

  last_sequence_id_ = sequence_id;

  seqlock_.fetch_add(1, std::memory_order_release);
  last_update_time_ = std::chrono::steady_clock::now();
  sync_top_levels();
  seqlock_.fetch_add(1, std::memory_order_release);

  LOG_DEBUG("[{}:{}] Snapshot applied: {} bids, {} asks, seq={}",
            exchange_to_string(exchange_), pair_, bids_.size(), asks_.size(),
            sequence_id);
}

void OrderBook::apply_delta(const std::vector<PriceLevel> &bid_updates,
                            const std::vector<PriceLevel> &ask_updates,
                            uint64_t sequence_id) {
  std::unique_lock lock(mutex_);

  if (sequence_id != 0 && sequence_id <= last_sequence_id_) {
    LOG_WARN("[{}:{}] Stale delta ignored: seq={} <= last={}",
             exchange_to_string(exchange_), pair_, sequence_id,
             last_sequence_id_);
    return;
  }

  auto update_vec = [](std::vector<PriceLevel> &vec, const PriceLevel &level,
                       bool descending) {
    auto cmp = descending
                   ? [](const PriceLevel &a, double p) { return a.price > p; }
                   : [](const PriceLevel &a, double p) { return a.price < p; };

    auto it = std::lower_bound(vec.begin(), vec.end(), level.price, cmp);

    if (it != vec.end() && std::abs(it->price - level.price) < 1e-9) {
      if (level.quantity <= 0.0) {
        vec.erase(it);
      } else {
        it->quantity = level.quantity;
      }
    } else {
      if (level.quantity > 0.0) {
        vec.insert(it, level);
      }
    }
  };

  for (const auto &level : bid_updates) {
    update_vec(bids_, level, true);
  }

  for (const auto &level : ask_updates) {
    update_vec(asks_, level, false);
  }

  last_sequence_id_ = sequence_id;

  seqlock_.fetch_add(1, std::memory_order_release);
  last_update_time_ = std::chrono::steady_clock::now();
  sync_top_levels();
  seqlock_.fetch_add(1, std::memory_order_release);
}

std::optional<double> OrderBook::best_bid() const {
  uint64_t seq;
  std::optional<double> result = std::nullopt;
  do {
    seq = seqlock_.load(std::memory_order_acquire);
    if (seq & 1)
      continue;

    if (num_top_bids_ > 0) {
      result = top_bids_[0].price;
    } else {
      result = std::nullopt;
    }

  } while (seqlock_.load(std::memory_order_acquire) != seq);

  return result;
}

std::optional<double> OrderBook::best_ask() const {
  uint64_t seq;
  std::optional<double> result = std::nullopt;
  do {
    seq = seqlock_.load(std::memory_order_acquire);
    if (seq & 1)
      continue;

    if (num_top_asks_ > 0) {
      result = top_asks_[0].price;
    } else {
      result = std::nullopt;
    }

  } while (seqlock_.load(std::memory_order_acquire) != seq);

  return result;
}

double OrderBook::mid_price() const {
  uint64_t seq;
  double bb = 0.0, ba = 0.0;
  bool valid = false;
  do {
    seq = seqlock_.load(std::memory_order_acquire);
    if (seq & 1)
      continue;

    if (num_top_bids_ > 0 && num_top_asks_ > 0) {
      bb = top_bids_[0].price;
      ba = top_asks_[0].price;
      valid = true;
    } else {
      valid = false;
    }

  } while (seqlock_.load(std::memory_order_acquire) != seq);

  if (!valid) {
    throw std::runtime_error("Cannot compute mid price: empty order book for " +
                             std::string(exchange_to_string(exchange_)) + ":" +
                             pair_);
  }
  return (bb + ba) / 2.0;
}

OrderBookSnapshot OrderBook::snapshot(int depth) const {
  OrderBookSnapshot snap;
  snap.exchange = exchange_;
  snap.pair = pair_;

  int d = std::min(depth, TOP_LEVELS);
  snap.bids.resize(d);
  snap.asks.resize(d);

  uint64_t seq;
  do {
    seq = seqlock_.load(std::memory_order_acquire);
    if (seq & 1)
      continue;

    snap.sequence_id = last_sequence_id_;
    snap.local_timestamp = last_update_time_;

    int nb = std::min(d, num_top_bids_);
    for (int i = 0; i < nb; ++i)
      snap.bids[i] = top_bids_[i];
    snap.bids.resize(nb);

    int na = std::min(d, num_top_asks_);
    for (int i = 0; i < na; ++i)
      snap.asks[i] = top_asks_[i];
    snap.asks.resize(na);

  } while (seqlock_.load(std::memory_order_acquire) != seq);

  return snap;
}

bool OrderBook::is_stale(std::chrono::milliseconds threshold) const {
  uint64_t seq;
  std::chrono::steady_clock::time_point lu;
  do {
    seq = seqlock_.load(std::memory_order_acquire);
    if (seq & 1)
      continue;
    lu = last_update_time_;
  } while (seqlock_.load(std::memory_order_acquire) != seq);

  auto elapsed = std::chrono::steady_clock::now() - lu;
  return elapsed > threshold;
}

Exchange OrderBook::exchange() const { return exchange_; }

const std::string &OrderBook::pair() const { return pair_; }
