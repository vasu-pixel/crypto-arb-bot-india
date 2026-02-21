#include "execution/paper_executor.h"
#include "orderbook/depth_analyzer.h"
#include "common/logger.h"
#include "common/crypto_utils.h"
#include "common/time_utils.h"
#include <thread>
#include <chrono>

PaperExecutor::PaperExecutor(
    const std::map<std::string, double>& initial_balances,
    OrderBookAggregator& aggregator,
    FeeManager& fee_manager,
    TradeLogger& trade_logger)
    : aggregator_(aggregator)
    , fee_manager_(fee_manager)
    , trade_logger_(trade_logger)
    , rng_(std::random_device{}()) {
    // Distribute initial balances across all exchanges equally
    for (auto& [asset, amount] : initial_balances) {
        double per_exchange = amount / 3.0;
        virtual_balances_[Exchange::BINANCE_US][asset] = per_exchange;
        virtual_balances_[Exchange::KRAKEN][asset] = per_exchange;
        virtual_balances_[Exchange::COINBASE][asset] = per_exchange;
    }
}

std::string PaperExecutor::extract_base_asset(const std::string& pair) const {
    auto pos = pair.find('-');
    return (pos != std::string::npos) ? pair.substr(0, pos) : pair;
}

std::string PaperExecutor::extract_quote_asset(const std::string& pair) const {
    auto pos = pair.find('-');
    return (pos != std::string::npos) ? pair.substr(pos + 1) : "USD";
}

bool PaperExecutor::check_virtual_balance(Exchange exch, const std::string& asset,
                                            double amount) const {
    std::shared_lock lock(mutex_);
    auto exch_it = virtual_balances_.find(exch);
    if (exch_it == virtual_balances_.end()) return false;
    auto asset_it = exch_it->second.find(asset);
    if (asset_it == exch_it->second.end()) return false;
    return asset_it->second >= amount;
}

void PaperExecutor::update_virtual_balance(Exchange exch, const std::string& asset, double delta) {
    std::unique_lock lock(mutex_);
    virtual_balances_[exch][asset] += delta;
}

OrderResult PaperExecutor::simulate_fill(const OrderRequest& req,
                                           const OrderBookSnapshot& book,
                                           double taker_fee_rate) {
    OrderResult result;
    result.exchange_order_id = "PAPER-" + CryptoUtils::generate_uuid();

    // Add simulated latency jitter (5-50ms)
    std::uniform_int_distribution<int> latency_dist(5, 50);
    std::this_thread::sleep_for(std::chrono::milliseconds(latency_dist(rng_)));

    // Walk the order book to determine fill
    if (req.side == Side::BUY) {
        auto effective = DepthAnalyzer::effective_buy_price(book, req.quantity);
        if (!effective.fully_fillable) {
            // Partial fill: fill what's available
            std::uniform_real_distribution<double> partial_dist(0.5, 0.9);
            double fill_pct = partial_dist(rng_);
            result.filled_quantity = effective.quantity_filled * fill_pct;
            result.avg_fill_price = effective.avg_price;
            result.status = OrderStatus::PARTIALLY_FILLED;
        } else {
            result.filled_quantity = req.quantity;
            result.avg_fill_price = effective.avg_price;
            result.status = OrderStatus::FILLED;
        }
    } else {
        auto effective = DepthAnalyzer::effective_sell_price(book, req.quantity);
        if (!effective.fully_fillable) {
            std::uniform_real_distribution<double> partial_dist(0.5, 0.9);
            double fill_pct = partial_dist(rng_);
            result.filled_quantity = effective.quantity_filled * fill_pct;
            result.avg_fill_price = effective.avg_price;
            result.status = OrderStatus::PARTIALLY_FILLED;
        } else {
            result.filled_quantity = req.quantity;
            result.avg_fill_price = effective.avg_price;
            result.status = OrderStatus::FILLED;
        }
    }

    // Apply per-exchange per-pair taker fee
    result.fee_paid = result.filled_quantity * result.avg_fill_price * taker_fee_rate;
    return result;
}

