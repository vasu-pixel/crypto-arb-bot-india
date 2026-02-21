#include "orderbook/depth_analyzer.h"

#include <algorithm>
#include <cmath>
#include <limits>

EffectivePrice DepthAnalyzer::effective_buy_price(const OrderBookSnapshot& book, double quantity)
{
    EffectivePrice result;

    if (book.asks.empty() || quantity <= 0.0) {
        return result;
    }

    double remaining = quantity;
    double total_cost = 0.0;

    // Asks are sorted ascending by price (cheapest first)
    for (const auto& level : book.asks) {
        if (remaining <= 0.0) break;

        double fill = std::min(remaining, level.quantity);
        total_cost += fill * level.price;
        remaining -= fill;
    }

    result.quantity_filled = quantity - remaining;
    result.total_cost = total_cost;
    result.fully_fillable = (remaining <= 0.0);

    if (result.quantity_filled > 0.0) {
        result.avg_price = total_cost / result.quantity_filled;

        // Slippage in bps relative to the top-of-book ask
        double top_ask = book.asks.front().price;
        if (top_ask > 0.0) {
            result.slippage_bps = (result.avg_price - top_ask) / top_ask * 10000.0;
        }
    }

    return result;
}

EffectivePrice DepthAnalyzer::effective_sell_price(const OrderBookSnapshot& book, double quantity)
{
    EffectivePrice result;

    if (book.bids.empty() || quantity <= 0.0) {
        return result;
    }

    double remaining = quantity;
    double total_proceeds = 0.0;

    // Bids are sorted descending by price (highest first)
    for (const auto& level : book.bids) {
        if (remaining <= 0.0) break;

        double fill = std::min(remaining, level.quantity);
        total_proceeds += fill * level.price;
        remaining -= fill;
    }

    result.quantity_filled = quantity - remaining;
    result.total_cost = total_proceeds; // for sells, "cost" is proceeds received
    result.fully_fillable = (remaining <= 0.0);

    if (result.quantity_filled > 0.0) {
        result.avg_price = total_proceeds / result.quantity_filled;

        // Slippage in bps relative to the top-of-book bid (negative = worse for seller)
        double top_bid = book.bids.front().price;
        if (top_bid > 0.0) {
            result.slippage_bps = (top_bid - result.avg_price) / top_bid * 10000.0;
        }
    }

    return result;
}

double DepthAnalyzer::max_arb_quantity(const OrderBookSnapshot& buy_book,
                                       const OrderBookSnapshot& sell_book,
                                       double min_net_spread_bps,
                                       double total_fee_rate)
{
    if (buy_book.asks.empty() || sell_book.bids.empty()) {
        return 0.0;
    }

    // Quick check: if top-of-book spread doesn't clear fees + min spread, no arb exists
    double top_ask = buy_book.asks.front().price;
    double top_bid = sell_book.bids.front().price;
    if (top_ask <= 0.0) return 0.0;

    double gross_bps = (top_bid - top_ask) / top_ask * 10000.0;
    double net_bps = gross_bps - total_fee_rate * 10000.0;
    if (net_bps < min_net_spread_bps) {
        return 0.0;
    }

    // Walk both books simultaneously, increasing quantity until the marginal
    // spread no longer exceeds the minimum. We use a binary-search approach on
    // quantity, bounded by the lesser total depth of the two sides.

    // Compute total available quantity on each side
    double total_ask_qty = 0.0;
    for (const auto& level : buy_book.asks) {
        total_ask_qty += level.quantity;
    }
    double total_bid_qty = 0.0;
    for (const auto& level : sell_book.bids) {
        total_bid_qty += level.quantity;
    }

    double max_qty = std::min(total_ask_qty, total_bid_qty);
    if (max_qty <= 0.0) return 0.0;

    // Binary search for the largest quantity where net spread >= min_net_spread_bps
    double lo = 0.0;
    double hi = max_qty;
    double best_qty = 0.0;

    static constexpr int MAX_ITERATIONS = 50;
    static constexpr double EPSILON = 1e-8;

    for (int i = 0; i < MAX_ITERATIONS; ++i) {
        double mid = (lo + hi) / 2.0;
        if (mid < EPSILON) break;

        EffectivePrice buy_eff = effective_buy_price(buy_book, mid);
        EffectivePrice sell_eff = effective_sell_price(sell_book, mid);

        if (!buy_eff.fully_fillable || !sell_eff.fully_fillable) {
            hi = mid;
            continue;
        }

        if (buy_eff.avg_price <= 0.0) {
            hi = mid;
            continue;
        }

        double current_gross_bps = (sell_eff.avg_price - buy_eff.avg_price) / buy_eff.avg_price * 10000.0;
        double current_net_bps = current_gross_bps - total_fee_rate * 10000.0;

        if (current_net_bps >= min_net_spread_bps) {
            best_qty = mid;
            lo = mid;
        } else {
            hi = mid;
        }

        if ((hi - lo) < EPSILON) break;
    }

    return best_qty;
}
