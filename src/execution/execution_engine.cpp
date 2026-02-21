#include "execution/execution_engine.h"
#include "common/logger.h"
#include "common/crypto_utils.h"
#include "common/time_utils.h"
#include <future>
#include <thread>

ExecutionEngine::ExecutionEngine(
    std::unordered_map<Exchange, IExchange*> exchanges,
    OrderManager& order_manager,
    InventoryTracker& inventory_tracker,
    TradeLogger& trade_logger)
    : exchanges_(std::move(exchanges))
    , order_manager_(order_manager)
    , inventory_tracker_(inventory_tracker)
    , trade_logger_(trade_logger) {}

bool ExecutionEngine::validate_opportunity(const ArbitrageOpportunity& opp) const {
    if (opp.net_spread_bps <= 0) return false;
    if (opp.quantity <= 0) return false;
    if (exchanges_.find(opp.buy_exchange) == exchanges_.end()) return false;
    if (exchanges_.find(opp.sell_exchange) == exchanges_.end()) return false;
    return true;
}

bool ExecutionEngine::check_balances(const ArbitrageOpportunity& opp) const {
    // Check if we have enough quote currency on buy exchange
    // and enough base currency on sell exchange
    auto buy_state = inventory_tracker_.get_state(opp.buy_exchange);
    auto sell_state = inventory_tracker_.get_state(opp.sell_exchange);

    // Extract base/quote from pair (e.g., "BTC-USD" -> "BTC", "USD")
    auto dash_pos = opp.pair.find('-');
    if (dash_pos == std::string::npos) return false;
    std::string base = opp.pair.substr(0, dash_pos);
    std::string quote = opp.pair.substr(dash_pos + 1);

    double required_quote = opp.buy_price * opp.quantity;
    double available_quote = buy_state.balances.count(quote) ? buy_state.balances.at(quote) : 0.0;
    if (available_quote < required_quote) {
        LOG_WARN("Insufficient {} on {}: need {}, have {}",
                 quote, exchange_to_string(opp.buy_exchange), required_quote, available_quote);
        return false;
    }

    double available_base = sell_state.balances.count(base) ? sell_state.balances.at(base) : 0.0;
    if (available_base < opp.quantity) {
        LOG_WARN("Insufficient {} on {}: need {}, have {}",
                 base, exchange_to_string(opp.sell_exchange), opp.quantity, available_base);
        return false;
    }
    return true;
}

ExecutionEngine::DualOrderResult ExecutionEngine::fire_orders(const ArbitrageOpportunity& opp) {
    std::string buy_cid = CryptoUtils::generate_uuid();
    std::string sell_cid = CryptoUtils::generate_uuid();

    OrderRequest buy_req{opp.buy_exchange, opp.pair, Side::BUY,
                         opp.buy_price, opp.quantity, buy_cid};
    OrderRequest sell_req{opp.sell_exchange, opp.pair, Side::SELL,
                          opp.sell_price, opp.quantity, sell_cid};

    order_manager_.track_order(buy_cid, buy_req);
    order_manager_.track_order(sell_cid, sell_req);

    // Fire both orders simultaneously
    auto buy_future = std::async(std::launch::async, [&]() {
        return exchanges_[opp.buy_exchange]->place_limit_order(buy_req);
    });
    auto sell_future = std::async(std::launch::async, [&]() {
        return exchanges_[opp.sell_exchange]->place_limit_order(sell_req);
    });

    DualOrderResult result;
    result.buy = buy_future.get();
    result.sell = sell_future.get();

    order_manager_.update_order(buy_cid, result.buy);
    order_manager_.update_order(sell_cid, result.sell);

    return result;
}

