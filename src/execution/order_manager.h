#pragma once

#include "common/types.h"

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <chrono>
#include <shared_mutex>

struct ManagedOrder {
    OrderRequest request;
    OrderResult result;
    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point updated_at;
};

class OrderManager {
public:
    OrderManager() = default;

    void track_order(const std::string& client_id, const OrderRequest& req);
    void update_order(const std::string& client_id, const OrderResult& result);
    std::optional<ManagedOrder> get_order(const std::string& client_id) const;
    std::vector<ManagedOrder> get_open_orders() const;
    int open_order_count() const;
    void cleanup_old_orders(std::chrono::seconds max_age = std::chrono::seconds(3600));

private:
    std::unordered_map<std::string, ManagedOrder> orders_;
    mutable std::shared_mutex mutex_;
};
