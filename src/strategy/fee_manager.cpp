#include "strategy/fee_manager.h"
#include "common/logger.h"

#include <algorithm>
#include <condition_variable>
#include <mutex>

FeeManager::FeeManager(std::vector<IExchange *> exchanges)
    : exchanges_(std::move(exchanges)) {}

FeeManager::~FeeManager() { stop(); }

void FeeManager::refresh_all_fees() {
  LOG_INFO("Refreshing fee schedule from all exchanges...");

  std::unordered_map<std::string, std::array<std::optional<FeeInfo>, EXCHANGE_COUNT>>
      new_cache;

  for (auto *exchange : exchanges_) {
    try {
      // Fetch top trading pairs to know which pairs to query fees for
      auto top_pairs = exchange->fetch_top_pairs_by_volume(20);

      for (const auto &[pair_name, volume] : top_pairs) {
        try {
          FeeInfo info = exchange->fetch_fees(pair_name);
          size_t exch_idx = static_cast<size_t>(exchange->exchange_id());
          new_cache[pair_name][exch_idx] = info;

          LOG_DEBUG("Fee for {}:{} -> maker={:.4f} taker={:.4f}",
                    exchange->exchange_name(), pair_name, info.maker_fee,
                    info.taker_fee);
        } catch (const std::exception &ex) {
          LOG_WARN("Failed to fetch fees for {}:{} - {}",
                   exchange->exchange_name(), pair_name, ex.what());
        }
      }
    } catch (const std::exception &ex) {
      LOG_ERROR("Failed to query pairs from {}: {}", exchange->exchange_name(),
                ex.what());
    }
  }

  // Swap in the new cache atomically
  {
    std::unique_lock lock(mutex_);
    fee_cache_ = std::move(new_cache);
  }

  LOG_INFO("Fee refresh complete. Cached {} fee entries.", fee_cache_.size());
}

// Internal helper: returns default fees per exchange (no lock required)
static FeeInfo default_fee_for_exchange(Exchange exch,
                                        const std::string &pair) {
  FeeInfo info;
  info.exchange = exch;
  info.pair = pair;

  // Conservative default fees per exchange when API fees are unavailable
  // (e.g., no API key configured). These are standard retail taker/maker rates.
  switch (exch) {
  case Exchange::BINANCE:
    info.maker_fee = 0.001; // 0.10%
    info.taker_fee = 0.001; // 0.10%
    break;
  case Exchange::OKX:
    info.maker_fee = 0.0008; // 0.08%
    info.taker_fee = 0.001;  // 0.10%
    break;
  case Exchange::BYBIT:
    info.maker_fee = 0.001; // 0.10%
    info.taker_fee = 0.001; // 0.10%
    break;
  case Exchange::MEXC:
    info.maker_fee = 0.0;    // 0.00% (MEXC zero maker)
    info.taker_fee = 0.0005; // 0.05%
    break;
  case Exchange::GATEIO:
    info.maker_fee = 0.002;  // 0.20% (VIP0 base rate)
    info.taker_fee = 0.002;  // 0.20% (VIP0 base rate)
    break;
  default:
    info.maker_fee = 0.001; // 0.10% conservative default
    info.taker_fee = 0.001; // 0.10% conservative default
    break;
  }
  return info;
}

FeeInfo FeeManager::get_fee(Exchange exch, const std::string &pair) const {
  std::shared_lock lock(mutex_);
  size_t exch_idx = static_cast<size_t>(exch);
  auto it = fee_cache_.find(pair);
  if (it != fee_cache_.end() && it->second[exch_idx].has_value()) {
    return it->second[exch_idx].value();
  }

  auto default_info = default_fee_for_exchange(exch, pair);
  LOG_WARN("No cached fee for {}:{}, using default taker={:.4f}",
           exchange_to_string(exch), pair, default_info.taker_fee);
  return default_info;
}

double FeeManager::total_fee_rate(Exchange buy_exch, Exchange sell_exch,
                                  const std::string &pair) const {
  std::shared_lock lock(mutex_);

  size_t buy_idx = static_cast<size_t>(buy_exch);
  size_t sell_idx = static_cast<size_t>(sell_exch);

  double buy_fee = 0.0;
  double sell_fee = 0.0;

  auto it = fee_cache_.find(pair);
  if (it != fee_cache_.end() && it->second[buy_idx].has_value()) {
    buy_fee = it->second[buy_idx]->taker_fee;
  } else {
    buy_fee = default_fee_for_exchange(buy_exch, pair).taker_fee;
  }

  if (it != fee_cache_.end() && it->second[sell_idx].has_value()) {
    sell_fee = it->second[sell_idx]->taker_fee;
  } else {
    sell_fee = default_fee_for_exchange(sell_exch, pair).taker_fee;
  }

  return buy_fee + sell_fee;
}

void FeeManager::start_periodic_refresh(std::chrono::seconds interval) {
  if (running_.exchange(true)) {
    LOG_WARN("FeeManager periodic refresh already running");
    return;
  }

  // Do an initial refresh synchronously
  refresh_all_fees();

  refresh_thread_ = std::thread([this, interval]() {
    LOG_INFO("FeeManager periodic refresh started (interval={}s)",
             interval.count());

    // Use a condition-variable style sleep so we can wake up promptly on stop()
    std::mutex sleep_mtx;
    while (running_.load()) {
      std::unique_lock lock(sleep_mtx);

      // Sleep in small increments to allow responsive shutdown
      auto deadline = std::chrono::steady_clock::now() + interval;
      while (running_.load() && std::chrono::steady_clock::now() < deadline) {
        // Sleep for at most 1 second at a time
        auto remaining = deadline - std::chrono::steady_clock::now();
        auto sleep_time = std::min(
            remaining,
            std::chrono::steady_clock::duration(std::chrono::seconds(1)));
        if (sleep_time.count() > 0) {
          lock.unlock();
          std::this_thread::sleep_for(sleep_time);
          lock.lock();
        }
      }

      if (running_.load()) {
        try {
          refresh_all_fees();
        } catch (const std::exception &ex) {
          LOG_ERROR("Fee refresh failed: {}", ex.what());
        }
      }
    }

    LOG_INFO("FeeManager periodic refresh stopped");
  });
}

void FeeManager::stop() {
  if (!running_.exchange(false)) {
    return; // Was already stopped
  }

  if (refresh_thread_.joinable()) {
    refresh_thread_.join();
  }
}