void ExecutionEngine::handle_partial_fill(const ArbitrageOpportunity& opp,
                                           const DualOrderResult& results) {
    // If one side rejected, cancel the other
    if (results.buy.status == OrderStatus::REJECTED && results.sell.status != OrderStatus::REJECTED) {
        LOG_WARN("Buy rejected, cancelling sell for {}", opp.pair);
        exchanges_[opp.sell_exchange]->cancel_order(opp.pair, results.sell.exchange_order_id);
    }
    if (results.sell.status == OrderStatus::REJECTED && results.buy.status != OrderStatus::REJECTED) {
        LOG_WARN("Sell rejected, cancelling buy for {}", opp.pair);
        exchanges_[opp.buy_exchange]->cancel_order(opp.pair, results.buy.exchange_order_id);
    }

    // If one side partially filled, wait 2s then cancel remainder
    auto wait_and_cancel = [&](Exchange exch, const std::string& order_id,
                               const std::string& side_name) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        auto status = exchanges_[exch]->query_order(opp.pair, order_id);
        if (status.status != OrderStatus::FILLED) {
            LOG_WARN("Cancelling unfilled {} order {} for {}", side_name, order_id, opp.pair);
            exchanges_[exch]->cancel_order(opp.pair, order_id);
        }
    };

    if (results.buy.status == OrderStatus::PARTIALLY_FILLED) {
        wait_and_cancel(opp.buy_exchange, results.buy.exchange_order_id, "buy");
    }
    if (results.sell.status == OrderStatus::PARTIALLY_FILLED) {
        wait_and_cancel(opp.sell_exchange, results.sell.exchange_order_id, "sell");
    }
}

TradeRecord ExecutionEngine::execute(const ArbitrageOpportunity& opp) {
    std::lock_guard<std::mutex> lock(execution_mutex_);

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
    record.mode = TradingMode::LIVE;

    if (!validate_opportunity(opp)) {
        LOG_WARN("Invalid opportunity for {}", opp.pair);
        record.buy_result.status = OrderStatus::REJECTED;
        record.buy_result.error_message = "Invalid opportunity";
        record.sell_result.status = OrderStatus::REJECTED;
        record.sell_result.error_message = "Invalid opportunity";
        return record;
    }

    LOG_INFO("Executing arb: {} buy@{} ({}) sell@{} ({}) qty={} net_spread={}bps",
             opp.pair, opp.buy_price, exchange_to_string(opp.buy_exchange),
             opp.sell_price, exchange_to_string(opp.sell_exchange),
             opp.quantity, opp.net_spread_bps);

    auto results = fire_orders(opp);
    record.buy_result = results.buy;
    record.sell_result = results.sell;

    // Handle partial fills
    bool buy_ok = (results.buy.status == OrderStatus::FILLED);
    bool sell_ok = (results.sell.status == OrderStatus::FILLED);
    if (!buy_ok || !sell_ok) {
        handle_partial_fill(opp, results);
    }

    // Calculate realized P&L using actual fill prices
    double matched_qty = std::min(results.buy.filled_quantity, results.sell.filled_quantity);
    double buy_cost = results.buy.avg_fill_price * matched_qty;
    double sell_proceeds = results.sell.avg_fill_price * matched_qty;
    record.realized_pnl = sell_proceeds - buy_cost - results.buy.fee_paid - results.sell.fee_paid;

    // Update inventory
    auto dash_pos = opp.pair.find('-');
    if (dash_pos != std::string::npos) {
        std::string base = opp.pair.substr(0, dash_pos);
        std::string quote = opp.pair.substr(dash_pos + 1);
        inventory_tracker_.record_fill(opp.buy_exchange, base, results.buy.filled_quantity);
        inventory_tracker_.record_fill(opp.buy_exchange, quote, -buy_cost);
        inventory_tracker_.record_fill(opp.sell_exchange, base, -results.sell.filled_quantity);
        inventory_tracker_.record_fill(opp.sell_exchange, quote, sell_proceeds);
    }

    trade_logger_.log_trade(record);

    LOG_INFO("Trade completed: {} pnl=${:.4f} buy_status={} sell_status={}",
             opp.pair, record.realized_pnl,
             order_status_to_string(results.buy.status),
             order_status_to_string(results.sell.status));

    return record;
}