TradeRecord PaperExecutor::execute(const ArbitrageOpportunity& opp) {
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

    // Check virtual balances
    double required_quote = opp.buy_price * opp.quantity;
    if (!check_virtual_balance(opp.buy_exchange, quote, required_quote)) {
        LOG_WARN("[PAPER] Insufficient {} on {}", quote, exchange_to_string(opp.buy_exchange));
        record.buy_result.status = OrderStatus::REJECTED;
        record.buy_result.error_message = "Insufficient virtual balance";
        record.sell_result.status = OrderStatus::REJECTED;
        return record;
    }
    if (!check_virtual_balance(opp.sell_exchange, base, opp.quantity)) {
        LOG_WARN("[PAPER] Insufficient {} on {}", base, exchange_to_string(opp.sell_exchange));
        record.sell_result.status = OrderStatus::REJECTED;
        record.sell_result.error_message = "Insufficient virtual balance";
        record.buy_result.status = OrderStatus::REJECTED;
        return record;
    }

    // Get current order books
    auto* buy_book_ptr = aggregator_.get_book(opp.buy_exchange, opp.pair);
    auto* sell_book_ptr = aggregator_.get_book(opp.sell_exchange, opp.pair);
    if (!buy_book_ptr || !sell_book_ptr) {
        record.buy_result.status = OrderStatus::REJECTED;
        record.buy_result.error_message = "No order book available";
        record.sell_result.status = OrderStatus::REJECTED;
        return record;
    }

    auto buy_snap = buy_book_ptr->snapshot();
    auto sell_snap = sell_book_ptr->snapshot();

    // Get per-exchange per-pair taker fees
    double buy_taker_fee = fee_manager_.get_fee(opp.buy_exchange, opp.pair).taker_fee;
    double sell_taker_fee = fee_manager_.get_fee(opp.sell_exchange, opp.pair).taker_fee;

    // Simulate fills
    OrderRequest buy_req{opp.buy_exchange, opp.pair, Side::BUY,
                         opp.buy_price, opp.quantity, CryptoUtils::generate_uuid()};
    OrderRequest sell_req{opp.sell_exchange, opp.pair, Side::SELL,
                          opp.sell_price, opp.quantity, CryptoUtils::generate_uuid()};

    record.buy_result = simulate_fill(buy_req, buy_snap, buy_taker_fee);
    record.sell_result = simulate_fill(sell_req, sell_snap, sell_taker_fee);

    // Update virtual balances
    double matched_qty = std::min(record.buy_result.filled_quantity,
                                   record.sell_result.filled_quantity);
    double buy_cost = record.buy_result.avg_fill_price * matched_qty;
    double sell_proceeds = record.sell_result.avg_fill_price * matched_qty;

    update_virtual_balance(opp.buy_exchange, base, matched_qty);
    update_virtual_balance(opp.buy_exchange, quote, -buy_cost - record.buy_result.fee_paid);
    update_virtual_balance(opp.sell_exchange, base, -matched_qty);
    update_virtual_balance(opp.sell_exchange, quote, sell_proceeds - record.sell_result.fee_paid);

    record.realized_pnl = sell_proceeds - buy_cost - record.buy_result.fee_paid - record.sell_result.fee_paid;
    total_pnl_ += record.realized_pnl;

    trade_logger_.log_trade(record);

    LOG_INFO("[PAPER] Trade: {} buy@{:.2f}({}) sell@{:.2f}({}) qty={:.6f} pnl=${:.4f}",
             opp.pair, record.buy_result.avg_fill_price, exchange_to_string(opp.buy_exchange),
             record.sell_result.avg_fill_price, exchange_to_string(opp.sell_exchange),
             matched_qty, record.realized_pnl);

    return record;
}

std::map<Exchange, std::unordered_map<std::string, double>> PaperExecutor::get_virtual_balances() const {
    std::shared_lock lock(mutex_);
    return virtual_balances_;
}

double PaperExecutor::get_virtual_pnl() const {
    std::shared_lock lock(mutex_);
    return total_pnl_;
}
