#include <gtest/gtest.h>
#include "strategy/spread_detector.h"
#include "orderbook/order_book_aggregator.h"
#include "common/types.h"

class SpreadDetectorTest : public ::testing::Test {
protected:
    OrderBookAggregator aggregator;

    void populate_books() {
        // Binance: lower asks (cheaper to buy)
        auto& binance_book = aggregator.get_or_create_book(Exchange::BINANCE, "BTC-USDT");
        binance_book.apply_snapshot(
            {{99990.0, 1.0}},  // bids
            {{100000.0, 1.0}}, // asks
            1
        );

        // OKX: higher bids (better to sell)
        auto& okx_book = aggregator.get_or_create_book(Exchange::OKX, "BTC-USDT");
        okx_book.apply_snapshot(
            {{100050.0, 1.0}}, // bids
            {{100060.0, 1.0}}, // asks
            1
        );
    }
};

TEST_F(SpreadDetectorTest, DetectsPositiveSpread) {
    populate_books();

    auto* binance_book = aggregator.get_book(Exchange::BINANCE, "BTC-USDT");
    auto* okx_book = aggregator.get_book(Exchange::OKX, "BTC-USDT");

    ASSERT_NE(binance_book, nullptr);
    ASSERT_NE(okx_book, nullptr);

    auto binance_snap = binance_book->snapshot();
    auto okx_snap = okx_book->snapshot();

    ASSERT_FALSE(binance_snap.asks.empty());
    ASSERT_FALSE(okx_snap.bids.empty());

    // Buy on Binance at 100000, sell on OKX at 100050
    double buy_price = binance_snap.asks[0].price;
    double sell_price = okx_snap.bids[0].price;
    EXPECT_GT(sell_price, buy_price);

    double gross_bps = (sell_price - buy_price) / buy_price * 10000.0;
    EXPECT_NEAR(gross_bps, 5.0, 0.1);
}

TEST_F(SpreadDetectorTest, NoSpreadWhenPricesEqual) {
    auto& binance_book = aggregator.get_or_create_book(Exchange::BINANCE, "ETH-USDT");
    binance_book.apply_snapshot(
        {{3000.0, 10.0}},  // bids
        {{3010.0, 10.0}},  // asks
        1
    );

    auto& okx_book = aggregator.get_or_create_book(Exchange::OKX, "ETH-USDT");
    okx_book.apply_snapshot(
        {{3000.0, 10.0}},  // bids
        {{3010.0, 10.0}},  // asks
        1
    );

    auto* binance_ptr = aggregator.get_book(Exchange::BINANCE, "ETH-USDT");
    auto* okx_ptr = aggregator.get_book(Exchange::OKX, "ETH-USDT");

    auto binance_snap = binance_ptr->snapshot();
    auto okx_snap = okx_ptr->snapshot();

    double buy_price = binance_snap.asks[0].price;
    double sell_price = okx_snap.bids[0].price;
    EXPECT_LE(sell_price, buy_price);
}

TEST_F(SpreadDetectorTest, FeeDeductionReducesSpread) {
    double buy_price = 100000.0;
    double sell_price = 100050.0;

    double buy_fee_rate = 0.001;
    double sell_fee_rate = 0.001;
    double buy_cost = buy_price * (1.0 + buy_fee_rate);
    double sell_revenue = sell_price * (1.0 - sell_fee_rate);
    double net_profit = sell_revenue - buy_cost;
    EXPECT_LT(net_profit, 0.0);

    buy_fee_rate = 0.0001;
    sell_fee_rate = 0.0001;
    buy_cost = buy_price * (1.0 + buy_fee_rate);
    sell_revenue = sell_price * (1.0 - sell_fee_rate);
    net_profit = sell_revenue - buy_cost;
    EXPECT_GT(net_profit, 0.0);
}

TEST_F(SpreadDetectorTest, AggregatorReturnsPairsCorrectly) {
    populate_books();
    auto pairs = aggregator.get_pairs();
    bool found = false;
    for (const auto& p : pairs) {
        if (p == "BTC-USDT") found = true;
    }
    EXPECT_TRUE(found);
}
