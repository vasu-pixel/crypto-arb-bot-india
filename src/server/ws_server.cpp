#include "server/ws_server.h"
#include "common/logger.h"
#include "common/time_utils.h"

DashboardWsServer::DashboardWsServer(uint16_t port)
    : port_(port), queue_(8192) {
    server_.set_access_channels(websocketpp::log::alevel::none);
    server_.set_error_channels(websocketpp::log::elevel::fatal);

    server_.init_asio();
    server_.set_reuse_addr(true);

    server_.set_open_handler([this](websocketpp::connection_hdl hdl) { on_open(hdl); });
    server_.set_close_handler([this](websocketpp::connection_hdl hdl) { on_close(hdl); });
}

DashboardWsServer::~DashboardWsServer() {
    stop();
}

void DashboardWsServer::on_open(websocketpp::connection_hdl hdl) {
    std::unique_lock lock(conn_mutex_);
    connections_.insert(hdl);
    LOG_INFO("Dashboard client connected (total: {})", connections_.size());
}

void DashboardWsServer::on_close(websocketpp::connection_hdl hdl) {
    std::unique_lock lock(conn_mutex_);
    connections_.erase(hdl);
    LOG_INFO("Dashboard client disconnected (total: {})", connections_.size());
}

void DashboardWsServer::start() {
    running_ = true;
    server_.listen(port_);
    server_.start_accept();

    server_thread_ = std::thread([this]() { run(); });
    drain_thread_ = std::thread([this]() { drain_loop(); });
    LOG_INFO("Dashboard WS server started on port {}", port_);
}

void DashboardWsServer::run() {
    server_.run();
}

void DashboardWsServer::stop() {
    if (!running_) return;
    running_ = false;

    server_.stop_listening();
    {
        std::shared_lock lock(conn_mutex_);
        for (auto& hdl : connections_) {
            try {
                server_.close(hdl, websocketpp::close::status::going_away, "Server shutting down");
            } catch (...) {}
        }
    }
    server_.stop();

    if (server_thread_.joinable()) server_thread_.join();
    if (drain_thread_.joinable()) drain_thread_.join();
    LOG_INFO("Dashboard WS server stopped");
}

void DashboardWsServer::broadcast(const std::string& message) {
    if (!queue_.try_push(message)) {
        LOG_DEBUG("Broadcast queue full, message dropped");
    }
}

void DashboardWsServer::drain_loop() {
    std::map<websocketpp::connection_hdl, int,
             std::owner_less<websocketpp::connection_hdl>> fail_counts;

    while (running_) {
        auto msg = queue_.try_pop();
        if (!msg) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        std::shared_lock lock(conn_mutex_);
        std::vector<websocketpp::connection_hdl> to_close;

        for (auto& hdl : connections_) {
            try {
                server_.send(hdl, *msg, websocketpp::frame::opcode::text);
                fail_counts[hdl] = 0;
            } catch (const std::exception& e) {
                fail_counts[hdl]++;
                if (fail_counts[hdl] >= 3) {
                    to_close.push_back(hdl);
                    LOG_WARN("Closing slow dashboard client after 3 failures");
                }
            }
        }

        lock.unlock();

        for (auto& hdl : to_close) {
            try {
                server_.close(hdl, websocketpp::close::status::going_away, "Too slow");
            } catch (...) {}
        }
    }
}

void DashboardWsServer::broadcast_trade(const TradeRecord& trade) {
    auto msg = MessageTypes::make_trade_message(trade);
    broadcast(msg.dump());
}

void DashboardWsServer::broadcast_spreads(
    const std::string& pair,
    const std::map<std::string, std::map<std::string, std::pair<double, double>>>& spreads) {
    auto msg = MessageTypes::make_spreads_message(pair, spreads);
    broadcast(msg.dump());
}

void DashboardWsServer::broadcast_balances(
    const std::map<Exchange, std::unordered_map<std::string, double>>& balances) {
    auto msg = MessageTypes::make_balances_message(balances);
    broadcast(msg.dump());
}

void DashboardWsServer::broadcast_pnl(double total_pnl,
                                        const std::map<std::string, double>& pnl_per_pair,
                                        int total_trades, double win_rate,
                                        double total_fees,
                                        const std::map<std::string, double>& fees_per_exchange) {
    auto msg = MessageTypes::make_pnl_message(total_pnl, pnl_per_pair,
                                                total_trades, win_rate, total_fees,
                                                fees_per_exchange);
    broadcast(msg.dump());
}

void DashboardWsServer::broadcast_heartbeat() {
    heartbeat_seq_++;
    auto msg = MessageTypes::make_heartbeat_message(heartbeat_seq_, queue_.dropped_count());
    broadcast(msg.dump());
}

void DashboardWsServer::broadcast_prices(
    const std::map<std::string, std::vector<MessageTypes::ExchangePrice>>& prices) {
    auto msg = MessageTypes::make_prices_message(prices);
    broadcast(msg.dump());
}
