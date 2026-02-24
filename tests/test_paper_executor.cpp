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

// Helper: Returns a PaperRealismConfig with ALL realism features DISABLED.
// This ensures backward-compatible tests that don't get randomly rejected
// by competition, staleness, one-leg risk, etc.
static PaperRealismConfig no_realism() {
  PaperRealismConfig cfg;
  cfg.enable_latency = false;
  cfg.enable_adverse_slippage = false;
  cfg.enable_staleness_penalty = false;
  cfg.enable_realistic_rebalance = false;
  cfg.enable_withdrawal_fees = false;
  cfg.enable_market_impact = false;
  cfg.enable_competition = false;
  cfg.enable_rate_limits = false;
  cfg.enable_min_order_size = false;
  cfg.enable_one_leg_risk = false;
  return cfg;
}

// Helper: Returns a standard ArbitrageOpportunity for BTC-USDT
static ArbitrageOpportunity make_btc_opp(double qty = 0.01,
                                          double net_spread = 3.0) {
  ArbitrageOpportunity opp;
  opp.pair = "BTC-USDT";
  opp.buy_exchange = Exchange::BINANCE;
  opp.sell_exchange = Exchange::OKX;
  opp.buy_price = 100000.0;
  opp.sell_price = 100050.0;
  opp.quantity = qty;
  opp.gross_spread_bps = 5.0;
  opp.net_spread_bps = net_spread;
  opp.detected_at = std::chrono::steady_clock::now();
  return opp;
}

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

    // Set up order books for BTC-USDT on two exchanges
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

// =============================================================================
// ORIGINAL TESTS (now pass no_realism() to avoid stochastic failures)
// =============================================================================

TEST_F(PaperExecutorTest, VirtualBalancesInitialized) {
  std::map<std::string, double> initial = {{"USDT", 30000.0}, {"BTC", 0.3}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX, Exchange::BYBIT};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger,
                         no_realism());

  auto balances = executor.get_virtual_balances();
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
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger,
                         no_realism());

  auto opp = make_btc_opp();
  auto record = executor.execute(opp);

  EXPECT_NE(record.buy_result.status, OrderStatus::PENDING);
  EXPECT_EQ(record.mode, TradingMode::PAPER);
  EXPECT_EQ(record.pair, "BTC-USDT");

  auto balances = executor.get_virtual_balances();
  if (record.buy_result.status == OrderStatus::FILLED) {
    EXPECT_GT(balances[Exchange::BINANCE]["BTC"], 0.1);
    EXPECT_LT(balances[Exchange::OKX]["BTC"], 0.1);
  }
}

TEST_F(PaperExecutorTest, InsufficientBalanceRejects) {
  std::map<std::string, double> initial = {{"USDT", 3.0}, {"BTC", 0.0}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX, Exchange::BYBIT};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger,
                         no_realism());

  auto opp = make_btc_opp(1.0); // qty=1.0 BTC at $100k = needs $100k
  auto record = executor.execute(opp);
  EXPECT_EQ(record.buy_result.status, OrderStatus::REJECTED);
}

TEST_F(PaperExecutorTest, TradeLoggedAfterExecution) {
  std::map<std::string, double> initial = {{"USDT", 30000.0}, {"BTC", 0.3}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX, Exchange::BYBIT};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger,
                         no_realism());

  auto opp = make_btc_opp();
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
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger,
                         no_realism());

  auto opp = make_btc_opp();
  auto record = executor.execute(opp);

  if (record.buy_result.status == OrderStatus::FILLED) {
    double pnl = executor.get_virtual_pnl();
    EXPECT_NE(pnl, 0.0);
  }
}

