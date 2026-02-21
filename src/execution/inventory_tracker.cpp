#include "execution/inventory_tracker.h"
#include "common/logger.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

InventoryTracker::InventoryTracker(std::unordered_map<Exchange, IExchange*> exchanges,
                                   double drift_threshold_pct)
    : exchanges_(std::move(exchanges))
    , drift_threshold_pct_(drift_threshold_pct)
{
}

InventoryTracker::~InventoryTracker() {
    stop();
}

void InventoryTracker::refresh_balances() {
    std::unique_lock lock(mutex_);
    for (auto& [exch, iface] : exchanges_) {
        try {
            auto balances = iface->fetch_balances();
            InventoryState state;
            for (const auto& b : balances) {
                double total = b.free + b.locked;
                if (total > 0.0) {
                    state.balances[b.asset] = total;
                }
            }
            // USD value is approximated by the sum of USD/USDT-like assets.
            // A proper implementation would fetch prices for each asset.
            double usd_approx = 0.0;
            for (const auto& [asset, amount] : state.balances) {
                if (asset == "USD" || asset == "USDT" || asset == "USDC") {
                    usd_approx += amount;
                }
            }
            state.total_usd_value = usd_approx;
            states_[exch] = std::move(state);
            LOG_DEBUG("InventoryTracker: refreshed balances for {}",
                      exchange_to_string(exch));
        } catch (const std::exception& e) {
            LOG_ERROR("InventoryTracker: failed to refresh balances for {}: {}",
                      exchange_to_string(exch), e.what());
        }
    }
}

InventoryState InventoryTracker::get_state(Exchange exch) const {
    std::shared_lock lock(mutex_);
    auto it = states_.find(exch);
    if (it == states_.end()) {
        return {};
    }
    return it->second;
}

std::map<Exchange, InventoryState> InventoryTracker::get_all_states() const {
    std::shared_lock lock(mutex_);
    std::map<Exchange, InventoryState> result;
    for (const auto& [exch, state] : states_) {
        result[exch] = state;
    }
    return result;
}

std::vector<DriftAlert> InventoryTracker::check_drift() const {
    std::shared_lock lock(mutex_);
    std::vector<DriftAlert> alerts;

    // Collect all known assets across all exchanges
    std::map<std::string, std::map<Exchange, double>> asset_balances;
    for (const auto& [exch, state] : states_) {
        for (const auto& [asset, amount] : state.balances) {
            asset_balances[asset][exch] = amount;
        }
    }

    // For each asset, compare balances across exchanges
    for (const auto& [asset, exch_amounts] : asset_balances) {
        if (exch_amounts.size() < 2) {
            continue; // Need at least 2 exchanges to compare
        }

        // Compute total across all exchanges
        double total = 0.0;
        for (const auto& [exch, amt] : exch_amounts) {
            total += amt;
        }
        if (total <= 0.0) {
            continue;
        }

        double avg = total / static_cast<double>(exch_amounts.size());

        // Find the exchange with the most and least of this asset
        Exchange max_exch = exch_amounts.begin()->first;
        Exchange min_exch = exch_amounts.begin()->first;
        double max_amt = exch_amounts.begin()->second;
        double min_amt = exch_amounts.begin()->second;

        for (const auto& [exch, amt] : exch_amounts) {
            if (amt > max_amt) {
                max_amt = amt;
                max_exch = exch;
            }
            if (amt < min_amt) {
                min_amt = amt;
                min_exch = exch;
            }
        }

        // Compute imbalance as percentage deviation from mean
        double imbalance_pct = 0.0;
        if (avg > 0.0) {
            imbalance_pct = ((max_amt - min_amt) / avg) * 100.0;
        }

        if (imbalance_pct > drift_threshold_pct_) {
            DriftAlert alert;
            alert.asset = asset;
            alert.excess_exchange = max_exch;
            alert.deficit_exchange = min_exch;
            alert.imbalance_pct = imbalance_pct;

            std::ostringstream msg;
            msg << "Drift alert for " << asset << ": "
                << exchange_to_string(max_exch) << " has " << max_amt
                << " vs " << exchange_to_string(min_exch) << " has " << min_amt
                << " (imbalance " << std::fixed << std::setprecision(1)
                << imbalance_pct << "%)";
            alert.message = msg.str();

            alerts.push_back(std::move(alert));
            LOG_WARN("{}", alerts.back().message);
        }
    }

    return alerts;
}

void InventoryTracker::start_monitoring(std::chrono::seconds interval) {
    if (running_.load()) {
        LOG_WARN("InventoryTracker: monitoring already running");
        return;
    }

    running_.store(true);
    monitor_thread_ = std::thread([this, interval]() {
        LOG_INFO("InventoryTracker: monitoring started (interval={}s)", interval.count());
        while (running_.load()) {
            refresh_balances();
            auto drift_alerts = check_drift();
            if (!drift_alerts.empty()) {
                LOG_WARN("InventoryTracker: {} drift alerts detected", drift_alerts.size());
            }

            // Sleep in small increments so we can respond to stop() quickly
            auto end_time = std::chrono::steady_clock::now() + interval;
            while (running_.load() && std::chrono::steady_clock::now() < end_time) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }
        LOG_INFO("InventoryTracker: monitoring stopped");
    });
}

void InventoryTracker::stop() {
    if (running_.exchange(false)) {
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }
}

void InventoryTracker::record_fill(Exchange exch, const std::string& asset, double delta) {
    std::unique_lock lock(mutex_);
    states_[exch].balances[asset] += delta;
    LOG_DEBUG("InventoryTracker: recorded fill on {} asset={} delta={:.8f} new_balance={:.8f}",
              exchange_to_string(exch), asset, delta, states_[exch].balances[asset]);
}
