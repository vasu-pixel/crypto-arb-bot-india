#pragma once

#include "common/logger.h"
#include "common/types.h"
#include "orderbook/depth_analyzer.h"
#include "orderbook/order_book_aggregator.h"
#include "strategy/fee_manager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

template <typename OpportunityCallback> class SpreadDetector {
public:
  SpreadDetector(OrderBookAggregator &aggregator, FeeManager &fee_manager,
                 double min_net_spread_bps, double min_trade_size_usd,
                 double max_trade_size_usd, OpportunityCallback callback)
      : aggregator_(aggregator), fee_manager_(fee_manager),
        min_net_spread_bps_(min_net_spread_bps),
        min_trade_size_usd_(min_trade_size_usd),
        max_trade_size_usd_(max_trade_size_usd),
        callback_(std::move(callback)) {}

  void scan_pair(const std::string &pair) {
    auto snapshots = aggregator_.get_pair_snapshots(pair, 20);
    if (snapshots.size() < 2)
      return;

    for (size_t i = 0; i < snapshots.size(); ++i) {
      for (size_t j = 0; j < snapshots.size(); ++j) {
        if (i == j)
          continue;

        const auto &buy_book = snapshots[i];
        const auto &sell_book = snapshots[j];

        if (buy_book.asks.empty() || sell_book.bids.empty())
          continue;

        auto now = std::chrono::steady_clock::now();
        auto buy_age = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - buy_book.local_timestamp);
        auto sell_age = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - sell_book.local_timestamp);
        if (buy_age.count() > 5000 || sell_age.count() > 5000)
          continue;

        double top_ask = buy_book.asks.front().price;
        double top_bid = sell_book.bids.front().price;
        if (top_ask <= 0.0 || top_bid <= top_ask)
          continue;

        double fee_rate = fee_manager_.total_fee_rate(buy_book.exchange,
                                                      sell_book.exchange, pair);

        double quick_gross_bps = (top_bid - top_ask) / top_ask * 10000.0;
        double quick_net_bps = quick_gross_bps - fee_rate * 10000.0;
        if (quick_net_bps < min_net_spread_bps_)
          continue;

        double max_qty = DepthAnalyzer::max_arb_quantity(
            buy_book, sell_book, min_net_spread_bps_, fee_rate);

        if (max_qty <= 0.0)
          continue;

        auto buy_eff = DepthAnalyzer::effective_buy_price(buy_book, max_qty);
        auto sell_eff = DepthAnalyzer::effective_sell_price(sell_book, max_qty);

        if (!buy_eff.fully_fillable || !sell_eff.fully_fillable)
          continue;
        if (buy_eff.avg_price <= 0.0)
          continue;

        double trade_size_usd = buy_eff.avg_price * max_qty;

        if (trade_size_usd < min_trade_size_usd_)
          continue;

        double actual_qty = max_qty;
        if (trade_size_usd > max_trade_size_usd_) {
          actual_qty = max_trade_size_usd_ / buy_eff.avg_price;
          buy_eff = DepthAnalyzer::effective_buy_price(buy_book, actual_qty);
          sell_eff = DepthAnalyzer::effective_sell_price(sell_book, actual_qty);
          if (!buy_eff.fully_fillable || !sell_eff.fully_fillable)
            continue;
          if (buy_eff.avg_price <= 0.0)
            continue;
        }

        double gross_spread_bps = (sell_eff.avg_price - buy_eff.avg_price) /
                                  buy_eff.avg_price * 10000.0;
        double net_spread_bps = gross_spread_bps - fee_rate * 10000.0;

        if (net_spread_bps < min_net_spread_bps_)
          continue;

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

        // Optimized Log formatting here to cover task 8
        if (Logger::get()->level() <= spdlog::level::info) {
          LOG_INFO("ARB DETECTED: {} buy@{} ({}) sell@{} ({}) qty={:.6f} "
                   "gross={:.1f}bps net={:.1f}bps",
                   pair, buy_eff.avg_price,
                   exchange_to_string(buy_book.exchange), sell_eff.avg_price,
                   exchange_to_string(sell_book.exchange), actual_qty,
                   gross_spread_bps, net_spread_bps);
        }

        callback_(opp);
      }
    }
  }

private:
  OrderBookAggregator &aggregator_;
  FeeManager &fee_manager_;
  double min_net_spread_bps_;
  double min_trade_size_usd_;
  double max_trade_size_usd_;

  OpportunityCallback callback_;
};