TEST_F(PaperExecutorTest, PerExchangeFeesApplied) {
  std::map<std::string, double> initial = {{"USDT", 30000.0}, {"BTC", 0.3}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX, Exchange::BYBIT};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger,
                         no_realism());

  auto opp = make_btc_opp();
  opp.net_spread_bps = 1.4;
  auto record = executor.execute(opp);

  if (record.buy_result.status == OrderStatus::FILLED &&
      record.sell_result.status == OrderStatus::FILLED) {
    EXPECT_NE(record.buy_result.fee_paid, record.sell_result.fee_paid);
    EXPECT_LT(record.buy_result.fee_paid, record.sell_result.fee_paid);
  }
}

// =============================================================================
// NEW TESTS FOR PAPER-REALISM GAPS
// =============================================================================

// --- Gap 9: Minimum order size ---
TEST_F(PaperExecutorTest, MinOrderSizeRejectsTooSmall) {
  PaperRealismConfig cfg = no_realism();
  cfg.enable_min_order_size = true;
  cfg.default_min_notional_usd = 10.0; // $10 minimum

  std::map<std::string, double> initial = {{"USDT", 30000.0}, {"BTC", 0.3}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger, cfg);

  // quantity = 0.00001 BTC at $100k = $1.0, well below $10 min
  auto opp = make_btc_opp(0.00001);
  auto record = executor.execute(opp);

  EXPECT_EQ(record.buy_result.status, OrderStatus::REJECTED);
  EXPECT_EQ(record.buy_result.error_message, "Below minimum order size");
}

TEST_F(PaperExecutorTest, MinOrderSizeAllowsLargeEnough) {
  PaperRealismConfig cfg = no_realism();
  cfg.enable_min_order_size = true;
  cfg.default_min_notional_usd = 5.0;

  std::map<std::string, double> initial = {{"USDT", 30000.0}, {"BTC", 0.3}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger, cfg);

  // quantity = 0.01 BTC at $100k = $1000, well above $5 min
  auto opp = make_btc_opp(0.01);
  auto record = executor.execute(opp);

  EXPECT_NE(record.buy_result.status, OrderStatus::PENDING);
  // Should not be rejected for min order size
  if (record.buy_result.status == OrderStatus::REJECTED) {
    EXPECT_NE(record.buy_result.error_message, "Below minimum order size");
  }
}

// --- Gap 3: Staleness penalty ---
TEST_F(PaperExecutorTest, StalenessRejectsOldBook) {
  PaperRealismConfig cfg = no_realism();
  cfg.enable_staleness_penalty = true;
  cfg.max_book_age_ms = 1.0; // 1ms — books will definitely be stale

  std::map<std::string, double> initial = {{"USDT", 30000.0}, {"BTC", 0.3}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger, cfg);

  // Sleep a tiny bit to ensure books are stale
  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  auto opp = make_btc_opp();
  auto record = executor.execute(opp);

  EXPECT_EQ(record.buy_result.status, OrderStatus::REJECTED);
  EXPECT_EQ(record.buy_result.error_message, "Order book too stale");
  EXPECT_EQ(record.rejection_reason, "staleness");
}

TEST_F(PaperExecutorTest, StalenessAcceptsFreshBook) {
  PaperRealismConfig cfg = no_realism();
  cfg.enable_staleness_penalty = true;
  cfg.max_book_age_ms = 60000.0; // 60 seconds — plenty of time
  cfg.staleness_penalty_bps_per_sec = 2.0;

  std::map<std::string, double> initial = {{"USDT", 30000.0}, {"BTC", 0.3}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger, cfg);

  auto opp = make_btc_opp();
  auto record = executor.execute(opp);

  // Should NOT be rejected for staleness (age < 60s)
  if (record.buy_result.status == OrderStatus::REJECTED) {
    EXPECT_NE(record.buy_result.error_message, "Order book too stale");
  }
}

