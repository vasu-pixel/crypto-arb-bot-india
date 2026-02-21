#include "execution/order_manager.h"
#include "common/logger.h"

void OrderManager::track_order(const std::string& client_id, const OrderRequest& req) {
    std::unique_lock lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    ManagedOrder managed;
    managed.request = req;
    managed.result.status = OrderStatus::PENDING;
    managed.created_at = now;
    managed.updated_at = now;
    orders_[client_id] = std::move(managed);
    LOG_DEBUG("OrderManager: tracking order client_id={} pair={} side={} qty={}",
              client_id, req.pair, side_to_string(req.side), req.quantity);
}

void OrderManager::update_order(const std::string& client_id, const OrderResult& result) {
    std::unique_lock lock(mutex_);
    auto it = orders_.find(client_id);
    if (it == orders_.end()) {
        LOG_WARN("OrderManager: update_order called for unknown client_id={}", client_id);
        return;
    }
    it->second.result = result;
    it->second.updated_at = std::chrono::steady_clock::now();
    LOG_DEBUG("OrderManager: updated order client_id={} status={} filled_qty={}",
              client_id, order_status_to_string(result.status), result.filled_quantity);
}

std::optional<ManagedOrder> OrderManager::get_order(const std::string& client_id) const {
    std::shared_lock lock(mutex_);
    auto it = orders_.find(client_id);
    if (it == orders_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<ManagedOrder> OrderManager::get_open_orders() const {
    std::shared_lock lock(mutex_);
    std::vector<ManagedOrder> open;
    for (const auto& [id, order] : orders_) {
        if (order.result.status == OrderStatus::PENDING ||
            order.result.status == OrderStatus::OPEN ||
            order.result.status == OrderStatus::PARTIALLY_FILLED) {
            open.push_back(order);
        }
    }
    return open;
}

int OrderManager::open_order_count() const {
    std::shared_lock lock(mutex_);
    int count = 0;
    for (const auto& [id, order] : orders_) {
        if (order.result.status == OrderStatus::PENDING ||
            order.result.status == OrderStatus::OPEN ||
            order.result.status == OrderStatus::PARTIALLY_FILLED) {
            ++count;
        }
    }
    return count;
}

void OrderManager::cleanup_old_orders(std::chrono::seconds max_age) {
    std::unique_lock lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    int removed = 0;
    for (auto it = orders_.begin(); it != orders_.end(); ) {
        bool is_terminal = (it->second.result.status == OrderStatus::FILLED ||
                            it->second.result.status == OrderStatus::CANCELLED ||
                            it->second.result.status == OrderStatus::REJECTED);
        bool is_old = (now - it->second.updated_at) > max_age;
        if (is_terminal && is_old) {
            it = orders_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    if (removed > 0) {
        LOG_INFO("OrderManager: cleaned up {} old orders", removed);
    }
}
