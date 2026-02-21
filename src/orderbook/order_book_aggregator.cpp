#include "orderbook/order_book_aggregator.h"
#include "common/logger.h"

#include <algorithm>
#include <set>

std::string OrderBookAggregator::make_key(Exchange exch, const std::string& pair) const
{
    return exchange_to_string(exch) + ":" + pair;
}

OrderBook& OrderBookAggregator::get_or_create_book(Exchange exch, const std::string& pair)
{
    std::string key = make_key(exch, pair);

    // Fast path: read-only check
    {
        std::shared_lock lock(mutex_);
        auto it = books_.find(key);
        if (it != books_.end()) {
            return *(it->second);
        }
    }

    // Slow path: create under exclusive lock
    std::unique_lock lock(mutex_);
    // Double-check after acquiring exclusive lock
    auto it = books_.find(key);
    if (it != books_.end()) {
        return *(it->second);
    }

    auto book = std::make_unique<OrderBook>(exch, pair);
    OrderBook& ref = *book;
    books_.emplace(std::move(key), std::move(book));

    LOG_INFO("Created order book for {}:{}", exchange_to_string(exch), pair);
    return ref;
}

OrderBook* OrderBookAggregator::get_book(Exchange exch, const std::string& pair)
{
    std::shared_lock lock(mutex_);
    auto it = books_.find(make_key(exch, pair));
    if (it != books_.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::vector<OrderBookSnapshot> OrderBookAggregator::get_pair_snapshots(const std::string& pair, int depth)
{
    std::vector<OrderBookSnapshot> snapshots;

    // Collect pointers under read lock, then snapshot outside
    std::vector<OrderBook*> matching_books;
    {
        std::shared_lock lock(mutex_);
        for (const auto& [key, book] : books_) {
            if (book->pair() == pair) {
                matching_books.push_back(book.get());
            }
        }
    }

    snapshots.reserve(matching_books.size());
    for (auto* book : matching_books) {
        snapshots.push_back(book->snapshot(depth));
    }

    return snapshots;
}

std::vector<std::string> OrderBookAggregator::get_pairs() const
{
    std::shared_lock lock(mutex_);
    std::set<std::string> unique_pairs;
    for (const auto& [key, book] : books_) {
        unique_pairs.insert(book->pair());
    }
    return {unique_pairs.begin(), unique_pairs.end()};
}