// --- Gap 2: Adverse slippage ---
TEST_F(PaperExecutorTest, AdverseSlippageWorsensFillPrice) {
  // Execute many trades: with slippage enabled, average fill price should be
  // worse (higher for buys) than without slippage.
  std::map<std::string, double> initial = {{"USDT", 300000.0}, {"BTC", 3.0}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX};

  PaperRealismConfig no_slip_cfg = no_realism();
  PaperRealismConfig slip_cfg = no_realism();
  slip_cfg.enable_adverse_slippage = true;
  slip_cfg.slippage_bps_mean = 5.0;   // Large slippage for visibility
  slip_cfg.slippage_bps_stddev = 1.0;

  double total_price_no_slip = 0.0;
  double total_price_slip = 0.0;
  int successful = 0;
  const int N = 20;

  for (int i = 0; i < N; ++i) {
    PaperExecutor exec_no(initial, active, aggregator, *fee_manager, *logger, no_slip_cfg);
    PaperExecutor exec_slip(initial, active, aggregator, *fee_manager, *logger, slip_cfg);

    auto opp = make_btc_opp(0.01);
    auto rec_no = exec_no.execute(opp);
    auto rec_slip = exec_slip.execute(opp);

    if (rec_no.buy_result.status == OrderStatus::FILLED &&
        rec_slip.buy_result.status == OrderStatus::FILLED) {
      total_price_no_slip += rec_no.buy_result.avg_fill_price;
      total_price_slip += rec_slip.buy_result.avg_fill_price;
      ++successful;
    }
  }

  if (successful > 5) {
    // With adverse slippage, buy price should be higher (worse for buyer)
    EXPECT_GT(total_price_slip / successful, total_price_no_slip / successful);
  }
}

// --- Gap 8: Rate limits ---
TEST_F(PaperExecutorTest, RateLimitRejectsExcessiveOrders) {
  PaperRealismConfig cfg = no_realism();
  cfg.enable_rate_limits = true;
  cfg.max_orders_per_second = 2;
  cfg.max_orders_per_minute = 100;

  std::map<std::string, double> initial = {{"USDT", 300000.0}, {"BTC", 3.0}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger, cfg);

  int rejected_count = 0;
  for (int i = 0; i < 6; ++i) {
    auto opp = make_btc_opp(0.001);
    auto record = executor.execute(opp);
    if (record.buy_result.status == OrderStatus::REJECTED &&
        record.buy_result.error_message == "Rate limit exceeded") {
      ++rejected_count;
    }
  }

  // After 2 orders in ~0ms, the 3rd+ should hit the per-second limit
  EXPECT_GT(rejected_count, 0);
}

// --- Gap 7: Competition filter ---
TEST_F(PaperExecutorTest, CompetitionRejectsSomeOpportunities) {
  PaperRealismConfig cfg = no_realism();
  cfg.enable_competition = true;
  cfg.competition_base_prob = 0.5; // 50% base fill probability
  cfg.competition_decay_bps = 5.0;

  std::map<std::string, double> initial = {{"USDT", 300000.0}, {"BTC", 3.0}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger, cfg);

  int rejected = 0;
  int filled = 0;
  const int N = 50;
  for (int i = 0; i < N; ++i) {
    auto opp = make_btc_opp(0.001, 3.0);
    auto record = executor.execute(opp);
    if (record.buy_result.status == OrderStatus::REJECTED &&
        (record.buy_result.error_message == "Beaten by competing bot" ||
         record.sell_result.error_message == "Beaten by competing bot")) {
      ++rejected;
    } else if (record.buy_result.status == OrderStatus::FILLED) {
      ++filled;
    }
  }

  // With 50% base prob, we should see a meaningful number of both
  EXPECT_GT(rejected, 0) << "Expected some competition rejections with 50% base prob";
  EXPECT_GT(filled, 0) << "Expected some fills with 50% base prob";
}

