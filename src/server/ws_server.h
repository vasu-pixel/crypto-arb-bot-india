#pragma once
#include "server/broadcast_queue.h"
#include "server/message_types.h"
#include "common/types.h"

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <set>
#include <shared_mutex>
#include <thread>
#include <atomic>
#include <string>
#include <map>
#include <unordered_map>

using WsServer = websocketpp::server<websocketpp::config::asio>;

class DashboardWsServer {
public:
    explicit DashboardWsServer(uint16_t port = 9002);
    ~DashboardWsServer();

    void start();
    void stop();

    // Non-blocking broadcast: enqueues message into ring buffer
    void broadcast(const std::string& message);

    // Typed broadcast helpers
    void broadcast_trade(const TradeRecord& trade);
    void broadcast_spreads(const std::string& pair,
                           const std::map<std::string,
                           std::map<std::string, std::pair<double, double>>>& spreads);
    void broadcast_balances(const std::map<Exchange,
                            std::unordered_map<std::string, double>>& balances);
    void broadcast_pnl(double total_pnl, const std::map<std::string, double>& pnl_per_pair,
                        int total_trades, double win_rate, double total_fees,
                        const std::map<std::string, double>& fees_per_exchange);
    void broadcast_heartbeat();

private:
    void run();
    void on_open(websocketpp::connection_hdl hdl);
    void on_close(websocketpp::connection_hdl hdl);
    void drain_loop();

    WsServer server_;
    uint16_t port_;
    std::set<websocketpp::connection_hdl,
             std::owner_less<websocketpp::connection_hdl>> connections_;
    std::shared_mutex conn_mutex_;
    std::thread server_thread_;
    std::thread drain_thread_;
    std::atomic<bool> running_{false};
    BroadcastQueue queue_;
    uint64_t heartbeat_seq_ = 0;
};
