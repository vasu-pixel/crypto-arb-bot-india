#include "common/types.h"
#include "execution/paper_executor.h"
#include "orderbook/order_book_aggregator.h"
#include "persistence/trade_logger.h"
#include "strategy/fee_manager.h"
#include <filesystem>
#include <gtest/gtest.h>

// Minimal stub exchange for FeeManager in tests
class StubExchange : public IExchange {
public:
  StubExchange(Exchange id, double maker, double taker)
      : id_(id), maker_(maker), taker_(taker) {}

  Exchange exchange_id() const override { return id_; }
  std::string exchange_name() const override {
    return std::string(exchange_to_string(id_));
  }
  std::string normalize_pair(const std::string &p) const override { return p; }
  std::string canonical_pair(const std::string &p) const override { return p; }

  OrderBookSnapshot fetch_order_book(const std::string &, int) override {
    return {};
  }
  std::vector<std::pair<std::string, double>>
  fetch_top_pairs_by_volume(int) override {
    return {{"BTC-USDT", 1000000.0}};
  }
  FeeInfo fetch_fees(const std::string &pair) override {
    return {maker_, taker_, id_, pair};
  }
  OrderResult place_limit_order(const OrderRequest &) override { return {}; }
  OrderResult cancel_order(const std::string &, const std::string &) override {
    return {};
  }
  OrderResult query_order(const std::string &, const std::string &) override {
    return {};
  }
  std::vector<BalanceInfo> fetch_balances() override { return {}; }
  void subscribe_order_book(const std::string &, OrderBookCallback) override {}
  void unsubscribe_order_book(const std::string &) override {}
  void connect() override {}
  void disconnect() override {}
  bool is_connected() const override { return true; }

private:
  Exchange id_;
  double maker_, taker_;
};

class PaperExecutorTest : public ::testing::Test {
protected:
  std::string test_log_file;
  std::unique_ptr<TradeLogger> logger;
  OrderBookAggregator aggregator;
  // Stub exchanges with realistic per-exchange fees
  StubExchange binance_stub{Exchange::BINANCE, 0.001, 0.001};
  StubExchange okx_stub{Exchange::OKX, 0.0016, 0.0026};
  StubExchange bybit_stub{Exchange::BYBIT, 0.004, 0.006};
  std::vector<IExchange *> stub_exchanges;
  std::unique_ptr<FeeManager> fee_manager;

  void SetUp() override {
    test_log_file =
        "/tmp/paper_test_trades_" +
        std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
        ".jsonl";
    std::filesystem::remove(test_log_file);
    logger = std::make_unique<TradeLogger>(test_log_file);

    // Initialize fee manager with stub exchanges
    stub_exchanges = {&binance_stub, &okx_stub, &bybit_stub};
    fee_manager = std::make_unique<FeeManager>(stub_exchanges);
    fee_manager->refresh_all_fees();

    // Set up order books for BTC-USD on two exchanges
    auto &binance_book =
        aggregator.get_or_create_book(Exchange::BINANCE, "BTC-USDT");
    binance_book.apply_snapshot({{99990.0, 5.0}},  // bids
                                {{100000.0, 5.0}}, // asks
                                1);

    auto &okx_book = aggregator.get_or_create_book(Exchange::OKX, "BTC-USDT");
    okx_book.apply_snapshot({{100050.0, 5.0}}, // bids
                            {{100060.0, 5.0}}, // asks
                            1);
  }

  void TearDown() override { std::filesystem::remove(test_log_file); }
};

TEST_F(PaperExecutorTest, VirtualBalancesInitialized) {
  // Initial balances distributed across 3 exchanges equally
  std::map<std::string, double> initial = {{"USDT", 30000.0}, {"BTC", 0.3}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX, Exchange::BYBIT};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger);

  auto balances = executor.get_virtual_balances();
  // Each exchange should have 10000 USD and 0.1 BTC
  EXPECT_NEAR(balances[Exchange::BINANCE]["USDT"], 10000.0, 0.01);
  EXPECT_NEAR(balances[Exchange::BINANCE]["BTC"], 0.1, 0.001);
  EXPECT_NEAR(balances[Exchange::OKX]["USDT"], 10000.0, 0.01);
  EXPECT_NEAR(balances[Exchange::OKX]["BTC"], 0.1, 0.001);
  EXPECT_NEAR(balances[Exchange::BYBIT]["USDT"], 10000.0, 0.01);
  EXPECT_NEAR(balances[Exchange::BYBIT]["BTC"], 0.1, 0.001);
}

