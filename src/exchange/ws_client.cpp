#include "exchange/ws_client.h"
#include "common/logger.h"

#include <algorithm>
#include <chrono>

// ─── Constructor / Destructor ───────────────────────────────────────────────

ExchangeWsClient::ExchangeWsClient(const std::string &uri) : uri_(uri) {
  // Enable all logging for debugging connection failures
  client_.set_access_channels(websocketpp::log::alevel::all);
  client_.set_error_channels(websocketpp::log::elevel::all);

  client_.init_asio();

  client_.set_open_handler([this](ConnectionHdl hdl) { on_open(hdl); });
  client_.set_close_handler([this](ConnectionHdl hdl) { on_close(hdl); });
  client_.set_fail_handler([this](ConnectionHdl hdl) { on_fail(hdl); });
  client_.set_message_handler(
      [this](ConnectionHdl hdl, MessagePtr msg) { on_message(hdl, msg); });
  client_.set_ping_handler([this](ConnectionHdl hdl, std::string payload) {
    return on_ping(hdl, payload);
  });
  client_.set_tls_init_handler(
      [this](ConnectionHdl hdl) { return on_tls_init(hdl); });

  LOG_DEBUG("ExchangeWsClient created for uri={}", uri_);
}

ExchangeWsClient::~ExchangeWsClient() { disconnect(); }

// ─── Callback setters ───────────────────────────────────────────────────────

void ExchangeWsClient::set_on_message(
    std::function<void(const std::string &)> callback) {
  on_message_cb_ = std::move(callback);
}

void ExchangeWsClient::set_on_connect(std::function<void()> callback) {
  on_connect_cb_ = std::move(callback);
}

void ExchangeWsClient::set_on_disconnect(std::function<void()> callback) {
  on_disconnect_cb_ = std::move(callback);
}

void ExchangeWsClient::set_auto_reconnect(bool enabled) {
  auto_reconnect_.store(enabled);
}

void ExchangeWsClient::set_ping_interval_s(int seconds) {
  ping_interval_s_.store(seconds);
}

// ─── Lifecycle ──────────────────────────────────────────────────────────────

