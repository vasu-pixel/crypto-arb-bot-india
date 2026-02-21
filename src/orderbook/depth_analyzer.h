#pragma once

#include "common/types.h"

#include <vector>

struct EffectivePrice {
    double avg_price = 0.0;
    double total_cost = 0.0;
    double quantity_filled = 0.0;
    double slippage_bps = 0.0;
    bool fully_fillable = false;
};

class DepthAnalyzer {
public:
    // Walk the ask side of the book to compute volume-weighted average buy price.
    static EffectivePrice effective_buy_price(const OrderBookSnapshot& book, double quantity);

    // Walk the bid side of the book to compute volume-weighted average sell price.
    static EffectivePrice effective_sell_price(const OrderBookSnapshot& book, double quantity);

    // Find the maximum quantity that can be arbitraged between buy_book (asks)
    // and sell_book (bids) while maintaining at least min_net_spread_bps after fees.
    static double max_arb_quantity(const OrderBookSnapshot& buy_book,
                                   const OrderBookSnapshot& sell_book,
                                   double min_net_spread_bps,
                                   double total_fee_rate);
};
