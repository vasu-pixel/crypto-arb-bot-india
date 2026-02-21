#pragma once

#include "common/types.h"

#include <string>
#include <vector>
#include <mutex>

class TradeLogger {
public:
    explicit TradeLogger(const std::string& filepath);

    void log_trade(const TradeRecord& record);
    std::vector<TradeRecord> load_all_trades() const;
    double total_realized_pnl() const;
    double pnl_for_pair(const std::string& pair) const;

private:
    void append_to_file(const std::string& json_line);

    std::string filepath_;
    mutable std::mutex mutex_;
    std::mutex file_mutex_;
};