void ExchangeWsClient::connect() {
  shutting_down_.store(false);
  reconnect_delay_ms_ = kInitialReconnectDelayMs;

  websocketpp::lib::error_code ec;
  auto con = client_.get_connection(uri_, ec);
  if (ec) {
    LOG_ERROR("ExchangeWsClient: connect error: {}", ec.message());
    return;
  }

  // Set a standard User-Agent, as many crypto exchanges drop connections
  // missing it
  con->append_header("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                                   "Chrome/120.0.0.0 Safari/537.36");

  {
    std::lock_guard<std::mutex> lock(connection_mutex_);
    connection_hdl_ = con->get_handle();
  }

  // Clear previous run state
  client_.get_alog().write(websocketpp::log::alevel::app,
                           "Connecting to " + uri_);

  // Connect and start thread
  client_.connect(con);
  if (io_thread_.joinable()) {
    io_thread_.join();
  }
  io_thread_ = std::thread([this]() { run_io_thread(); });

  LOG_INFO("ExchangeWsClient: connecting to {}", uri_);
}

void ExchangeWsClient::disconnect() {
  shutting_down_.store(true);
  auto_reconnect_.store(false);

  if (connected_.load()) {
    websocketpp::lib::error_code ec;
    std::lock_guard<std::mutex> lock(connection_mutex_);
    client_.close(connection_hdl_, websocketpp::close::status::normal,
                  "client disconnect", ec);
    if (ec) {
      LOG_WARN("ExchangeWsClient: close error: {}", ec.message());
    }
  }

  client_.stop();

  if (io_thread_.joinable()) {
    io_thread_.join();
  }
  if (reconnect_thread_.joinable()) {
    reconnect_thread_.join();
  }

  connected_.store(false);
  LOG_INFO("ExchangeWsClient: disconnected from {}", uri_);
}

void ExchangeWsClient::send(const std::string &message) {
  if (!connected_.load()) {
    LOG_WARN("ExchangeWsClient: cannot send, not connected");
    return;
  }

  std::lock_guard<std::mutex> lock(send_mutex_);
  websocketpp::lib::error_code ec;
  {
    std::lock_guard<std::mutex> conn_lock(connection_mutex_);
    client_.send(connection_hdl_, message, websocketpp::frame::opcode::text,
                 ec);
  }
  if (ec) {
    LOG_ERROR("ExchangeWsClient: send error: {}", ec.message());
  }
}

bool ExchangeWsClient::is_connected() const { return connected_.load(); }

void ExchangeWsClient::set_uri(const std::string &uri) {
  uri_ = uri;
  LOG_DEBUG("ExchangeWsClient: URI updated to {}", uri_);
}

// ─── Internal handlers ──────────────────────────────────────────────────────

void ExchangeWsClient::on_open(ConnectionHdl hdl) {
  connected_.store(true);
  reconnect_delay_ms_ = kInitialReconnectDelayMs;
  LOG_INFO("ExchangeWsClient: connection opened to {}", uri_);

  if (on_connect_cb_) {
    on_connect_cb_();
  }
}

void ExchangeWsClient::on_close(ConnectionHdl hdl) {
  connected_.store(false);

  // Extract close code and reason for diagnostics
  try {
    auto con = client_.get_con_from_hdl(hdl);
    auto code = con->get_remote_close_code();
    auto reason = con->get_remote_close_reason();
    LOG_WARN("ExchangeWsClient: connection closed for {} (code={}, reason={})",
             uri_, code, reason.empty() ? "<none>" : reason);
  } catch (...) {
    LOG_INFO("ExchangeWsClient: connection closed for {}", uri_);
  }

  if (on_disconnect_cb_) {
    on_disconnect_cb_();
  }

  if (auto_reconnect_.load() && !shutting_down_.load()) {
    // Launch reconnect in a separate thread so we don't block the io_service
    if (reconnect_thread_.joinable()) {
      reconnect_thread_.join();
    }
    reconnect_thread_ = std::thread([this]() { reconnect_loop(); });
  }
}

void ExchangeWsClient::on_fail(ConnectionHdl hdl) {
  connected_.store(false);
  LOG_ERROR("ExchangeWsClient: connection failed for {}", uri_);

  if (on_disconnect_cb_) {
    on_disconnect_cb_();
  }

  if (auto_reconnect_.load() && !shutting_down_.load()) {
    if (reconnect_thread_.joinable()) {
      reconnect_thread_.join();
    }
    reconnect_thread_ = std::thread([this]() { reconnect_loop(); });
  }
}

void ExchangeWsClient::on_message(ConnectionHdl /*hdl*/, MessagePtr msg) {
  if (on_message_cb_) {
    on_message_cb_(msg->get_payload());
  }
}

bool ExchangeWsClient::on_ping(ConnectionHdl hdl, std::string payload) {
  // Respond with pong automatically (websocketpp does this by default
  // when we return true)
  LOG_DEBUG("ExchangeWsClient: received ping, sending pong");
  return true;
}

ExchangeWsClient::SslContext
ExchangeWsClient::on_tls_init(ConnectionHdl /*hdl*/) {
  auto ctx = std::make_shared<boost::asio::ssl::context>(
      boost::asio::ssl::context::tlsv12_client);

  try {
    ctx->set_options(boost::asio::ssl::context::default_workarounds |
                     boost::asio::ssl::context::no_sslv2 |
                     boost::asio::ssl::context::no_sslv3 |
                     boost::asio::ssl::context::single_dh_use);

    ctx->set_default_verify_paths();
    // Disable rigorous peer verification for debugging
    ctx->set_verify_mode(boost::asio::ssl::verify_none);
  } catch (const std::exception &e) {
    LOG_ERROR("ExchangeWsClient: TLS init error: {}", e.what());
  }

  return ctx;
}

// ─── IO & Reconnect threads ────────────────────────────────────────────────

void ExchangeWsClient::run_io_thread() {
  try {
    client_.run();
  } catch (const std::exception &e) {
    LOG_ERROR("ExchangeWsClient: io_service exception: {}", e.what());
  }
  LOG_DEBUG("ExchangeWsClient: io_service thread exiting");
}

void ExchangeWsClient::reconnect_loop() {
  while (!shutting_down_.load() && auto_reconnect_.load() &&
         !connected_.load()) {
    LOG_INFO("ExchangeWsClient: reconnecting in {}ms", reconnect_delay_ms_);
    std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_delay_ms_));

    if (shutting_down_.load())
      break;

    // Exponential backoff: 1s, 2s, 4s, 8s, ..., max 30s
    reconnect_delay_ms_ =
        std::min(reconnect_delay_ms_ * 2, kMaxReconnectDelayMs);

    // Reset io_service and reconnect
    client_.reset();

    websocketpp::lib::error_code ec;
    auto con = client_.get_connection(uri_, ec);
    if (ec) {
      LOG_ERROR("ExchangeWsClient: reconnect get_connection error: {}",
                ec.message());
      continue;
    }

    // Set User-Agent on reconnect (same as initial connect)
    con->append_header("User-Agent",
                       "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                       "Chrome/120.0.0.0 Safari/537.36");

    {
      std::lock_guard<std::mutex> lock(connection_mutex_);
      connection_hdl_ = con->get_handle();
    }

    client_.connect(con);

    // Run io_service again (previous run() returned when connection closed)
    if (io_thread_.joinable()) {
      io_thread_.join();
    }
    io_thread_ = std::thread([this]() { run_io_thread(); });

    // Wait a bit to see if connection succeeds
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
}
