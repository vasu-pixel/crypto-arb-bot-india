#include "execution/paper_executor.h"
#include "common/crypto_utils.h"
#include "common/logger.h"
#include "common/time_utils.h"
#include "orderbook/depth_analyzer.h"
#include <algorithm>
#include <chrono>
#include <cmath>

PaperExecutor::PaperExecutor(
    const std::map<std::string, double> &initial_balances,
    const std::vector<Exchange> &active_exchanges,
    OrderBookAggregator &aggregator, FeeManager &fee_manager,
    TradeLogger &trade_logger, const PaperRealismConfig &realism)
    : active_exchanges_(active_exchanges), aggregator_(aggregator),
      fee_manager_(fee_manager), trade_logger_(trade_logger),
      rng_(std::random_device{}()), realism_(realism) {
  if (active_exchanges.empty()) {
    LOG_WARN("[PAPER] No active exchanges — virtual balances will be empty");
    return;
  }
  double n = static_cast<double>(active_exchanges.size());
  for (auto &[asset, amount] : initial_balances) {
    double per_exchange = amount / n;
    for (auto exch : active_exchanges) {
      virtual_balances_[exch][asset] = per_exchange;
    }
    LOG_INFO("[PAPER] {} = {:.6f} per exchange ({} exchanges)", asset,
             per_exchange, active_exchanges.size());
  }
}

// ── Utility helpers ──────────────────────────────────────────────────

std::string PaperExecutor::extract_base_asset(const std::string &pair) const {
  auto pos = pair.find('-');
  return (pos != std::string::npos) ? pair.substr(0, pos) : pair;
}

std::string PaperExecutor::extract_quote_asset(const std::string &pair) const {
  auto pos = pair.find('-');
  return (pos != std::string::npos) ? pair.substr(pos + 1) : "USDT";
}

// ── Gap 1: Latency sampling (log-normal, no actual sleep) ───────────

double PaperExecutor::sample_latency_ms(Exchange /*exch*/) {
  if (!realism_.enable_latency)
    return 0.0;
  double mu = realism_.latency_mean_ms;
  double sigma = realism_.latency_stddev_ms;
  if (sigma <= 0.0)
    return mu;
  double variance = sigma * sigma;
  double ln_mu = std::log(mu * mu / std::sqrt(variance + mu * mu));
  double ln_sigma = std::sqrt(std::log(1.0 + variance / (mu * mu)));
  std::lognormal_distribution<double> dist(ln_mu, ln_sigma);
  return dist(rng_);
}

// ── Gap 6: Market impact helpers ────────────────────────────────────

void PaperExecutor::cleanup_phantom_fills() {
  if (!realism_.enable_market_impact)
    return;
  auto now = std::chrono::steady_clock::now();
  double max_age_s = realism_.impact_decay_seconds * 3.0;
  recent_fills_.erase(
      std::remove_if(recent_fills_.begin(), recent_fills_.end(),
                     [&](const PhantomFill &f) {
                       double elapsed =
                           std::chrono::duration<double>(now - f.filled_at)
                               .count();
                       return elapsed > max_age_s;
                     }),
      recent_fills_.end());
}

void PaperExecutor::record_phantom_fill(Exchange exch, const std::string &pair,
                                        Side side, double qty) {
  if (!realism_.enable_market_impact)
    return;
  recent_fills_.push_back(
      {exch, pair, side, qty, std::chrono::steady_clock::now()});
}

