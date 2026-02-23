#pragma once

#include "common/types.h"
#include "orderbook/order_book.h"

#include <array>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

class OrderBookAggregator {
public:
  OrderBookAggregator() = default;

  using UpdateCallback = std::function<void(const std::string &)>;

  void set_update_callback(UpdateCallback cb) {
    update_callback_ = std::move(cb);
  }

  void notify_book_update(const std::string &pair) {
    if (update_callback_) {
      update_callback_(pair);
    }
  }

  // Get or create an order book for a given exchange+pair.
  // Thread-safe; creates the book on first access.
  OrderBook &get_or_create_book(Exchange exch, const std::string &pair);

  // Returns nullptr if the book does not exist.
  OrderBook *get_book(Exchange exch, const std::string &pair);

  // Get snapshots for a pair across all exchanges that have a book for it.
  std::vector<OrderBookSnapshot> get_pair_snapshots(const std::string &pair,
                                                    int depth = 20);

  // Get all monitored (unique canonical) pairs.
  std::vector<std::string> get_pairs() const;

private:
  // Key: "PAIR"
  std::unordered_map<std::string, std::array<std::unique_ptr<OrderBook>, 3>>
      books_;
  mutable std::shared_mutex mutex_;
  UpdateCallback update_callback_;
};
