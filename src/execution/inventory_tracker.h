#pragma once

#include "common/types.h"
#include "exchange/exchange_interface.h"

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <shared_mutex>
#include <thread>
#include <atomic>
#include <chrono>

struct InventoryState {
    std::unordered_map<std::string, double> balances; // asset -> amount
    double total_usd_value = 0.0;
};

class InventoryTracker {
public:
    InventoryTracker(std::unordered_map<Exchange, IExchange*> exchanges,
                     double drift_threshold_pct = 20.0);
    ~InventoryTracker();

    void refresh_balances();
    InventoryState get_state(Exchange exch) const;
    std::map<Exchange, InventoryState> get_all_states() const;
    std::vector<DriftAlert> check_drift() const;
    void start_monitoring(std::chrono::seconds interval = std::chrono::seconds(30));
    void stop();
    void record_fill(Exchange exch, const std::string& asset, double delta);

private:
    std::unordered_map<Exchange, IExchange*> exchanges_;
    std::unordered_map<Exchange, InventoryState> states_;
    mutable std::shared_mutex mutex_;
    double drift_threshold_pct_;
    std::thread monitor_thread_;
    std::atomic<bool> running_{false};
};
