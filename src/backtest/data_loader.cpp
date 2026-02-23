#include "backtest/data_loader.h"
#include "common/logger.h"
#include "common/time_utils.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

std::vector<HistoricalSnapshot>
DataLoader::load_csv(const std::string &filepath) {
  std::vector<HistoricalSnapshot> snapshots;
  std::ifstream file(filepath);
  if (!file.is_open()) {
    LOG_ERROR("Cannot open CSV: {}", filepath);
    return snapshots;
  }

  std::string line;
  // Skip header
  std::getline(file, line);

  while (std::getline(file, line)) {
    if (line.empty())
      continue;

    std::istringstream ss(line);
    std::string token;
    HistoricalSnapshot snap;

    // Format:
    // timestamp_ms,exchange,pair,bid1_price,bid1_qty,...,ask1_price,ask1_qty,...
    std::getline(ss, token, ',');
    snap.timestamp_ms = std::stoull(token);
    std::getline(ss, token, ',');
    snap.exchange = exchange_from_string(token);
    std::getline(ss, token, ',');
    snap.pair = token;

    // Read up to 20 bid levels
    for (int i = 0; i < 20; ++i) {
      std::string price_str, qty_str;
      if (!std::getline(ss, price_str, ','))
        break;
      if (!std::getline(ss, qty_str, ','))
        break;
      if (price_str.empty())
        break;
      double price = std::stod(price_str);
      double qty = std::stod(qty_str);
      if (price > 0)
        snap.bids.push_back({price, qty});
    }

    // Read up to 20 ask levels
    for (int i = 0; i < 20; ++i) {
      std::string price_str, qty_str;
      if (!std::getline(ss, price_str, ','))
        break;
      if (!std::getline(ss, qty_str, ','))
        break;
      if (price_str.empty())
        break;
      double price = std::stod(price_str);
      double qty = std::stod(qty_str);
      if (price > 0)
        snap.asks.push_back({price, qty});
    }

    snapshots.push_back(std::move(snap));
  }

  LOG_INFO("Loaded {} snapshots from {}", snapshots.size(), filepath);
  return snapshots;
}

std::vector<HistoricalSnapshot>
DataLoader::load_csv_directory(const std::string &directory) {
  std::vector<HistoricalSnapshot> all;

  for (auto &entry : fs::directory_iterator(directory)) {
    if (entry.path().extension() == ".csv") {
      auto snapshots = load_csv(entry.path().string());
      all.insert(all.end(), std::make_move_iterator(snapshots.begin()),
                 std::make_move_iterator(snapshots.end()));
    }
  }

  // Sort by timestamp
  std::sort(all.begin(), all.end(), [](const auto &a, const auto &b) {
    return a.timestamp_ms < b.timestamp_ms;
  });

  LOG_INFO("Loaded {} total snapshots from {}", all.size(), directory);
  return all;
}

void DataLoader::save_snapshot_csv(const std::string &directory,
                                   const OrderBookSnapshot &snapshot,
                                   uint64_t timestamp_ms) {
  std::string filename = directory + "/" +
                         std::string(exchange_to_string(snapshot.exchange)) +
                         "_" + snapshot.pair + ".csv";

  bool file_exists = fs::exists(filename);
  std::ofstream file(filename, std::ios::app);
  if (!file.is_open())
    return;

  // Write header if new file
  if (!file_exists) {
    file << "timestamp_ms,exchange,pair";
    for (int i = 0; i < 20; ++i)
      file << ",bid" << i << "_price,bid" << i << "_qty";
    for (int i = 0; i < 20; ++i)
      file << ",ask" << i << "_price,ask" << i << "_qty";
    file << "\n";
  }

  file << timestamp_ms << "," << exchange_to_string(snapshot.exchange) << ","
       << snapshot.pair;
  for (int i = 0; i < 20; ++i) {
    if (i < static_cast<int>(snapshot.bids.size())) {
      file << "," << snapshot.bids[i].price << "," << snapshot.bids[i].quantity;
    } else {
      file << ",,";
    }
  }
  for (int i = 0; i < 20; ++i) {
    if (i < static_cast<int>(snapshot.asks.size())) {
      file << "," << snapshot.asks[i].price << "," << snapshot.asks[i].quantity;
    } else {
      file << ",,";
    }
  }
  file << "\n";
}

std::vector<HistoricalSnapshot>
DataLoader::filter(const std::vector<HistoricalSnapshot> &data,
                   uint64_t from_ms, uint64_t to_ms,
                   const std::vector<std::string> &pairs) {
  std::vector<HistoricalSnapshot> result;
  for (auto &snap : data) {
    if (snap.timestamp_ms < from_ms || snap.timestamp_ms > to_ms)
      continue;
    if (!pairs.empty()) {
      bool found = false;
      for (auto &p : pairs) {
        if (snap.pair == p) {
          found = true;
          break;
        }
      }
      if (!found)
        continue;
    }
    result.push_back(snap);
  }
  return result;
}
