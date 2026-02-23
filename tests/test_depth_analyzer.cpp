#include "common/types.h"
#include "orderbook/depth_analyzer.h"
#include <gtest/gtest.h>

class DepthAnalyzerTest : public ::testing::Test {
protected:
  OrderBookSnapshot make_book() {
    OrderBookSnapshot snap;
    snap.exchange = Exchange::BINANCE;
    snap.pair = "BTC-USDT";
    // 3 bid levels (best first: descending)
    snap.bids = {{100000.0, 1.0}, {99990.0, 2.0}, {99980.0, 3.0}};
    // 3 ask levels (best first: ascending)
    snap.asks = {{100010.0, 1.0}, {100020.0, 2.0}, {100030.0, 3.0}};
    return snap;
  }
};

TEST_F(DepthAnalyzerTest, EffectiveBuyPriceSingleLevel) {
  auto snap = make_book();
  // Buying 0.5 BTC - fits entirely in the first ask level
  auto result = DepthAnalyzer::effective_buy_price(snap, 0.5);
  EXPECT_TRUE(result.fully_fillable);
  EXPECT_DOUBLE_EQ(result.avg_price, 100010.0);
  EXPECT_NEAR(result.total_cost, 100010.0 * 0.5, 0.01);
  EXPECT_DOUBLE_EQ(result.quantity_filled, 0.5);
}

TEST_F(DepthAnalyzerTest, EffectiveBuyPriceMultipleLevels) {
  auto snap = make_book();
  // Buying 2.0 BTC - needs to walk 2 ask levels
  // 1.0 @ 100010 + 1.0 @ 100020 = 200030
  // VWAP = 200030 / 2.0 = 100015.0
  auto result = DepthAnalyzer::effective_buy_price(snap, 2.0);
  EXPECT_TRUE(result.fully_fillable);
  EXPECT_NEAR(result.avg_price, 100015.0, 0.01);
  EXPECT_DOUBLE_EQ(result.quantity_filled, 2.0);
}

TEST_F(DepthAnalyzerTest, EffectiveSellPriceSingleLevel) {
  auto snap = make_book();
  // Selling 0.5 BTC - fits in the first bid level
  auto result = DepthAnalyzer::effective_sell_price(snap, 0.5);
  EXPECT_TRUE(result.fully_fillable);
  EXPECT_DOUBLE_EQ(result.avg_price, 100000.0);
  EXPECT_DOUBLE_EQ(result.quantity_filled, 0.5);
}

TEST_F(DepthAnalyzerTest, EffectiveSellPriceMultipleLevels) {
  auto snap = make_book();
  // Selling 2.0 BTC - needs 2 bid levels
  // 1.0 @ 100000 + 1.0 @ 99990 = 199990
  // VWAP = 199990 / 2.0 = 99995.0
  auto result = DepthAnalyzer::effective_sell_price(snap, 2.0);
  EXPECT_TRUE(result.fully_fillable);
  EXPECT_NEAR(result.avg_price, 99995.0, 0.01);
  EXPECT_DOUBLE_EQ(result.quantity_filled, 2.0);
}

TEST_F(DepthAnalyzerTest, InsufficientLiquidity) {
  auto snap = make_book();
  // Total ask liquidity is 1+2+3 = 6 BTC, try buying 10
  auto result = DepthAnalyzer::effective_buy_price(snap, 10.0);
  EXPECT_FALSE(result.fully_fillable);
  EXPECT_LE(result.quantity_filled, 6.0);
}

TEST_F(DepthAnalyzerTest, EmptyBookNotFillable) {
  OrderBookSnapshot empty;
  empty.exchange = Exchange::BINANCE;
  empty.pair = "BTC-USDT";

  auto buy_result = DepthAnalyzer::effective_buy_price(empty, 1.0);
  EXPECT_FALSE(buy_result.fully_fillable);
  EXPECT_DOUBLE_EQ(buy_result.quantity_filled, 0.0);

  auto sell_result = DepthAnalyzer::effective_sell_price(empty, 1.0);
  EXPECT_FALSE(sell_result.fully_fillable);
  EXPECT_DOUBLE_EQ(sell_result.quantity_filled, 0.0);
}

TEST_F(DepthAnalyzerTest, MaxArbQuantityPositiveSpread) {
  auto buy_book = make_book(); // asks start at 100010
  OrderBookSnapshot sell_book;
  sell_book.exchange = Exchange::OKX;
  sell_book.pair = "BTC-USDT";
  sell_book.bids = {{101050.0, 1.0}, {101040.0, 2.0}, {101030.0, 3.0}};
  sell_book.asks = {{100060.0, 1.0}};

  double max_qty =
      DepthAnalyzer::max_arb_quantity(buy_book, sell_book, 5.0, 0.002);

  EXPECT_GT(max_qty, 0.0);
}

TEST_F(DepthAnalyzerTest, MaxArbQuantityNoOpportunity) {
  auto buy_book = make_book(); // asks start at 100010
  OrderBookSnapshot sell_book;
  sell_book.exchange = Exchange::OKX;
  sell_book.pair = "BTC-USDT";
  sell_book.bids = {{100005.0, 1.0}, {100000.0, 2.0}};
  sell_book.asks = {{100010.0, 1.0}};

  double max_qty =
      DepthAnalyzer::max_arb_quantity(buy_book, sell_book, 5.0, 0.002);

  EXPECT_DOUBLE_EQ(max_qty, 0.0);
}
