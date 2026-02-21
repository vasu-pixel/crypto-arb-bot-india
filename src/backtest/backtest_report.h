#pragma once
#include "common/types.h"
#include <vector>
#include <string>

class BacktestReport {
public:
    static BacktestMetrics compute_metrics(const std::vector<TradeRecord>& trades);
    static std::string format_report(const BacktestMetrics& metrics);
    static void save_json(const BacktestMetrics& metrics, const std::string& filepath);
};
