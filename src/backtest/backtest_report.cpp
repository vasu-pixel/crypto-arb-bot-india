#include "backtest/backtest_report.h"
#include "common/logger.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <numeric>

BacktestMetrics BacktestReport::compute_metrics(const std::vector<TradeRecord>& trades) {
    BacktestMetrics m;
    m.total_trades = static_cast<int>(trades.size());

    if (trades.empty()) return m;

    // Collect PnL values
    std::vector<double> pnls;
    double cumulative = 0;
    double peak = 0;
    double max_dd = 0;
    double gross_wins = 0;
    double gross_losses = 0;

    for (auto& t : trades) {
        pnls.push_back(t.realized_pnl);
        m.total_pnl += t.realized_pnl;
        m.total_fees_paid += t.buy_result.fee_paid + t.sell_result.fee_paid;
        m.pnl_per_pair[t.pair] += t.realized_pnl;

        if (t.realized_pnl > 0) {
            m.winning_trades++;
            gross_wins += t.realized_pnl;
        } else {
            gross_losses += std::abs(t.realized_pnl);
        }

        // Equity curve
        cumulative += t.realized_pnl;
        m.equity_curve.push_back({t.timestamp_iso, cumulative});

        // Drawdown
        if (cumulative > peak) peak = cumulative;
        double dd = peak - cumulative;
        if (dd > max_dd) max_dd = dd;
    }

    m.win_rate = (m.total_trades > 0) ?
                 (static_cast<double>(m.winning_trades) / m.total_trades * 100.0) : 0.0;
    m.avg_trade_pnl = m.total_pnl / m.total_trades;
    m.max_drawdown = max_dd;
    m.max_drawdown_pct = (peak > 0) ? (max_dd / peak * 100.0) : 0.0;
    m.profit_factor = (gross_losses > 0) ? (gross_wins / gross_losses) : (gross_wins > 0 ? 999.0 : 0.0);

    // Sharpe ratio (annualized, assuming 365 trading days, ~8760 trades/year at ~hourly)
    if (pnls.size() > 1) {
        double mean = std::accumulate(pnls.begin(), pnls.end(), 0.0) / pnls.size();
        double sq_sum = 0;
        for (auto p : pnls) sq_sum += (p - mean) * (p - mean);
        double stddev = std::sqrt(sq_sum / (pnls.size() - 1));
        if (stddev > 0) {
            m.sharpe_ratio = (mean / stddev) * std::sqrt(365.0 * 24.0); // Annualize
        }
    }

    return m;
}

std::string BacktestReport::format_report(const BacktestMetrics& m) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);
    oss << "====================================\n";
    oss << "       BACKTEST REPORT\n";
    oss << "====================================\n";
    oss << "Total P&L:        $" << m.total_pnl << "\n";
    oss << "Total Trades:     " << m.total_trades << "\n";
    oss << "Winning Trades:   " << m.winning_trades << "\n";
    oss << "Win Rate:         " << std::setprecision(1) << m.win_rate << "%\n";
    oss << std::setprecision(4);
    oss << "Avg Trade P&L:    $" << m.avg_trade_pnl << "\n";
    oss << "Sharpe Ratio:     " << std::setprecision(2) << m.sharpe_ratio << "\n";
    oss << "Max Drawdown:     $" << std::setprecision(4) << m.max_drawdown << "\n";
    oss << "Max Drawdown %:   " << std::setprecision(1) << m.max_drawdown_pct << "%\n";
    oss << "Profit Factor:    " << std::setprecision(2) << m.profit_factor << "\n";
    oss << "Total Fees:       $" << std::setprecision(4) << m.total_fees_paid << "\n";
    oss << "------------------------------------\n";
    oss << "P&L Per Pair:\n";
    for (auto& [pair, pnl] : m.pnl_per_pair) {
        oss << "  " << pair << ": $" << std::setprecision(4) << pnl << "\n";
    }
    oss << "====================================\n";
    return oss.str();
}

void BacktestReport::save_json(const BacktestMetrics& metrics, const std::string& filepath) {
    nlohmann::json j;
    to_json(j, metrics);
    std::ofstream file(filepath);
    file << j.dump(2);
    LOG_INFO("Backtest report saved to {}", filepath);
}