OrderBookSnapshot
PaperExecutor::apply_phantom_to_snapshot(const OrderBookSnapshot &snap,
                                         Exchange exch,
                                         const std::string &pair, Side side) {
  if (!realism_.enable_market_impact)
    return snap;

  auto now = std::chrono::steady_clock::now();
  double consumed = 0.0;
  for (const auto &f : recent_fills_) {
    if (f.exchange == exch && f.pair == pair && f.side == side) {
      double elapsed =
          std::chrono::duration<double>(now - f.filled_at).count();
      consumed +=
          f.quantity * std::exp(-elapsed / realism_.impact_decay_seconds);
    }
  }
  if (consumed <= 0.0)
    return snap;

  OrderBookSnapshot adjusted = snap;
  auto &levels = (side == Side::BUY) ? adjusted.asks : adjusted.bids;
  double remaining = consumed;
  for (auto &level : levels) {
    if (remaining <= 0.0)
      break;
    double reduce = std::min(remaining, level.quantity);
    level.quantity -= reduce;
    remaining -= reduce;
  }
  levels.erase(std::remove_if(levels.begin(), levels.end(),
                               [](const PriceLevel &l) {
                                 return l.quantity <= 0.0;
                               }),
               levels.end());
  return adjusted;
}

// ── Gap 8: Rate limit helpers ───────────────────────────────────────

bool PaperExecutor::check_rate_limit(Exchange exch) {
  if (!realism_.enable_rate_limits)
    return true;
  auto now = std::chrono::steady_clock::now();
  auto &state = rate_limit_state_[exch];
  while (!state.order_timestamps.empty() &&
         std::chrono::duration_cast<std::chrono::seconds>(
             now - state.order_timestamps.front())
                 .count() > 60) {
    state.order_timestamps.pop_front();
  }
  int in_last_second = 0;
  for (auto it = state.order_timestamps.rbegin();
       it != state.order_timestamps.rend(); ++it) {
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - *it)
            .count() <= 1000) {
      ++in_last_second;
    } else {
      break;
    }
  }
  if (in_last_second >= realism_.max_orders_per_second)
    return false;
  if (static_cast<int>(state.order_timestamps.size()) >=
      realism_.max_orders_per_minute)
    return false;
  return true;
}

void PaperExecutor::record_rate_limit_hit(Exchange exch) {
  if (!realism_.enable_rate_limits)
    return;
  rate_limit_state_[exch].order_timestamps.push_back(
      std::chrono::steady_clock::now());
}

// ── Gap 4/5: Pending transfer settlement ────────────────────────────

void PaperExecutor::settle_pending_transfers() {
  std::unique_lock lock(mutex_);
  auto now = std::chrono::steady_clock::now();
  auto it = pending_transfers_.begin();
  while (it != pending_transfers_.end()) {
    if (now >= it->available_at) {
      virtual_balances_[it->to_exchange][it->asset] += it->amount;
      LOG_INFO("[PAPER] Transfer settled: {:.6f} {} → {}", it->amount,
               it->asset, exchange_to_string(it->to_exchange));
      it = pending_transfers_.erase(it);
    } else {
      ++it;
    }
  }
}

// ── simulate_fill (Gaps 2, 7 applied inside) ────────────────────────
// CORRECTNESS FIX: No random 50-90% partial fill.
// When book can't fully fill, use actual book depth (quantity_filled).
// Re-walk book with actual filled qty for correct avg price.

