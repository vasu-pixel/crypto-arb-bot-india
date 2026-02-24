#pragma once

#include "common/types.h"
#include "exchange/exchange_interface.h"

#include <atomic>
#include <chrono>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class FeeManager {
public:
  explicit FeeManager(std::vector<IExchange *> exchanges);
  ~FeeManager();

  // Query every exchange for fees on all known pairs and cache the results.
  void refresh_all_fees();

  // Look up cached fee info for a specific exchange+pair.
  FeeInfo get_fee(Exchange exch, const std::string &pair) const;

  // Sum of taker fees for buy exchange + sell exchange on the given pair.
  double total_fee_rate(Exchange buy_exch, Exchange sell_exch,
                        const std::string &pair) const;

  // Launch a background thread that calls refresh_all_fees every `interval`.
  void start_periodic_refresh(
      std::chrono::seconds interval = std::chrono::seconds(3600));

  // Stop the periodic refresh thread.
  void stop();

private:
  // Key: "PAIR", mapped to enum-indexed array
  std::unordered_map<std::string, std::array<std::optional<FeeInfo>, EXCHANGE_COUNT>>
      fee_cache_;
  mutable std::shared_mutex mutex_;
  std::vector<IExchange *> exchanges_;
  std::thread refresh_thread_;
  std::atomic<bool> running_{false};
};
