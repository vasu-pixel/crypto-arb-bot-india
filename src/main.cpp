#include "backtest/backtest_engine.h"
#include "backtest/backtest_report.h"
#include "backtest/data_loader.h"
#include "common/config.h"
#include "common/logger.h"
#include "common/time_utils.h"
#include "common/types.h"
#include "exchange/exchange_factory.h"
#include "execution/execution_engine.h"
#include "execution/inventory_tracker.h"
#include "execution/order_manager.h"
#include "execution/paper_executor.h"
#include "orderbook/order_book.h"
#include "orderbook/order_book_aggregator.h"
#include "persistence/trade_logger.h"
#include "server/message_types.h"
#include "server/ws_server.h"
#include "strategy/fee_manager.h"
#include "strategy/spread_detector.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <iostream>
#include <set>

std::atomic<bool> g_shutdown{false};
void signal_handler(int) { g_shutdown = true; }

// Discover top pairs by volume across all exchanges
std::vector<std::string>
discover_top_pairs(std::unordered_map<Exchange, IExchange *> &exchanges,
                   int limit) {

  std::map<std::string, double> volume_sum;
  std::map<std::string, int> pair_exchange_count;

  for (auto &[exch, adapter] : exchanges) {
    try {
      auto pairs = adapter->fetch_top_pairs_by_volume(50);
      for (auto &[pair, volume] : pairs) {
        volume_sum[pair] += volume;
        pair_exchange_count[pair]++;
      }
    } catch (const std::exception &e) {
      LOG_ERROR("Failed to fetch pairs from {}: {}", exchange_to_string(exch),
                e.what());
    }
  }

  // Only keep pairs available on all 3 exchanges
  std::vector<std::pair<std::string, double>> candidates;
  for (auto &[pair, volume] : volume_sum) {
    if (pair_exchange_count[pair] >= static_cast<int>(exchanges.size())) {
      candidates.push_back({pair, volume / exchanges.size()});
    }
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const auto &a, const auto &b) { return a.second > b.second; });

  std::vector<std::string> result;
  for (int i = 0; i < limit && i < static_cast<int>(candidates.size()); ++i) {
    result.push_back(candidates[i].first);
    LOG_INFO("Monitoring pair: {} (avg volume: {:.0f})", candidates[i].first,
             candidates[i].second);
  }
  return result;
}