OrderResult PaperExecutor::simulate_fill(const OrderRequest &req,
                                         const OrderBookSnapshot &book,
                                         double taker_fee_rate,
                                         double additional_adverse_bps,
                                         double net_spread_bps) {
  OrderResult result;
  result.exchange_order_id = "PAPER-" + CryptoUtils::generate_uuid();

  // Gap 7: Competition filter — probability of being beaten to the fill
  if (realism_.enable_competition) {
    double p_fill =
        1.0 - (1.0 - realism_.competition_base_prob) *
                   std::exp(-net_spread_bps / realism_.competition_decay_bps);
    p_fill = std::clamp(p_fill, 0.0, 1.0);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    if (u(rng_) > p_fill) {
      result.status = OrderStatus::REJECTED;
      result.error_message = "Beaten by competing bot";
      result.filled_quantity = 0.0;
      return result;
    }
  }

  // Walk the order book to determine fill — use actual depth only
  // CORRECTNESS FIX: No random partial fill percentage.
  // If book can't fully fill, fill exactly what's available and re-walk
  // for correct average price at that quantity.
  if (req.side == Side::BUY) {
    auto effective = DepthAnalyzer::effective_buy_price(book, req.quantity);
    if (!effective.fully_fillable) {
      if (effective.quantity_filled <= 0.0) {
        result.status = OrderStatus::REJECTED;
        result.error_message = "No liquidity available";
        return result;
      }
      result.filled_quantity = effective.quantity_filled;
      // Re-walk the book for the actual filled quantity to get correct avg price
      auto actual_eff =
          DepthAnalyzer::effective_buy_price(book, result.filled_quantity);
      result.avg_fill_price = actual_eff.avg_price;
      result.status = OrderStatus::PARTIALLY_FILLED;
    } else {
      result.filled_quantity = req.quantity;
      result.avg_fill_price = effective.avg_price;
      result.status = OrderStatus::FILLED;
    }
  } else {
    auto effective = DepthAnalyzer::effective_sell_price(book, req.quantity);
    if (!effective.fully_fillable) {
      if (effective.quantity_filled <= 0.0) {
        result.status = OrderStatus::REJECTED;
        result.error_message = "No liquidity available";
        return result;
      }
      result.filled_quantity = effective.quantity_filled;
      auto actual_eff =
          DepthAnalyzer::effective_sell_price(book, result.filled_quantity);
      result.avg_fill_price = actual_eff.avg_price;
      result.status = OrderStatus::PARTIALLY_FILLED;
    } else {
      result.filled_quantity = req.quantity;
      result.avg_fill_price = effective.avg_price;
      result.status = OrderStatus::FILLED;
    }
  }

  if (result.avg_fill_price <= 0.0 || result.filled_quantity <= 0.0) {
    result.status = OrderStatus::REJECTED;
    result.error_message = "No liquidity in order book";
    result.filled_quantity = 0.0;
    return result;
  }

  // Gap 2: Adverse slippage beyond VWAP (half-normal, always adverse)
  double total_extra_bps = additional_adverse_bps; // from staleness (Gap 3)
  if (realism_.enable_adverse_slippage) {
    std::normal_distribution<double> slip_dist(realism_.slippage_bps_mean,
                                               realism_.slippage_bps_stddev);
    total_extra_bps += std::abs(slip_dist(rng_));
  }
  if (total_extra_bps > 0.0) {
    double factor = total_extra_bps / 10000.0;
    if (req.side == Side::BUY) {
      result.avg_fill_price *= (1.0 + factor); // Worse for buyer
    } else {
      result.avg_fill_price *= (1.0 - factor); // Worse for seller
    }
  }

  // CORRECTNESS FIX: fee_paid is computed in quote terms here for PnL,
  // but actual balance deductions happen in execute() with correct currency.
  result.fee_paid =
      result.filled_quantity * result.avg_fill_price * taker_fee_rate;
  return result;
}

// ── execute (all gaps orchestrated) ─────────────────────────────────
// CORRECTNESS FIX: Hold unique_lock for the ENTIRE execution to prevent
// TOCTOU races. This mirrors ExecutionEngine which holds execution_mutex_.