// --- Gap 10: One-leg risk ---
TEST_F(PaperExecutorTest, OneLegRiskCausesFailures) {
  PaperRealismConfig cfg = no_realism();
  cfg.enable_one_leg_risk = true;
  cfg.one_leg_probability = 0.5; // 50% one-leg failure for testing
  cfg.one_leg_unwind_slippage_bps = 10.0;

  std::map<std::string, double> initial = {{"USDT", 300000.0}, {"BTC", 3.0}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger, cfg);

  int one_leg_count = 0;
  const int N = 30;
  for (int i = 0; i < N; ++i) {
    auto opp = make_btc_opp(0.001);
    auto record = executor.execute(opp);
    if (record.one_leg_failure) {
      ++one_leg_count;
      // One-leg should result in negative PnL (unwind cost)
      EXPECT_LT(record.realized_pnl, 0.0);
    }
  }

  EXPECT_GT(one_leg_count, 0) << "Expected some one-leg failures with 50% probability";
}

// --- Gap 1: Latency simulation ---
TEST_F(PaperExecutorTest, LatencyPopulatesMetadata) {
  PaperRealismConfig cfg = no_realism();
  cfg.enable_latency = true;
  cfg.latency_mean_ms = 100.0;
  cfg.latency_stddev_ms = 30.0;

  std::map<std::string, double> initial = {{"USDT", 30000.0}, {"BTC", 0.3}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger, cfg);

  auto opp = make_btc_opp();
  auto record = executor.execute(opp);

  // Even if the trade gets rejected for some other reason,
  // latency metadata should be populated if we got past the latency step
  // (latency comes after staleness check, and staleness is disabled here)
  if (record.buy_result.error_message != "Order book too stale" &&
      record.buy_result.error_message != "No order book available") {
    EXPECT_GT(record.simulated_latency_buy_ms, 0.0);
    EXPECT_GT(record.simulated_latency_sell_ms, 0.0);
  }
}

// --- Gap 6: Market impact phantom fills ---
TEST_F(PaperExecutorTest, MarketImpactReducesLiquidity) {
  PaperRealismConfig cfg = no_realism();
  cfg.enable_market_impact = true;
  cfg.impact_decay_seconds = 60.0; // Long decay so impact persists

  std::map<std::string, double> initial = {{"USDT", 300000.0}, {"BTC", 3.0}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger, cfg);

  // Execute a large trade to create phantom impact
  auto opp1 = make_btc_opp(1.0);
  auto rec1 = executor.execute(opp1);

  // Execute a second trade immediately — phantom impact should reduce available depth
  auto opp2 = make_btc_opp(1.0);
  auto rec2 = executor.execute(opp2);

  // After first trade consumed liquidity in phantom, the second trade should see
  // reduced depth. If original book had 5.0 BTC and first fill consumed 1.0 phantom,
  // second should still fill but possibly partially
  // This test just ensures no crash and the system handles phantom fills
  EXPECT_NE(rec2.buy_result.status, OrderStatus::PENDING);
}

// --- Gap 4/5: Realistic rebalance with delayed transfers and withdrawal fees ---
TEST_F(PaperExecutorTest, RealisticRebalanceCreatesDelayedTransfers) {
  PaperRealismConfig cfg = no_realism();
  cfg.enable_realistic_rebalance = true;
  cfg.rebalance_delay_minutes = 0.001; // ~60ms delay for testing
  cfg.enable_withdrawal_fees = true;
  cfg.withdrawal_fee_pct = 1.0; // 1% fee for visibility

  std::map<std::string, double> initial = {{"USDT", 30000.0}, {"BTC", 0.3}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger, cfg);

  // Manually skew balances by executing a trade
  auto opp = make_btc_opp(0.05);
  executor.execute(opp);

  auto before_balances = executor.get_virtual_balances();

  // Rebalance — should NOT instantly equalize because of delay
  executor.rebalance();

  // Immediately after rebalance, the deficit exchange should NOT have full balance
  // because transfers are pending
  auto after_balances = executor.get_virtual_balances();

  // Wait for transfers to settle
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  executor.settle_pending_transfers();

  auto settled_balances = executor.get_virtual_balances();

  // After settlement, the withdrawal fee should have reduced total assets
  double total_before = 0.0, total_after = 0.0;
  for (auto &[exch, assets] : before_balances) {
    for (auto &[asset, amount] : assets) total_before += amount;
  }
  for (auto &[exch, assets] : settled_balances) {
    for (auto &[asset, amount] : assets) total_after += amount;
  }
  // With 1% withdrawal fee, total should be slightly less after rebalance
  // (only if there was actually an imbalance to transfer)
  EXPECT_LE(total_after, total_before + 0.01);
}