int main(int argc, char **argv) {
  std::string config_path = "config/config.json";
  bool run_backtest = false;
  std::string backtest_from, backtest_to;

  // Parse command line
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc)
      config_path = argv[++i];
    else if (arg == "--backtest")
      run_backtest = true;
    else if (arg == "--from" && i + 1 < argc)
      backtest_from = argv[++i];
    else if (arg == "--to" && i + 1 < argc)
      backtest_to = argv[++i];
  }

  // Load config
  Config config;
  try {
    config = Config::load(config_path);
  } catch (const std::exception &e) {
    std::cerr << "Failed to load config: " << e.what() << std::endl;
    return 1;
  }

  Logger::init(config.log_level, config.log_file);
  LOG_INFO("Crypto Arb Bot v1.0.0 starting in {} mode",
           trading_mode_to_string(config.mode));

  // Handle backtest mode
  if (run_backtest || config.mode == TradingMode::BACKTEST) {
    LOG_INFO("Running backtest...");
    auto data = DataLoader::load_csv_directory(config.historical_data_dir);
    if (data.empty()) {
      LOG_ERROR("No historical data found in {}", config.historical_data_dir);
      return 1;
    }

    BacktestConfig bt_config;
    bt_config.from_date = backtest_from;
    bt_config.to_date = backtest_to;
    bt_config.min_net_spread_bps = config.min_net_spread_bps;
    bt_config.min_trade_size_usd = config.min_trade_size_usd;
    bt_config.max_trade_size_usd = config.max_trade_size_usd;
    bt_config.initial_balances = config.paper_initial_balances;

    BacktestEngine engine(bt_config);
    auto metrics = engine.run(data);

    std::cout << BacktestReport::format_report(metrics);
    BacktestReport::save_json(metrics, "data/backtest_results.json");
    return 0;
  }

  // Create exchange adapters
  auto binance = ExchangeFactory::create(Exchange::BINANCE_US, config);
  auto kraken = ExchangeFactory::create(Exchange::KRAKEN, config);
  auto coinbase = ExchangeFactory::create(Exchange::COINBASE, config);

  std::unordered_map<Exchange, IExchange *> exchanges = {
      {Exchange::BINANCE_US, binance.get()},
      {Exchange::KRAKEN, kraken.get()},
      {Exchange::COINBASE, coinbase.get()},
  };

  // Discover top pairs
  // auto pairs = discover_top_pairs(exchanges, 10);
  // if (pairs.empty()) {
  //     LOG_ERROR("No common pairs found across exchanges");
  //     return 1;
  // }

  // Hardcoded pairs to bypass REST API discovery and run without API keys
  // Must use canonical "BASE-QUOTE" format used throughout the codebase
  std::vector<std::string> pairs = {"BTC-USD", "ETH-USD", "SOL-USD"};

  // Initialize fee manager
  std::vector<IExchange *> exch_ptrs = {binance.get(), kraken.get(),
                                        coinbase.get()};
  FeeManager fee_manager(exch_ptrs);
  try {
    fee_manager.refresh_all_fees();
  } catch (const std::exception &e) {
    LOG_WARN("Failed to fetch initial fees: {}", e.what());
  }
  fee_manager.start_periodic_refresh();

  // Create order book aggregator
  OrderBookAggregator aggregator;

  // Subscribe to order books for all pairs on all exchanges
  for (auto &[exch, adapter] : exchanges) {
    for (auto &pair : pairs) {
      auto &book = aggregator.get_or_create_book(exch, pair);
      adapter->subscribe_order_book(
          pair, [&book](const OrderBookSnapshot &snap) {
            if (snap.is_delta) {
              book.apply_delta(snap.bids, snap.asks, snap.sequence_id);
            } else {
              book.apply_snapshot(snap.bids, snap.asks, snap.sequence_id);
            }
          });
    }
  }

  // Connect all exchanges
  for (auto &[exch, adapter] : exchanges) {
    try {
      adapter->connect();
      LOG_INFO("{} connected", exchange_to_string(exch));
    } catch (const std::exception &e) {
      LOG_ERROR("Failed to connect {}: {}", exchange_to_string(exch), e.what());
    }
  }

  // Initialize trade logger
  std::string trades_file = (config.mode == TradingMode::PAPER)
                                ? config.paper_trades_file
                                : config.trades_file;
  TradeLogger trade_logger(trades_file);

  // Dashboard WS server
  DashboardWsServer ws_server(config.ws_port);
  ws_server.start();

  // Initialize execution components
  OrderManager order_manager;
  InventoryTracker inventory_tracker(exchanges, config.drift_threshold_pct);

  // Create appropriate executor based on mode
  std::unique_ptr<PaperExecutor> paper_executor;
  std::unique_ptr<ExecutionEngine> live_executor;

  if (config.mode == TradingMode::PAPER) {
    paper_executor = std::make_unique<PaperExecutor>(
        config.paper_initial_balances, aggregator, fee_manager, trade_logger);
    LOG_INFO("Paper trading mode active");
  } else {
    live_executor = std::make_unique<ExecutionEngine>(
        exchanges, order_manager, inventory_tracker, trade_logger);
    inventory_tracker.start_monitoring(
        std::chrono::seconds(config.balance_refresh_interval_s));
    LOG_INFO("Live trading mode active");
  }

  // Spread detector
  SpreadDetector detector(aggregator, fee_manager, config.min_net_spread_bps,
                          config.min_trade_size_usd, config.max_trade_size_usd);

  detector.set_opportunity_callback([&](const ArbitrageOpportunity &opp) {
    LOG_INFO("Opportunity: {} buy@{}({}) sell@{}({}) net={}bps", opp.pair,
             opp.buy_price, exchange_to_string(opp.buy_exchange),
             opp.sell_price, exchange_to_string(opp.sell_exchange),
             opp.net_spread_bps);

    TradeRecord record;
    if (config.mode == TradingMode::PAPER && paper_executor) {
      record = paper_executor->execute(opp);
    } else if (live_executor) {
      record = live_executor->execute(opp);
    } else {
      return;
    }

    ws_server.broadcast_trade(record);
  });
  detector.start();

  // Signal handling
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  LOG_INFO("Bot is running. Press Ctrl+C to stop.");

  // Main loop: broadcast periodic updates
  while (!g_shutdown) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Broadcast heartbeat
    ws_server.broadcast_heartbeat();

    // Broadcast balances
    if (config.mode == TradingMode::PAPER && paper_executor) {
      auto vb = paper_executor->get_virtual_balances();
      std::map<Exchange, std::unordered_map<std::string, double>> balances_map;
      for (auto &[exch, assets] : vb)
        balances_map[exch] = assets;
      ws_server.broadcast_balances(balances_map);
    }

    // Broadcast P&L
    double total_pnl = (config.mode == TradingMode::PAPER && paper_executor)
                           ? paper_executor->get_virtual_pnl()
                           : trade_logger.total_realized_pnl();
    std::map<std::string, double> pnl_per_pair;
    for (auto &pair : pairs) {
      pnl_per_pair[pair] = trade_logger.pnl_for_pair(pair);
    }
    ws_server.broadcast_pnl(total_pnl, pnl_per_pair);

    // Check drift alerts
    if (config.mode == TradingMode::LIVE) {
      auto alerts = inventory_tracker.check_drift();
      for (auto &alert : alerts) {
        nlohmann::json alert_j;
        to_json(alert_j, alert);
        auto envelope = MessageTypes::make_envelope("alert", alert_j);
        ws_server.broadcast(envelope.dump());
        LOG_WARN("Drift: {}", alert.message);
      }
    }
  }

  // Graceful shutdown
  LOG_INFO("Shutting down...");
  detector.stop();
  inventory_tracker.stop();
  fee_manager.stop();
  ws_server.stop();

  for (auto &[exch, adapter] : exchanges) {
    adapter->disconnect();
  }

  LOG_INFO("Shutdown complete.");
  return 0;
}