TEST_F(PaperExecutorTest, ExecuteArbitrageUpdatesBalances) {
  std::map<std::string, double> initial = {{"USDT", 30000.0}, {"BTC", 0.3}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX, Exchange::BYBIT};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger);

  ArbitrageOpportunity opp;
  opp.pair = "BTC-USDT";
  opp.buy_exchange = Exchange::BINANCE;
  opp.sell_exchange = Exchange::OKX;
  opp.buy_price = 100000.0;
  opp.sell_price = 100050.0;
  opp.quantity = 0.01;
  opp.gross_spread_bps = 5.0;
  opp.net_spread_bps = 3.0;

  auto record = executor.execute(opp);

  EXPECT_NE(record.buy_result.status, OrderStatus::PENDING);
  EXPECT_EQ(record.mode, TradingMode::PAPER);
  EXPECT_EQ(record.pair, "BTC-USDT");

  // Balances should have changed
  auto balances = executor.get_virtual_balances();

  if (record.buy_result.status == OrderStatus::FILLED) {
    EXPECT_GT(balances[Exchange::BINANCE]["BTC"], 0.1);
    EXPECT_LT(balances[Exchange::OKX]["BTC"], 0.1);
  }
}

TEST_F(PaperExecutorTest, InsufficientBalanceRejects) {
  std::map<std::string, double> initial = {{"USDT", 3.0}, {"BTC", 0.0}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX, Exchange::BYBIT};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger);

  ArbitrageOpportunity opp;
  opp.pair = "BTC-USDT";
  opp.buy_exchange = Exchange::BINANCE;
  opp.sell_exchange = Exchange::OKX;
  opp.buy_price = 100000.0;
  opp.sell_price = 100050.0;
  opp.quantity = 1.0;
  opp.gross_spread_bps = 5.0;
  opp.net_spread_bps = 3.0;

  auto record = executor.execute(opp);
  EXPECT_EQ(record.buy_result.status, OrderStatus::REJECTED);
}

TEST_F(PaperExecutorTest, TradeLoggedAfterExecution) {
  std::map<std::string, double> initial = {{"USDT", 30000.0}, {"BTC", 0.3}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX, Exchange::BYBIT};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger);

  ArbitrageOpportunity opp;
  opp.pair = "BTC-USDT";
  opp.buy_exchange = Exchange::BINANCE;
  opp.sell_exchange = Exchange::OKX;
  opp.buy_price = 100000.0;
  opp.sell_price = 100050.0;
  opp.quantity = 0.01;
  opp.gross_spread_bps = 5.0;
  opp.net_spread_bps = 3.0;

  executor.execute(opp);

  auto trades = logger->load_all_trades();
  EXPECT_GE(trades.size(), 1u);
  if (!trades.empty()) {
    EXPECT_EQ(trades[0].pair, "BTC-USDT");
    EXPECT_EQ(trades[0].mode, TradingMode::PAPER);
  }
}

TEST_F(PaperExecutorTest, PnlTracked) {
  std::map<std::string, double> initial = {{"USDT", 30000.0}, {"BTC", 0.3}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX, Exchange::BYBIT};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger);

  ArbitrageOpportunity opp;
  opp.pair = "BTC-USDT";
  opp.buy_exchange = Exchange::BINANCE;
  opp.sell_exchange = Exchange::OKX;
  opp.buy_price = 100000.0;
  opp.sell_price = 100050.0;
  opp.quantity = 0.01;
  opp.gross_spread_bps = 5.0;
  opp.net_spread_bps = 3.0;

  auto record = executor.execute(opp);

  if (record.buy_result.status == OrderStatus::FILLED) {
    double pnl = executor.get_virtual_pnl();
    EXPECT_NE(pnl, 0.0);
  }
}

TEST_F(PaperExecutorTest, PerExchangeFeesApplied) {
  // Verify that different exchanges get different fee rates
  std::map<std::string, double> initial = {{"USDT", 30000.0}, {"BTC", 0.3}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX, Exchange::BYBIT};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger);

  // Execute buy on Binance (0.1% taker) and sell on OKX (0.26% taker)
  ArbitrageOpportunity opp;
  opp.pair = "BTC-USDT";
  opp.buy_exchange = Exchange::BINANCE;
  opp.sell_exchange = Exchange::OKX;
  opp.buy_price = 100000.0;
  opp.sell_price = 100050.0;
  opp.quantity = 0.01;
  opp.gross_spread_bps = 5.0;
  opp.net_spread_bps = 1.4; // 5.0 - (0.1 + 0.26) * 100

  auto record = executor.execute(opp);

  if (record.buy_result.status == OrderStatus::FILLED &&
      record.sell_result.status == OrderStatus::FILLED) {
    // Buy fee should be ~0.1% of trade value (~$1.00)
    // Sell fee should be ~0.26% of trade value (~$2.60)
    // They should NOT be equal (proving per-exchange fees work)
    EXPECT_NE(record.buy_result.fee_paid, record.sell_result.fee_paid);
    // Buy fee should be less than sell fee (Binance < OKX)
    EXPECT_LT(record.buy_result.fee_paid, record.sell_result.fee_paid);
  }
}
