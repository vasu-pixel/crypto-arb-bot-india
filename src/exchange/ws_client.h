#pragma once

#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>

#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>

class ExchangeWsClient {
public:
    using WsClient = websocketpp::client<websocketpp::config::asio_tls_client>;
    using SslContext = websocketpp::lib::shared_ptr<boost::asio::ssl::context>;
    using ConnectionHdl = websocketpp::connection_hdl;
    using MessagePtr = WsClient::message_ptr;

    explicit ExchangeWsClient(const std::string& uri);
    ~ExchangeWsClient();

    // Non-copyable, non-movable (because of threads & callbacks)
    ExchangeWsClient(const ExchangeWsClient&) = delete;
    ExchangeWsClient& operator=(const ExchangeWsClient&) = delete;

    // ── Callbacks ───────────────────────────────────────────────────────
    void set_on_message(std::function<void(const std::string&)> callback);
    void set_on_connect(std::function<void()> callback);
    void set_on_disconnect(std::function<void()> callback);

    // ── Lifecycle ───────────────────────────────────────────────────────
    void connect();
    void disconnect();
    void send(const std::string& message);
    bool is_connected() const;
    void set_uri(const std::string& uri);

    // ── Configuration ───────────────────────────────────────────────────
    void set_auto_reconnect(bool enabled);
    void set_ping_interval_s(int seconds);

private:
    void on_open(ConnectionHdl hdl);
    void on_close(ConnectionHdl hdl);
    void on_fail(ConnectionHdl hdl);
    void on_message(ConnectionHdl hdl, MessagePtr msg);
    bool on_ping(ConnectionHdl hdl, std::string payload);
    SslContext on_tls_init(ConnectionHdl hdl);

    void run_io_thread();
    void reconnect_loop();
    void start_ping_timer();

    std::string uri_;
    WsClient client_;
    ConnectionHdl connection_hdl_;

    std::thread io_thread_;
    std::thread reconnect_thread_;

    std::mutex send_mutex_;
    std::mutex connection_mutex_;

    std::atomic<bool> connected_{false};
    std::atomic<bool> shutting_down_{false};
    std::atomic<bool> auto_reconnect_{true};
    std::atomic<int> ping_interval_s_{30};

    // Reconnect backoff
    int reconnect_delay_ms_ = 1000;
    static constexpr int kMaxReconnectDelayMs = 30000;
    static constexpr int kInitialReconnectDelayMs = 1000;

    // Callbacks
    std::function<void(const std::string&)> on_message_cb_;
    std::function<void()> on_connect_cb_;
    std::function<void()> on_disconnect_cb_;
};
