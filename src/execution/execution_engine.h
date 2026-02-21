#pragma once
#include "exchange/exchange_interface.h"
#include "execution/order_manager.h"
#include "execution/inventory_tracker.h"
#include "persistence/trade_logger.h"
#include "common/types.h"
#include <unordered_map>
#include <mutex>
#include <future>

class ExecutionEngine {
public:
    ExecutionEngine(std::unordered_map<Exchange, IExchange*> exchanges,
                    OrderManager& order_manager,
                    InventoryTracker& inventory_tracker,
                    TradeLogger& trade_logger);

    TradeRecord execute(const ArbitrageOpportunity& opp);

private:
    struct DualOrderResult {
        OrderResult buy;
        OrderResult sell;
    };

    DualOrderResult fire_orders(const ArbitrageOpportunity& opp);
    void handle_partial_fill(const ArbitrageOpportunity& opp, const DualOrderResult& results);
    bool validate_opportunity(const ArbitrageOpportunity& opp) const;
    bool check_balances(const ArbitrageOpportunity& opp) const;

    std::unordered_map<Exchange, IExchange*> exchanges_;
    OrderManager& order_manager_;
    InventoryTracker& inventory_tracker_;
    TradeLogger& trade_logger_;
    std::mutex execution_mutex_;
};
