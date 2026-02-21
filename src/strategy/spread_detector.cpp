#include "strategy/spread_detector.h"
#include "orderbook/depth_analyzer.h"
#include "common/logger.h"

#include <chrono>
#include <cmath>
#include <algorithm>

SpreadDetector::SpreadDetector(OrderBookAggregator& aggregator,
                               FeeManager& fee_manager,
                               double min_net_spread_bps,
                               double min_trade_size_usd,
                               double max_trade_size_usd)
    : aggregator_(aggregator)
    , fee_manager_(fee_manager)
    , min_net_spread_bps_(min_net_spread_bps)
    , min_trade_size_usd_(min_trade_size_usd)
    , max_trade_size_usd_(max_trade_size_usd)
{
}

SpreadDetector::~SpreadDetector()
{
    stop();
}

void SpreadDetector::set_opportunity_callback(OpportunityCallback cb)
{
    callback_ = std::move(cb);
}

void SpreadDetector::start()
{
    if (running_.exchange(true)) {
        LOG_WARN("SpreadDetector already running");
        return;
    }

    monitor_thread_ = std::thread([this]() {
        LOG_INFO("SpreadDetector started (min_net_spread={}bps, trade_range=[${}, ${}])",
                 min_net_spread_bps_, min_trade_size_usd_, max_trade_size_usd_);
        monitor_loop();
        LOG_INFO("SpreadDetector stopped");
    });
}

void SpreadDetector::stop()
{
    if (!running_.exchange(false)) {
        return;
    }

    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
}

void SpreadDetector::monitor_loop()
{
    while (running_.load()) {
        auto scan_start = std::chrono::steady_clock::now();

        try {
            auto pairs = aggregator_.get_pairs();
            for (const auto& pair : pairs) {
                if (!running_.load()) break;
                scan_pair(pair);
            }
        } catch (const std::exception& ex) {
            LOG_ERROR("SpreadDetector scan error: {}", ex.what());
        }

        // Sleep for ~100ms minus the time we spent scanning
        auto scan_duration = std::chrono::steady_clock::now() - scan_start;
        auto sleep_time = std::chrono::milliseconds(100) - scan_duration;
        if (sleep_time > std::chrono::milliseconds::zero()) {
            std::this_thread::sleep_for(sleep_time);
        }
    }
}

void SpreadDetector::scan_pair(const std::string& pair)
{
    // Get snapshots from all exchanges that have a book for this pair
    auto snapshots = aggregator_.get_pair_snapshots(pair, 20);
    if (snapshots.size() < 2) return;

    // Check all (buy_exchange, sell_exchange) permutations
    for (size_t i = 0; i < snapshots.size(); ++i) {
        for (size_t j = 0; j < snapshots.size(); ++j) {
            if (i == j) continue;

            const auto& buy_book = snapshots[i];   // we buy from this exchange (consume asks)
            const auto& sell_book = snapshots[j];   // we sell on this exchange (consume bids)

            if (buy_book.asks.empty() || sell_book.bids.empty()) continue;

            // Skip stale books (no update in >5s)
            auto now = std::chrono::steady_clock::now();
            auto buy_age = std::chrono::duration_cast<std::chrono::milliseconds>(now - buy_book.local_timestamp);
            auto sell_age = std::chrono::duration_cast<std::chrono::milliseconds>(now - sell_book.local_timestamp);
            if (buy_age.count() > 5000 || sell_age.count() > 5000) continue;

            // Quick top-of-book check before doing expensive depth analysis
            double top_ask = buy_book.asks.front().price;
            double top_bid = sell_book.bids.front().price;
            if (top_ask <= 0.0 || top_bid <= top_ask) continue;

            double fee_rate = fee_manager_.total_fee_rate(buy_book.exchange, sell_book.exchange, pair);

            // Quick gross spread check
            double quick_gross_bps = (top_bid - top_ask) / top_ask * 10000.0;
            double quick_net_bps = quick_gross_bps - fee_rate * 10000.0;
            if (quick_net_bps < min_net_spread_bps_) continue;

            // Find maximum arbitrageable quantity
            double max_qty = DepthAnalyzer::max_arb_quantity(
                buy_book, sell_book, min_net_spread_bps_, fee_rate);

            if (max_qty <= 0.0) continue;

            // Compute effective prices at max_qty
            auto buy_eff = DepthAnalyzer::effective_buy_price(buy_book, max_qty);
            auto sell_eff = DepthAnalyzer::effective_sell_price(sell_book, max_qty);

            if (!buy_eff.fully_fillable || !sell_eff.fully_fillable) continue;
            if (buy_eff.avg_price <= 0.0) continue;

            // Compute trade size in USD and clamp quantity
            double trade_size_usd = buy_eff.avg_price * max_qty;

            // If trade is below minimum, skip
            if (trade_size_usd < min_trade_size_usd_) continue;

            // If trade is above maximum, scale down quantity
            double actual_qty = max_qty;
            if (trade_size_usd > max_trade_size_usd_) {
                actual_qty = max_trade_size_usd_ / buy_eff.avg_price;
                // Recompute effective prices for the clamped quantity
                buy_eff = DepthAnalyzer::effective_buy_price(buy_book, actual_qty);
                sell_eff = DepthAnalyzer::effective_sell_price(sell_book, actual_qty);
                if (!buy_eff.fully_fillable || !sell_eff.fully_fillable) continue;
                if (buy_eff.avg_price <= 0.0) continue;
            }

            double gross_spread_bps = (sell_eff.avg_price - buy_eff.avg_price) / buy_eff.avg_price * 10000.0;
            double net_spread_bps = gross_spread_bps - fee_rate * 10000.0;

            if (net_spread_bps < min_net_spread_bps_) continue;

            // Build and fire the opportunity
            ArbitrageOpportunity opp;
            opp.pair = pair;
            opp.buy_exchange = buy_book.exchange;
            opp.sell_exchange = sell_book.exchange;
            opp.buy_price = buy_eff.avg_price;
            opp.sell_price = sell_eff.avg_price;
            opp.quantity = actual_qty;
            opp.gross_spread_bps = gross_spread_bps;
            opp.net_spread_bps = net_spread_bps;
            opp.detected_at = std::chrono::steady_clock::now();

            LOG_INFO("ARB DETECTED: {} buy@{} ({}) sell@{} ({}) qty={:.6f} gross={:.1f}bps net={:.1f}bps",
                     pair,
                     buy_eff.avg_price, exchange_to_string(buy_book.exchange),
                     sell_eff.avg_price, exchange_to_string(sell_book.exchange),
                     actual_qty, gross_spread_bps, net_spread_bps);

            if (callback_) {
                callback_(opp);
            }
        }
    }
}