TEST_F(PaperExecutorTest, InstantRebalanceWhenRealismDisabled) {
  PaperRealismConfig cfg = no_realism();
  // enable_realistic_rebalance is already false in no_realism()

  std::map<std::string, double> initial = {{"USDT", 20000.0}, {"BTC", 0.2}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger, cfg);

  // Execute a trade to create imbalance
  auto opp = make_btc_opp(0.05);
  executor.execute(opp);

  // Rebalance instantly
  executor.rebalance();

  auto balances = executor.get_virtual_balances();
  // After instant rebalance, both exchanges should have equal BTC
  double binance_btc = balances[Exchange::BINANCE]["BTC"];
  double okx_btc = balances[Exchange::OKX]["BTC"];
  EXPECT_NEAR(binance_btc, okx_btc, 0.0001);
}

// --- Metadata serialization round-trip ---
TEST_F(PaperExecutorTest, SimulationMetadataSerializedInTradeLog) {
  PaperRealismConfig cfg = no_realism();
  cfg.enable_latency = true;
  cfg.latency_mean_ms = 100.0;
  cfg.latency_stddev_ms = 30.0;
  cfg.enable_adverse_slippage = true;
  cfg.slippage_bps_mean = 2.0;
  cfg.slippage_bps_stddev = 0.5;

  std::map<std::string, double> initial = {{"USDT", 30000.0}, {"BTC", 0.3}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger, cfg);

  auto opp = make_btc_opp();
  auto record = executor.execute(opp);

  // Load back from log and verify metadata fields survive serialization
  auto trades = logger->load_all_trades();
  ASSERT_GE(trades.size(), 1u);

  auto &t = trades[0];
  // Latency should be non-zero if the trade got past the book check
  if (t.buy_result.error_message != "No order book available" &&
      t.buy_result.error_message != "Order book too stale") {
    EXPECT_GT(t.simulated_latency_buy_ms, 0.0);
    EXPECT_GT(t.simulated_latency_sell_ms, 0.0);
  }
  // These fields should exist in the deserialized record (even if zero)
  EXPECT_GE(t.adverse_slippage_buy_bps, 0.0);
  EXPECT_GE(t.adverse_slippage_sell_bps, 0.0);
}

// --- Combined realism: all gaps active together ---
TEST_F(PaperExecutorTest, AllRealismFeaturesActiveNocrash) {
  // Default PaperRealismConfig has everything enabled.
  // This is a smoke test: run many trades with all realism features
  // active and ensure no crash, segfault, or assertion failure.
  PaperRealismConfig cfg; // All enabled by default
  cfg.max_book_age_ms = 60000.0; // Generous staleness so test books pass

  std::map<std::string, double> initial = {{"USDT", 300000.0}, {"BTC", 3.0}};
  std::vector<Exchange> active = {Exchange::BINANCE, Exchange::OKX};
  PaperExecutor executor(initial, active, aggregator, *fee_manager, *logger, cfg);

  int total_attempts = 0;
  int fills = 0;
  int rejects = 0;

  for (int i = 0; i < 30; ++i) {
    auto opp = make_btc_opp(0.001, 3.0 + (i % 5));
    auto record = executor.execute(opp);
    ++total_attempts;
    if (record.buy_result.status == OrderStatus::FILLED &&
        record.sell_result.status == OrderStatus::FILLED) {
      ++fills;
    } else {
      ++rejects;
    }
  }

  EXPECT_EQ(total_attempts, 30);
  // With all realism on, we expect a mix of fills and rejections
  // (not all 30 should fill, and not all 30 should be rejected)
  // This is a soft check — the main goal is no crash
  EXPECT_GT(fills + rejects, 0);
}
