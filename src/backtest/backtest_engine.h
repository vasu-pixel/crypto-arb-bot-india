#pragma once
#include "common/types.h"
#include "backtest/simulated_exchange.h"
#include "backtest/data_loader.h"
#include "backtest/backtest_report.h"
#include <vector>
#include <memory>
#include <map>

class BacktestEngine {
public:
    explicit BacktestEngine(const BacktestConfig& config);

    BacktestMetrics run(const std::vector<HistoricalSnapshot>& data);
    const std::vector<TradeRecord>& get_trades() const { return trades_; }

private:
    BacktestConfig config_;
    std::vector<TradeRecord> trades_;
};
