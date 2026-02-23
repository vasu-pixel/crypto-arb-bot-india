#include "backtest/backtest_engine.h"
#include "orderbook/order_book_aggregator.h"
#include "orderbook/depth_analyzer.h"
#include "strategy/fee_manager.h"
#include "persistence/trade_logger.h"
#include "common/logger.h"
#include "common/crypto_utils.h"
#include "common/time_utils.h"
#include <set>
#include <algorithm>

BacktestEngine::BacktestEngine(const BacktestConfig& config)
    : config_(config) {}

BacktestMetrics BacktestEngine::run(const std::vector<HistoricalSnapshot>& data) {
    LOG_INFO("Starting backtest with {} snapshots", data.size());
    trades_.clear();

    // Create simulated exchanges with realistic per-exchange fee rates
    // Binance: 0.1% maker/taker, OKX: 0.08%/0.10%, Bybit: 0.1%/0.1%
    struct ExchangeFees { Exchange id; double maker; double taker; };
    std::vector<ExchangeFees> exch_fees = {
        {Exchange::BINANCE, 0.001,  0.001},
        {Exchange::OKX,     0.0008, 0.001},
        {Exchange::BYBIT,   0.001,  0.001},
    };

    std::map<Exchange, std::unique_ptr<SimulatedExchange>> sim_exchanges;
    for (auto& ef : exch_fees) {
        sim_exchanges[ef.id] = std::make_unique<SimulatedExchange>(
            ef.id, exchange_to_string(ef.id), config_.initial_balances,
            ef.maker, ef.taker);
        sim_exchanges[ef.id]->load_snapshots(data);
    }

    // Collect unique timestamps
    std::set<uint64_t> timestamps;
    for (auto& snap : data) {
        timestamps.insert(snap.timestamp_ms);
    }

    LOG_INFO("Processing {} unique timestamps", timestamps.size());

    OrderBookAggregator aggregator;

    // Initialize books for all pairs across all exchanges
    std::set<std::string> all_pairs;
    for (auto& snap : data) all_pairs.insert(snap.pair);

    // Filter to configured pairs if specified
    std::set<std::string> active_pairs;
    if (!config_.pairs.empty()) {
        for (auto& p : config_.pairs) {
            if (all_pairs.count(p)) active_pairs.insert(p);
        }
    } else {
        active_pairs = all_pairs;
    }

    // Process each timestamp
    for (uint64_t ts : timestamps) {
        // Advance all exchanges to this timestamp
        for (auto& [exch, sim] : sim_exchanges) {
            sim->advance_to(ts);
        }

        // Update order books in aggregator
        for (auto& [exch, sim] : sim_exchanges) {
            for (auto& pair : active_pairs) {
                auto snap = sim->fetch_order_book(pair, 20);
                if (!snap.bids.empty() || !snap.asks.empty()) {
                    auto& book = aggregator.get_or_create_book(exch, pair);
                    book.apply_snapshot(snap.bids, snap.asks, ts);
                }
            }
        }

        // Check for arbitrage opportunities
        for (auto& pair : active_pairs) {
            auto snapshots = aggregator.get_pair_snapshots(pair, 20);
            if (snapshots.size() < 2) continue;

            for (size_t i = 0; i < snapshots.size(); ++i) {
                for (size_t j = 0; j < snapshots.size(); ++j) {
                    if (i == j) continue;

                    auto& buy_snap = snapshots[i];  // Buy from asks
                    auto& sell_snap = snapshots[j];  // Sell into bids

                    if (buy_snap.asks.empty() || sell_snap.bids.empty()) continue;

                    double best_ask = buy_snap.asks[0].price;
                    double best_bid = sell_snap.bids[0].price;

                    if (best_bid <= best_ask) continue; // No spread

                    // Check with depth
                    double trade_size_usd = config_.min_trade_size_usd;
                    double quantity = trade_size_usd / best_ask;

                    auto eff_buy = DepthAnalyzer::effective_buy_price(buy_snap, quantity);
                    auto eff_sell = DepthAnalyzer::effective_sell_price(sell_snap, quantity);

                    if (!eff_buy.fully_fillable || !eff_sell.fully_fillable) continue;

                    // Get fees
                    auto buy_fee = sim_exchanges[buy_snap.exchange]->fetch_fees(pair);
                    auto sell_fee = sim_exchanges[sell_snap.exchange]->fetch_fees(pair);
                    double total_fee_rate = buy_fee.taker_fee + sell_fee.taker_fee;

                    double gross_spread_bps = (eff_sell.avg_price - eff_buy.avg_price) /
                                               eff_buy.avg_price * 10000.0;
                    double net_spread_bps = gross_spread_bps - total_fee_rate * 10000.0;

                    if (net_spread_bps < config_.min_net_spread_bps) continue;

                    // Execute on simulated exchanges
                    OrderRequest buy_req{buy_snap.exchange, pair, Side::BUY,
                                         eff_buy.avg_price * 1.001, quantity,
                                         CryptoUtils::generate_uuid()};
                    OrderRequest sell_req{sell_snap.exchange, pair, Side::SELL,
                                          eff_sell.avg_price * 0.999, quantity,
                                          CryptoUtils::generate_uuid()};

                    auto buy_result = sim_exchanges[buy_snap.exchange]->place_limit_order(buy_req);
                    auto sell_result = sim_exchanges[sell_snap.exchange]->place_limit_order(sell_req);

                    // Create trade record
                    TradeRecord record;
                    record.id = CryptoUtils::generate_uuid();
                    record.pair = pair;
                    record.buy_exchange = buy_snap.exchange;
                    record.sell_exchange = sell_snap.exchange;
                    record.buy_price = eff_buy.avg_price;
                    record.sell_price = eff_sell.avg_price;
                    record.quantity = quantity;
                    record.gross_spread_bps = gross_spread_bps;
                    record.net_spread_bps = net_spread_bps;
                    record.buy_result = buy_result;
                    record.sell_result = sell_result;
                    record.timestamp_iso = TimeUtils::to_iso8601(TimeUtils::from_ms(ts));
                    record.mode = TradingMode::BACKTEST;

                    double matched_qty = std::min(buy_result.filled_quantity,
                                                   sell_result.filled_quantity);
                    record.realized_pnl = (sell_result.avg_fill_price - buy_result.avg_fill_price)
                                          * matched_qty
                                          - buy_result.fee_paid - sell_result.fee_paid;

                    if (buy_result.status == OrderStatus::FILLED &&
                        sell_result.status == OrderStatus::FILLED) {
                        trades_.push_back(record);
                    }
                }
            }
        }
    }

    LOG_INFO("Backtest complete: {} trades executed", trades_.size());
    return BacktestReport::compute_metrics(trades_);
}
