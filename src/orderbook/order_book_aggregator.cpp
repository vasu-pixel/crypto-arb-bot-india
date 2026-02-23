#include "orderbook/order_book_aggregator.h"
#include "common/logger.h"

#include <algorithm>
#include <set>

OrderBook &OrderBookAggregator::get_or_create_book(Exchange exch,
                                                   const std::string &pair) {
  size_t exch_idx = static_cast<size_t>(exch);

  // Fast path: read-only check
  {
    std::shared_lock lock(mutex_);
    auto it = books_.find(pair);
    if (it != books_.end() && it->second[exch_idx] != nullptr) {
      return *(it->second[exch_idx]);
    }
  }

  // Slow path: create under exclusive lock
  std::unique_lock lock(mutex_);
  // Double-check after acquiring exclusive lock
  auto &arr = books_[pair];
  if (!arr[exch_idx]) {
    arr[exch_idx] = std::make_unique<OrderBook>(exch, pair);
    LOG_INFO("Created order book for {}:{}", exchange_to_string(exch), pair);
  }

  return *(arr[exch_idx]);
}

OrderBook *OrderBookAggregator::get_book(Exchange exch,
                                         const std::string &pair) {
  size_t exch_idx = static_cast<size_t>(exch);
  std::shared_lock lock(mutex_);
  auto it = books_.find(pair);
  if (it != books_.end() && it->second[exch_idx] != nullptr) {
    return it->second[exch_idx].get();
  }
  return nullptr;
}

std::vector<OrderBookSnapshot>
OrderBookAggregator::get_pair_snapshots(const std::string &pair, int depth) {
  std::vector<OrderBookSnapshot> snapshots;

  // Collect pointers under read lock, then snapshot outside
  std::vector<OrderBook *> matching_books;
  {
    std::shared_lock lock(mutex_);
    auto it = books_.find(pair);
    if (it != books_.end()) {
      for (const auto &book_ptr : it->second) {
        if (book_ptr) {
          matching_books.push_back(book_ptr.get());
        }
      }
    }
  }

  snapshots.reserve(matching_books.size());
  for (auto *book : matching_books) {
    snapshots.push_back(book->snapshot(depth));
  }

  return snapshots;
}

std::vector<std::string> OrderBookAggregator::get_pairs() const {
  std::shared_lock lock(mutex_);
  std::vector<std::string> unique_pairs;
  unique_pairs.reserve(books_.size());
  for (const auto &[pair, arr] : books_) {
    unique_pairs.push_back(pair);
  }
  return unique_pairs;
}