TradeRecord PaperExecutor::execute(const ArbitrageOpportunity &opp) {
  // Hold exclusive lock for the ENTIRE execution to prevent TOCTOU races
  std::unique_lock lock(mutex_);

  TradeRecord record;
  record.id = CryptoUtils::generate_uuid();
  record.pair = opp.pair;
  record.buy_exchange = opp.buy_exchange;
  record.sell_exchange = opp.sell_exchange;
  record.buy_price = opp.buy_price;
  record.sell_price = opp.sell_price;
  record.quantity = opp.quantity;
  record.gross_spread_bps = opp.gross_spread_bps;
  record.net_spread_bps = opp.net_spread_bps;
  record.timestamp_iso = TimeUtils::now_iso8601();
  record.mode = TradingMode::PAPER;

  std::string base = extract_base_asset(opp.pair);
  std::string quote = extract_quote_asset(opp.pair);

  // ── Pre-execution profitability guard ──────────────────────────
  {
    double expected_realism_cost_bps = 0.0;
    if (realism_.enable_adverse_slippage) {
      expected_realism_cost_bps += realism_.slippage_bps_mean * 2.0;
    }
    if (realism_.enable_staleness_penalty) {
      expected_realism_cost_bps +=
          realism_.staleness_penalty_bps_per_sec * 0.5 * 2.0;
    }
    if (realism_.enable_one_leg_risk) {
      expected_realism_cost_bps +=
          realism_.one_leg_probability * realism_.one_leg_unwind_slippage_bps;
    }
    if (opp.net_spread_bps < expected_realism_cost_bps) {
      LOG_DEBUG("[PAPER] Spread {:.1f}bps < realism cost {:.1f}bps for {}",
                opp.net_spread_bps, expected_realism_cost_bps, opp.pair);
      record.buy_result.status = OrderStatus::REJECTED;
      record.buy_result.error_message =
          "Spread insufficient after realism costs";
      record.sell_result.status = OrderStatus::REJECTED;
      record.rejection_reason = "realism_cost";
      return record;
    }
  }

  // ── Gap 9: Minimum order size ────────────────────────────────────
  if (realism_.enable_min_order_size) {
    double min_qty = realism_.default_min_notional_usd / opp.buy_price;
    auto it = realism_.min_order_sizes.find(opp.pair);
    if (it != realism_.min_order_sizes.end()) {
      min_qty = std::max(min_qty, it->second);
    }
    if (opp.quantity < min_qty) {
      LOG_WARN("[PAPER] Below minimum order size: {:.8f} < {:.8f} for {}",
               opp.quantity, min_qty, opp.pair);
      record.buy_result.status = OrderStatus::REJECTED;
      record.buy_result.error_message = "Below minimum order size";
      record.sell_result.status = OrderStatus::REJECTED;
      return record;
    }
  }

  // ── Gap 8: Rate limit check ──────────────────────────────────────
  if (realism_.enable_rate_limits) {
    if (!check_rate_limit(opp.buy_exchange) ||
        !check_rate_limit(opp.sell_exchange)) {
      LOG_WARN("[PAPER] Rate limit exceeded");
      record.buy_result.status = OrderStatus::REJECTED;
      record.buy_result.error_message = "Rate limit exceeded";
      record.sell_result.status = OrderStatus::REJECTED;
      return record;
    }
  }

  // ── Early fee lookup (needed for balance check) ─────────────────
  double buy_taker_fee =
      fee_manager_.get_fee(opp.buy_exchange, opp.pair).taker_fee;
  double sell_taker_fee =
      fee_manager_.get_fee(opp.sell_exchange, opp.pair).taker_fee;

  // ── CORRECTNESS FIX: Balance check using depth-walked prices ────
  // Instead of clamping (which hides balance problems), reject if
  // insufficient. Use depth-walked price + fee estimate, not opp.buy_price.
  double trade_qty = opp.quantity;

  // Check buy-side quote balance
  auto buy_exch_it = virtual_balances_.find(opp.buy_exchange);
  if (buy_exch_it == virtual_balances_.end()) {
    record.buy_result.status = OrderStatus::REJECTED;
    record.buy_result.error_message = "Exchange not configured";
    record.sell_result.status = OrderStatus::REJECTED;
    return record;
  }
  auto buy_quote_it = buy_exch_it->second.find(quote);
  double available_quote =
      (buy_quote_it != buy_exch_it->second.end()) ? buy_quote_it->second : 0.0;

  if (available_quote <= 0.0) {
    LOG_WARN("[PAPER] No {} on {}", quote,
             exchange_to_string(opp.buy_exchange));
    record.buy_result.status = OrderStatus::REJECTED;
    record.buy_result.error_message = "Insufficient virtual balance";
    record.sell_result.status = OrderStatus::REJECTED;
    return record;
  }

  // Pre-walk book to get estimated cost
  auto *pre_buy_book = aggregator_.get_book(opp.buy_exchange, opp.pair);
  if (pre_buy_book) {
    auto pre_snap = pre_buy_book->snapshot();
    auto pre_eff = DepthAnalyzer::effective_buy_price(pre_snap, trade_qty);
    double estimated_cost = pre_eff.fully_fillable
                                ? pre_eff.avg_price * trade_qty
                                : opp.buy_price * trade_qty;
    double required_quote = estimated_cost * (1.0 + buy_taker_fee);
    if (available_quote < required_quote) {
      LOG_WARN("[PAPER] Insufficient {} on {}: need {:.4f}, have {:.4f}", quote,
               exchange_to_string(opp.buy_exchange), required_quote,
               available_quote);
      record.buy_result.status = OrderStatus::REJECTED;
      record.buy_result.error_message = "Insufficient virtual balance";
      record.sell_result.status = OrderStatus::REJECTED;
      return record;
    }
  }

  // Check sell-side base balance
  auto sell_exch_it = virtual_balances_.find(opp.sell_exchange);
  if (sell_exch_it == virtual_balances_.end()) {
    record.sell_result.status = OrderStatus::REJECTED;
    record.sell_result.error_message = "Exchange not configured";
    record.buy_result.status = OrderStatus::REJECTED;
    return record;
  }
  auto sell_base_it = sell_exch_it->second.find(base);
  double available_base =
      (sell_base_it != sell_exch_it->second.end()) ? sell_base_it->second : 0.0;

  if (available_base < trade_qty) {
    LOG_WARN("[PAPER] Insufficient {} on {}: need {:.6f}, have {:.6f}", base,
             exchange_to_string(opp.sell_exchange), trade_qty, available_base);
    record.sell_result.status = OrderStatus::REJECTED;
    record.sell_result.error_message = "Insufficient virtual balance";
    record.buy_result.status = OrderStatus::REJECTED;
    return record;
  }

  // ── Get order books ──────────────────────────────────────────────
  auto *buy_book_ptr = aggregator_.get_book(opp.buy_exchange, opp.pair);
  auto *sell_book_ptr = aggregator_.get_book(opp.sell_exchange, opp.pair);
  if (!buy_book_ptr || !sell_book_ptr) {
    record.buy_result.status = OrderStatus::REJECTED;
    record.buy_result.error_message = "No order book available";
    record.sell_result.status = OrderStatus::REJECTED;
    return record;
  }

  auto buy_snap = buy_book_ptr->snapshot();
  auto sell_snap = sell_book_ptr->snapshot();

  if (buy_snap.asks.empty() || sell_snap.bids.empty()) {
    record.buy_result.status = OrderStatus::REJECTED;
    record.buy_result.error_message = "Empty order book";
    record.sell_result.status = OrderStatus::REJECTED;
    return record;
  }

  // ── Gap 3: Staleness penalty ─────────────────────────────────────
  double buy_staleness_bps = 0.0;
  double sell_staleness_bps = 0.0;
  if (realism_.enable_staleness_penalty) {
    auto now = std::chrono::steady_clock::now();
    double buy_age_ms =
        std::chrono::duration<double, std::milli>(now - buy_snap.local_timestamp)
            .count();
    double sell_age_ms = std::chrono::duration<double, std::milli>(
                             now - sell_snap.local_timestamp)
                             .count();

    if (buy_age_ms > realism_.max_book_age_ms ||
        sell_age_ms > realism_.max_book_age_ms) {
      LOG_WARN("[PAPER] Book too stale: buy={:.0f}ms sell={:.0f}ms", buy_age_ms,
               sell_age_ms);
      record.buy_result.status = OrderStatus::REJECTED;
      record.buy_result.error_message = "Order book too stale";
      record.sell_result.status = OrderStatus::REJECTED;
      record.rejection_reason = "staleness";
      return record;
    }
    buy_staleness_bps =
        (buy_age_ms / 1000.0) * realism_.staleness_penalty_bps_per_sec;
    sell_staleness_bps =
        (sell_age_ms / 1000.0) * realism_.staleness_penalty_bps_per_sec;
    record.staleness_penalty_buy_bps = buy_staleness_bps;
    record.staleness_penalty_sell_bps = sell_staleness_bps;
  }

  // ── Gap 1: Latency → re-snapshot (book may have changed) ────────
  if (realism_.enable_latency) {
    double lat_buy = sample_latency_ms(opp.buy_exchange);
    double lat_sell = sample_latency_ms(opp.sell_exchange);
    record.simulated_latency_buy_ms = lat_buy;
    record.simulated_latency_sell_ms = lat_sell;
    // Re-take snapshots to model book movement during latency window
    buy_snap = buy_book_ptr->snapshot();
    sell_snap = sell_book_ptr->snapshot();
  }

  // ── Gap 6: Apply phantom depth consumption ───────────────────────
  cleanup_phantom_fills();
  buy_snap = apply_phantom_to_snapshot(buy_snap, opp.buy_exchange, opp.pair,
                                       Side::BUY);
  sell_snap = apply_phantom_to_snapshot(sell_snap, opp.sell_exchange, opp.pair,
                                        Side::SELL);

  // ── Simulate fills (Gaps 2, 7 applied inside) ────────────────────
  OrderRequest buy_req{opp.buy_exchange, opp.pair,    Side::BUY, opp.buy_price,
                       trade_qty,        CryptoUtils::generate_uuid()};
  OrderRequest sell_req{opp.sell_exchange, opp.pair,     Side::SELL,
                        opp.sell_price,    trade_qty,
                        CryptoUtils::generate_uuid()};

  record.buy_result = simulate_fill(buy_req, buy_snap, buy_taker_fee,
                                    buy_staleness_bps, opp.net_spread_bps);
  record.sell_result = simulate_fill(sell_req, sell_snap, sell_taker_fee,
                                     sell_staleness_bps, opp.net_spread_bps);

  // Populate adverse slippage metadata
  record.adverse_slippage_buy_bps = buy_staleness_bps;
  record.adverse_slippage_sell_bps = sell_staleness_bps;
  if (realism_.enable_adverse_slippage) {
    record.adverse_slippage_buy_bps += realism_.slippage_bps_mean;
    record.adverse_slippage_sell_bps += realism_.slippage_bps_mean;
  }

  // Populate competition fill probability metadata
  if (realism_.enable_competition) {
    double p_fill =
        1.0 -
        (1.0 - realism_.competition_base_prob) *
            std::exp(-opp.net_spread_bps / realism_.competition_decay_bps);
    record.competition_fill_prob = std::clamp(p_fill, 0.0, 1.0);
  } else {
    record.competition_fill_prob = 1.0;
  }

  // Record rate-limit hits for successful orders
  if (record.buy_result.status != OrderStatus::REJECTED)
    record_rate_limit_hit(opp.buy_exchange);
  if (record.sell_result.status != OrderStatus::REJECTED)
    record_rate_limit_hit(opp.sell_exchange);

  // If either leg was rejected by competition/liquidity, bail early
  if (record.buy_result.status == OrderStatus::REJECTED ||
      record.sell_result.status == OrderStatus::REJECTED) {
    record.realized_pnl = 0.0;
    if (record.buy_result.status == OrderStatus::REJECTED) {
      record.rejection_reason = record.buy_result.error_message;
    } else {
      record.rejection_reason = record.sell_result.error_message;
    }
    trade_logger_.log_trade(record);
    return record;
  }

  // ── Gap 10: One-leg risk ─────────────────────────────────────────
  if (realism_.enable_one_leg_risk) {
    std::uniform_real_distribution<double> u(0.0, 1.0);
    if (u(rng_) < realism_.one_leg_probability) {
      record.one_leg_failure = true;
      bool buy_fails = u(rng_) < 0.5;
      double unwind_cost;
      if (buy_fails) {
        double notional = record.sell_result.filled_quantity *
                          record.sell_result.avg_fill_price;
        double slippage =
            notional * realism_.one_leg_unwind_slippage_bps / 10000.0;
        double original_fee = record.sell_result.fee_paid;
        double unwind_fee = notional * sell_taker_fee;
        unwind_cost = slippage + original_fee + unwind_fee;
        record.buy_result.status = OrderStatus::REJECTED;
        record.buy_result.filled_quantity = 0.0;
        record.buy_result.error_message = "One-leg failure (buy side)";
        virtual_balances_[opp.sell_exchange][quote] -= unwind_cost;
      } else {
        double notional = record.buy_result.filled_quantity *
                          record.buy_result.avg_fill_price;
        double slippage =
            notional * realism_.one_leg_unwind_slippage_bps / 10000.0;
        double original_fee = record.buy_result.fee_paid;
        double unwind_fee = notional * buy_taker_fee;
        unwind_cost = slippage + original_fee + unwind_fee;
        record.sell_result.status = OrderStatus::REJECTED;
        record.sell_result.filled_quantity = 0.0;
        record.sell_result.error_message = "One-leg failure (sell side)";
        virtual_balances_[opp.buy_exchange][quote] -= unwind_cost;
      }
      record.realized_pnl = -unwind_cost;
      total_pnl_ += record.realized_pnl;
      LOG_WARN("[PAPER] One-leg failure: {} unwind_cost=${:.4f}", opp.pair,
               unwind_cost);
      trade_logger_.log_trade(record);
      return record;
    }
  }

  // ── Normal matched fill + balance update ─────────────────────────
  double matched_qty = std::min(record.buy_result.filled_quantity,
                                record.sell_result.filled_quantity);

  if (matched_qty <= 0.0) {
    record.buy_result.status = OrderStatus::REJECTED;
    record.sell_result.status = OrderStatus::REJECTED;
    return record;
  }

  double buy_cost = record.buy_result.avg_fill_price * matched_qty;
  double sell_proceeds = record.sell_result.avg_fill_price * matched_qty;

  // CORRECTNESS FIX: Fee currency handling.
  // On real exchanges, buy fees are deducted from the RECEIVED base asset,
  // and sell fees from the RECEIVED quote proceeds.
  double buy_fee_in_base =
      matched_qty * buy_taker_fee; // you receive less base
  double sell_fee_in_quote =
      sell_proceeds * sell_taker_fee; // you receive less quote

  // Update virtual balances atomically (lock already held)
  // Buy exchange: spend quote, receive base minus fee
  virtual_balances_[opp.buy_exchange][quote] -= buy_cost;
  virtual_balances_[opp.buy_exchange][base] +=
      (matched_qty - buy_fee_in_base);

  // Sell exchange: spend base, receive quote minus fee
  virtual_balances_[opp.sell_exchange][base] -= matched_qty;
  virtual_balances_[opp.sell_exchange][quote] +=
      (sell_proceeds - sell_fee_in_quote);

  // PnL: what we gained in quote minus what we spent, accounting for
  // the base-denominated buy fee converted to quote terms
  double buy_fee_in_quote =
      buy_fee_in_base * record.buy_result.avg_fill_price;
  record.buy_result.fee_paid = buy_fee_in_quote;
  record.sell_result.fee_paid = sell_fee_in_quote;
  record.realized_pnl =
      sell_proceeds - buy_cost - buy_fee_in_quote - sell_fee_in_quote;

  total_pnl_ += record.realized_pnl;

  // ── Gap 6: Record phantom fills for impact decay ─────────────────
  record_phantom_fill(opp.buy_exchange, opp.pair, Side::BUY, matched_qty);
  record_phantom_fill(opp.sell_exchange, opp.pair, Side::SELL, matched_qty);

  trade_logger_.log_trade(record);

  LOG_INFO(
      "[PAPER] Trade: {} buy@{:.2f}({}) sell@{:.2f}({}) qty={:.6f} pnl=${:.4f}",
      opp.pair, record.buy_result.avg_fill_price,
      exchange_to_string(opp.buy_exchange), record.sell_result.avg_fill_price,
      exchange_to_string(opp.sell_exchange), matched_qty, record.realized_pnl);

  return record;
}

