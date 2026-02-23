#include <gtest/gtest.h>
#include "persistence/trade_logger.h"
#include "common/types.h"
#include <filesystem>
#include <fstream>

class TradeLoggerTest : public ::testing::Test {
protected:
    std::string test_file;

    void SetUp() override {
        test_file = "/tmp/test_trades_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + ".jsonl";
        // Remove any leftover file
        std::filesystem::remove(test_file);
    }

    void TearDown() override {
        std::filesystem::remove(test_file);
    }

    TradeRecord make_trade(const std::string& id, double pnl) {
        TradeRecord t;
        t.id = id;
        t.pair = "BTC-USDT";
        t.buy_exchange = Exchange::BINANCE;
        t.sell_exchange = Exchange::OKX;
        t.buy_price = 100000.0;
        t.sell_price = 100050.0;
        t.quantity = 0.01;
        t.gross_spread_bps = 5.0;
        t.net_spread_bps = 3.0;
        t.realized_pnl = pnl;
        t.timestamp_iso = "2026-02-19T12:00:00Z";
        t.mode = TradingMode::PAPER;
        t.buy_result.status = OrderStatus::FILLED;
        t.buy_result.filled_quantity = 0.01;
        t.buy_result.avg_fill_price = 100000.0;
        t.sell_result.status = OrderStatus::FILLED;
        t.sell_result.filled_quantity = 0.01;
        t.sell_result.avg_fill_price = 100050.0;
        return t;
    }
};

TEST_F(TradeLoggerTest, LogAndLoadSingleTrade) {
    TradeLogger logger(test_file);
    auto trade = make_trade("trade_1", 0.50);
    logger.log_trade(trade);

    auto trades = logger.load_all_trades();
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].id, "trade_1");
    EXPECT_EQ(trades[0].pair, "BTC-USDT");
    EXPECT_DOUBLE_EQ(trades[0].realized_pnl, 0.50);
    EXPECT_EQ(trades[0].mode, TradingMode::PAPER);
}

TEST_F(TradeLoggerTest, LogMultipleTrades) {
    TradeLogger logger(test_file);
    logger.log_trade(make_trade("t1", 0.10));
    logger.log_trade(make_trade("t2", -0.05));
    logger.log_trade(make_trade("t3", 0.25));

    auto trades = logger.load_all_trades();
    ASSERT_EQ(trades.size(), 3u);
    EXPECT_EQ(trades[0].id, "t1");
    EXPECT_EQ(trades[1].id, "t2");
    EXPECT_EQ(trades[2].id, "t3");
}

TEST_F(TradeLoggerTest, TotalRealizedPnl) {
    TradeLogger logger(test_file);
    logger.log_trade(make_trade("t1", 0.10));
    logger.log_trade(make_trade("t2", -0.05));
    logger.log_trade(make_trade("t3", 0.25));

    double total = logger.total_realized_pnl();
    EXPECT_NEAR(total, 0.30, 0.001);
}

TEST_F(TradeLoggerTest, PnlForPair) {
    TradeLogger logger(test_file);
    auto t1 = make_trade("t1", 0.10);
    t1.pair = "BTC-USDT";

    auto t2 = make_trade("t2", 0.20);
    t2.pair = "ETH-USDT";

    auto t3 = make_trade("t3", 0.30);
    t3.pair = "BTC-USDT";

    logger.log_trade(t1);
    logger.log_trade(t2);
    logger.log_trade(t3);

    double btc_pnl = logger.pnl_for_pair("BTC-USDT");
    EXPECT_NEAR(btc_pnl, 0.40, 0.001);

    double eth_pnl = logger.pnl_for_pair("ETH-USDT");
    EXPECT_NEAR(eth_pnl, 0.20, 0.001);
}

TEST_F(TradeLoggerTest, LoadFromEmptyFile) {
    TradeLogger logger(test_file);
    auto trades = logger.load_all_trades();
    EXPECT_TRUE(trades.empty());
}

TEST_F(TradeLoggerTest, PreservesOrderResultFields) {
    TradeLogger logger(test_file);
    auto trade = make_trade("t1", 1.0);
    trade.buy_result.exchange_order_id = "BIN123";
    trade.buy_result.fee_paid = 0.05;
    trade.sell_result.exchange_order_id = "KRK456";
    trade.sell_result.fee_paid = 0.04;
    logger.log_trade(trade);

    auto loaded = logger.load_all_trades();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].buy_result.exchange_order_id, "BIN123");
    EXPECT_DOUBLE_EQ(loaded[0].buy_result.fee_paid, 0.05);
    EXPECT_EQ(loaded[0].sell_result.exchange_order_id, "KRK456");
    EXPECT_DOUBLE_EQ(loaded[0].sell_result.fee_paid, 0.04);
    EXPECT_EQ(loaded[0].buy_result.status, OrderStatus::FILLED);
    EXPECT_EQ(loaded[0].sell_result.status, OrderStatus::FILLED);
}
