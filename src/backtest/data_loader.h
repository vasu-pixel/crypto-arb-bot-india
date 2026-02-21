#pragma once
#include "common/types.h"
#include <string>
#include <vector>

class DataLoader {
public:
    static std::vector<HistoricalSnapshot> load_csv_directory(const std::string& directory);
    static std::vector<HistoricalSnapshot> load_csv(const std::string& filepath);
    static void save_snapshot_csv(const std::string& directory,
                                   const OrderBookSnapshot& snapshot,
                                   uint64_t timestamp_ms);
    static std::vector<HistoricalSnapshot> filter(
        const std::vector<HistoricalSnapshot>& data,
        uint64_t from_ms, uint64_t to_ms,
        const std::vector<std::string>& pairs = {});
};