// ── Rebalance (Gap 4: delayed transfers, Gap 5: withdrawal fees) ────

void PaperExecutor::rebalance() {
  std::unique_lock lock(mutex_);
  if (virtual_balances_.empty())
    return;

  size_t num_exchanges = virtual_balances_.size();
  if (num_exchanges < 2)
    return;

  std::unordered_map<std::string, double> totals;
  for (auto &[exch, assets] : virtual_balances_) {
    for (auto &[asset, amount] : assets) {
      totals[asset] += amount;
    }
  }

  if (!realism_.enable_realistic_rebalance) {
    // Instant rebalance (original behavior)
    for (auto &[asset, total] : totals) {
      double per_exchange = total / static_cast<double>(num_exchanges);
      for (auto &[exch, assets] : virtual_balances_) {
        assets[asset] = per_exchange;
      }
    }
    LOG_INFO("[PAPER] Instant rebalance across {} exchanges", num_exchanges);
    return;
  }

  // Realistic rebalance: create delayed transfers with fees
  auto delay = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(realism_.rebalance_delay_minutes * 60.0));
  auto available_at = std::chrono::steady_clock::now() + delay;

  for (auto &[asset, total] : totals) {
    double target = total / static_cast<double>(num_exchanges);

    for (auto &[exch, assets] : virtual_balances_) {
      double current = assets[asset];
      double excess = current - target;
      if (excess <= 0.0)
        continue;

      // Deduct excess from source immediately
      assets[asset] = target;

      // Compute withdrawal fees (Gap 5)
      double amount_to_transfer = excess;
      if (realism_.enable_withdrawal_fees) {
        double flat_fee = 0.0;
        auto fee_it = realism_.withdrawal_flat_fees.find(asset);
        if (fee_it != realism_.withdrawal_flat_fees.end()) {
          flat_fee = fee_it->second;
        }
        double pct_fee =
            amount_to_transfer * realism_.withdrawal_fee_pct / 100.0;
        amount_to_transfer -= (flat_fee + pct_fee);
        if (amount_to_transfer <= 0.0) {
          LOG_WARN("[PAPER] Withdrawal fees exceed transfer amount for {}",
                   asset);
          continue;
        }
      }

      // Distribute net amount to deficit exchanges via pending transfers
      for (auto &[dest_exch, dest_assets] : virtual_balances_) {
        double deficit = target - dest_assets[asset];
        if (deficit <= 0.0 || dest_exch == exch)
          continue;

        double transfer_share = std::min(deficit, amount_to_transfer);
        pending_transfers_.push_back(
            {dest_exch, asset, transfer_share, available_at});
        amount_to_transfer -= transfer_share;
        if (amount_to_transfer <= 0.0)
          break;
      }
    }
  }

  LOG_INFO("[PAPER] Rebalance initiated: {} pending transfers (delay={:.0f}min)",
           pending_transfers_.size(), realism_.rebalance_delay_minutes);
}

// ── Getters ─────────────────────────────────────────────────────────

std::map<Exchange, std::unordered_map<std::string, double>>
PaperExecutor::get_virtual_balances() const {
  std::shared_lock lock(mutex_);
  return virtual_balances_;
}

double PaperExecutor::get_virtual_pnl() const {
  std::shared_lock lock(mutex_);
  return total_pnl_;
}
