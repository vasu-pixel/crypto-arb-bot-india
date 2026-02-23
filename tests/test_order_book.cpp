#include <gtest/gtest.h>
#include "orderbook/order_book.h"

class OrderBookTest : public ::testing::Test {
protected:
    OrderBook book{Exchange::BINANCE, "BTC-USDT"};
};

TEST_F(OrderBookTest, EmptyBookHasNoLevels) {
    auto snap = book.snapshot();
    EXPECT_TRUE(snap.bids.empty());
    EXPECT_TRUE(snap.asks.empty());
}

TEST_F(OrderBookTest, ApplySnapshotSetsLevels) {
    std::vector<PriceLevel> bids = {{100000.0, 1.5}, {99999.0, 2.0}};
    std::vector<PriceLevel> asks = {{100001.0, 1.0}, {100002.0, 3.0}};

    book.apply_snapshot(bids, asks, 1);

    auto result = book.snapshot();
    ASSERT_GE(result.bids.size(), 2u);
    ASSERT_GE(result.asks.size(), 2u);

    // Best bid should be highest
    EXPECT_DOUBLE_EQ(result.bids[0].price, 100000.0);
    EXPECT_DOUBLE_EQ(result.bids[0].quantity, 1.5);

    // Best ask should be lowest
    EXPECT_DOUBLE_EQ(result.asks[0].price, 100001.0);
    EXPECT_DOUBLE_EQ(result.asks[0].quantity, 1.0);
}

TEST_F(OrderBookTest, ApplyDeltaUpdatesLevels) {
    // First apply a snapshot
    std::vector<PriceLevel> bids = {{100000.0, 1.5}, {99999.0, 2.0}};
    std::vector<PriceLevel> asks = {{100001.0, 1.0}, {100002.0, 3.0}};
    book.apply_snapshot(bids, asks, 1);

    // Apply a delta that updates an existing level
    std::vector<PriceLevel> bid_deltas = {{100000.0, 2.5}};
    std::vector<PriceLevel> ask_deltas = {};
    book.apply_delta(bid_deltas, ask_deltas, 2);

    auto result = book.snapshot();
    ASSERT_GE(result.bids.size(), 2u);
    EXPECT_DOUBLE_EQ(result.bids[0].price, 100000.0);
    EXPECT_DOUBLE_EQ(result.bids[0].quantity, 2.5);
}

TEST_F(OrderBookTest, DeltaWithZeroQuantityRemovesLevel) {
    std::vector<PriceLevel> bids = {{100000.0, 1.5}, {99999.0, 2.0}};
    std::vector<PriceLevel> asks = {{100001.0, 1.0}};
    book.apply_snapshot(bids, asks, 1);

    // Remove a bid level by setting quantity to 0
    std::vector<PriceLevel> bid_deltas = {{100000.0, 0.0}};
    std::vector<PriceLevel> ask_deltas = {};
    book.apply_delta(bid_deltas, ask_deltas, 2);

    auto result = book.snapshot();
    // Top bid should now be 99999
    ASSERT_GE(result.bids.size(), 1u);
    EXPECT_DOUBLE_EQ(result.bids[0].price, 99999.0);
}

TEST_F(OrderBookTest, BestBidAsk) {
    std::vector<PriceLevel> bids = {{100000.0, 1.5}};
    std::vector<PriceLevel> asks = {{100001.0, 1.0}};
    book.apply_snapshot(bids, asks, 1);

    auto best_bid = book.best_bid();
    auto best_ask = book.best_ask();
    ASSERT_TRUE(best_bid.has_value());
    ASSERT_TRUE(best_ask.has_value());
    EXPECT_DOUBLE_EQ(*best_bid, 100000.0);
    EXPECT_DOUBLE_EQ(*best_ask, 100001.0);
}

TEST_F(OrderBookTest, EmptyBookBestBidAskReturnsNullopt) {
    auto best_bid = book.best_bid();
    auto best_ask = book.best_ask();
    EXPECT_FALSE(best_bid.has_value());
    EXPECT_FALSE(best_ask.has_value());
}

TEST_F(OrderBookTest, StalenessDetection) {
    std::vector<PriceLevel> bids = {{100000.0, 1.5}};
    std::vector<PriceLevel> asks = {{100001.0, 1.0}};
    book.apply_snapshot(bids, asks, 1);

    // With a very large threshold, it should not be stale
    EXPECT_FALSE(book.is_stale(std::chrono::milliseconds(60000)));
}

TEST_F(OrderBookTest, SnapshotPreservesExchangeAndPair) {
    std::vector<PriceLevel> bids = {{100000.0, 1.0}};
    std::vector<PriceLevel> asks = {{100001.0, 1.0}};
    book.apply_snapshot(bids, asks, 1);

    auto snap = book.snapshot();
    EXPECT_EQ(snap.exchange, Exchange::BINANCE);
    EXPECT_EQ(snap.pair, "BTC-USDT");
}

TEST_F(OrderBookTest, SnapshotDepthLimited) {
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
    for (int i = 0; i < 50; ++i) {
        bids.push_back({100000.0 - i, static_cast<double>(i + 1)});
        asks.push_back({100001.0 + i, static_cast<double>(i + 1)});
    }
    book.apply_snapshot(bids, asks, 1);

    // Request depth of 5
    auto snap = book.snapshot(5);
    EXPECT_LE(snap.bids.size(), 5u);
    EXPECT_LE(snap.asks.size(), 5u);
}
