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

  // Only keep pairs available on all exchanges
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

  // Create exchange adapters for all configured exchanges
  std::vector<std::unique_ptr<IExchange>> exchange_owners;
  std::unordered_map<Exchange, IExchange *> exchanges;

  for (auto& [exch_id, exch_cfg] : config.exchanges) {
    try {
      auto adapter = ExchangeFactory::create(exch_id, config);
      exchanges[exch_id] = adapter.get();
      exchange_owners.push_back(std::move(adapter));
    } catch (const std::exception& e) {
      LOG_ERROR("Failed to create adapter for {}: {}",
                exchange_to_string(exch_id), e.what());
    }
  }

  LOG_INFO("Created {} exchange adapters", exchanges.size());

  // Hardcoded USDT pairs for India/Global trading
  // Must use canonical "BASE-QUOTE" format used throughout the codebase
  std::vector<std::string> pairs = {"BTC-USDT", "ETH-USDT", "SOL-USDT"};

  // Initialize fee manager
  std::vector<IExchange *> exch_ptrs;
  for (auto& [exch_id, ptr] : exchanges) {
    exch_ptrs.push_back(ptr);
  }
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
          pair, [&book, &aggregator, pair](const OrderBookSnapshot &snap) {
            if (snap.is_delta) {
              book.apply_delta(snap.bids, snap.asks, snap.sequence_id);
            } else {
              book.apply_snapshot(snap.bids, snap.asks, snap.sequence_id);
            }
            aggregator.notify_book_update(pair);
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
    // Collect active exchange IDs for balance distribution,
    // skipping exchanges excluded from paper trading (e.g. Binance, MEXC)
    std::vector<Exchange> active_exchanges;
    for (auto& [exch_id, ptr] : exchanges) {
      if (config.paper_excluded_exchanges.count(exch_id) == 0) {
        active_exchanges.push_back(exch_id);
      } else {
        LOG_INFO("Excluding {} from paper balance distribution",
                 exchange_to_string(exch_id));
      }
    }
    paper_executor = std::make_unique<PaperExecutor>(
        config.paper_initial_balances, active_exchanges, aggregator,
        fee_manager, trade_logger, config.paper_realism);
    LOG_INFO("Paper trading mode active with {} exchanges",
             active_exchanges.size());
  } else {
    live_executor = std::make_unique<ExecutionEngine>(
        exchanges, order_manager, inventory_tracker, trade_logger);
    inventory_tracker.start_monitoring(
        std::chrono::seconds(config.balance_refresh_interval_s));
    LOG_INFO("Live trading mode active");
  }

  // Spread detector
  auto opp_callback = [&](const ArbitrageOpportunity &opp) {
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
  };

  SpreadDetector<decltype(opp_callback)> detector(
      aggregator, fee_manager, config.min_net_spread_bps,
      config.min_trade_size_usd, config.max_trade_size_usd, opp_callback);

  aggregator.set_update_callback(
      [&detector](const std::string &pair) { detector.scan_pair(pair); });

  // Signal handling
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  LOG_INFO("Bot is running. Press Ctrl+C to stop.");

  // Main loop: fast-path (100ms) for prices/spreads, slow-path (2s) for
  // PnL/balances Cached trade stats to avoid re-reading file every tick
  int cached_total_trades = 0;
  int cached_winning_trades = 0;
  double cached_total_fees = 0.0;
  std::map<std::string, double> cached_fees_per_exchange;
  auto last_slow_tick =
      std::chrono::steady_clock::now() - std::chrono::seconds(10);
  auto last_rebalance =
      std::chrono::steady_clock::now();
  constexpr int kRebalanceIntervalS = 60; // Rebalance virtual balances every 60s

  while (!g_shutdown) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    try {
      auto now = std::chrono::steady_clock::now();
      bool slow_tick = (std::chrono::duration_cast<std::chrono::seconds>(
                            now - last_slow_tick)
                            .count() >= 2);

      // ── Fast path (every 100ms): prices + spreads ──
      std::map<std::string, std::vector<MessageTypes::ExchangePrice>>
          all_prices;

      for (auto &pair : pairs) {
        auto snapshots = aggregator.get_pair_snapshots(pair, 5);

        // Collect live prices from all snapshots
        for (const auto &snap : snapshots) {
          MessageTypes::ExchangePrice ep;
          ep.exchange = std::string(exchange_to_string(snap.exchange));
          ep.bid = snap.bids.empty() ? 0.0 : snap.bids.front().price;
          ep.ask = snap.asks.empty() ? 0.0 : snap.asks.front().price;
          auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
              now - snap.local_timestamp);
          ep.age_ms = age.count();
          all_prices[pair].push_back(ep);
        }

        if (snapshots.size() < 2)
          continue;

        std::map<std::string, std::map<std::string, std::pair<double, double>>>
            spread_matrix;
        for (size_t i = 0; i < snapshots.size(); ++i) {
          for (size_t j = 0; j < snapshots.size(); ++j) {
            if (i == j)
              continue;
            const auto &buy_book = snapshots[i];
            const auto &sell_book = snapshots[j];
            if (buy_book.asks.empty() || sell_book.bids.empty())
              continue;

            double top_ask = buy_book.asks.front().price;
            double top_bid = sell_book.bids.front().price;
            if (top_ask <= 0.0)
              continue;

            double gross_bps = (top_bid - top_ask) / top_ask * 10000.0;
            double fee_rate = fee_manager.total_fee_rate(
                buy_book.exchange, sell_book.exchange, pair);
            double net_bps = gross_bps - fee_rate * 10000.0;

            spread_matrix[std::string(exchange_to_string(buy_book.exchange))]
                         [std::string(exchange_to_string(sell_book.exchange))] =
                             {gross_bps, net_bps};
          }
        }
        if (!spread_matrix.empty()) {
          ws_server.broadcast_spreads(pair, spread_matrix);
        }
      }

      if (!all_prices.empty()) {
        ws_server.broadcast_prices(all_prices);
      }

      // ── Slow path (every 2s): heartbeat, balances, PnL, alerts ──
      if (slow_tick) {
        last_slow_tick = now;

        ws_server.broadcast_heartbeat();

        // Periodic rebalance + settle: redistribute virtual balances across
        // exchanges and settle any pending transfers whose delay has elapsed
        if (config.mode == TradingMode::PAPER && paper_executor) {
          // Always try to settle arrived transfers (cheap no-op if none pending)
          paper_executor->settle_pending_transfers();

          auto since_rebalance =
              std::chrono::duration_cast<std::chrono::seconds>(
                  now - last_rebalance)
                  .count();
          if (since_rebalance >= kRebalanceIntervalS) {
            paper_executor->rebalance();
            last_rebalance = now;
          }
        }

        // Broadcast balances
        if (config.mode == TradingMode::PAPER && paper_executor) {
          auto vb = paper_executor->get_virtual_balances();
          std::map<Exchange, std::unordered_map<std::string, double>>
              balances_map;
          for (auto &[exch, assets] : vb)
            balances_map[exch] = assets;
          ws_server.broadcast_balances(balances_map);
        }

        // Broadcast P&L with cached trade stats
        double total_pnl = (config.mode == TradingMode::PAPER && paper_executor)
                               ? paper_executor->get_virtual_pnl()
                               : trade_logger.total_realized_pnl();
        std::map<std::string, double> pnl_per_pair;
        for (auto &pair : pairs) {
          pnl_per_pair[pair] = trade_logger.pnl_for_pair(pair);
        }

        // Refresh trade stats from disk (every 2s instead of every 100ms)
        auto all_trades = trade_logger.load_all_trades();
        cached_total_trades = static_cast<int>(all_trades.size());
        cached_winning_trades = 0;
        cached_total_fees = 0.0;
        cached_fees_per_exchange.clear();
        for (auto &t : all_trades) {
          if (t.realized_pnl > 0)
            cached_winning_trades++;
          cached_total_fees += t.buy_result.fee_paid + t.sell_result.fee_paid;
          cached_fees_per_exchange[std::string(
              exchange_to_string(t.buy_exchange))] += t.buy_result.fee_paid;
          cached_fees_per_exchange[std::string(
              exchange_to_string(t.sell_exchange))] += t.sell_result.fee_paid;
        }

        double win_rate =
            (cached_total_trades > 0)
                ? (100.0 * cached_winning_trades / cached_total_trades)
                : 0.0;

        ws_server.broadcast_pnl(total_pnl, pnl_per_pair, cached_total_trades,
                                win_rate, cached_total_fees,
                                cached_fees_per_exchange);

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
    } catch (const std::exception &e) {
      LOG_ERROR("Main loop error: {}", e.what());
    }
  }

  // Graceful shutdown
  LOG_INFO("Shutting down...");
  inventory_tracker.stop();
  fee_manager.stop();
  ws_server.stop();

  for (auto &[exch, adapter] : exchanges) {
    adapter->disconnect();
  }

  LOG_INFO("Shutdown complete.");
  return 0;
}
