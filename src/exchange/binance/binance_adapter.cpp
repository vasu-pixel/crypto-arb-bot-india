#include "exchange/binance/binance_adapter.h"
#include "common/logger.h"
#include "exchange/exchange_factory.h"
#include <algorithm>
#include <stdexcept>

BinanceAdapter::BinanceAdapter(const Config &config) {
  auto it = config.exchanges.find(Exchange::BINANCE_US);
  if (it == config.exchanges.end()) {
    throw std::runtime_error(
        "BinanceAdapter: no exchange config for BINANCE_US");
  }
  config_ = it->second;
  rest_client_ = std::make_unique<RestClient>(config_.rest_base_url);
  auth_ = std::make_unique<BinanceAuth>(config_.api_key, config_.secret_key);
  rest_ = std::make_unique<BinanceRest>(*rest_client_, *auth_);
  ws_client_ = std::make_unique<ExchangeWsClient>(config_.ws_base_url);
  ws_ = std::make_unique<BinanceWs>(config_.ws_base_url, *ws_client_);
  build_pair_map();

  ws_client_->set_on_connect([this]() { ws_->on_connected(); });
}

void BinanceAdapter::build_pair_map() {
  // Common Binance.US pairs: canonical -> native
  pair_map_["BTC-USD"] = "BTCUSD";
  pair_map_["ETH-USD"] = "ETHUSD";
  pair_map_["SOL-USD"] = "SOLUSD";
  pair_map_["XRP-USD"] = "XRPUSD";
  pair_map_["ADA-USD"] = "ADAUSD";
  pair_map_["DOGE-USD"] = "DOGEUSD";
  pair_map_["DOT-USD"] = "DOTUSD";
  pair_map_["LINK-USD"] = "LINKUSD";
  pair_map_["AVAX-USD"] = "AVAXUSD";
  pair_map_["MATIC-USD"] = "MATICUSD";
  pair_map_["BTC-USDT"] = "BTCUSDT";
  pair_map_["ETH-USDT"] = "ETHUSDT";
}

std::string
BinanceAdapter::normalize_pair(const std::string &canonical_pair) const {
  auto it = pair_map_.find(canonical_pair);
  if (it != pair_map_.end())
    return it->second;
  // Fallback: remove '-'
  std::string result = canonical_pair;
  result.erase(std::remove(result.begin(), result.end(), '-'), result.end());
  return result;
}

std::string
BinanceAdapter::canonical_pair(const std::string &native_pair) const {
  for (auto &[canonical, native] : pair_map_) {
    if (native == native_pair)
      return canonical;
  }
  // Heuristic: find known quote assets
  for (const auto &quote : {"USDT", "USD", "USDC", "BTC", "ETH"}) {
    std::string q(quote);
    if (native_pair.size() > q.size() &&
        native_pair.substr(native_pair.size() - q.size()) == q) {
      std::string base = native_pair.substr(0, native_pair.size() - q.size());
      return base + "-" + q;
    }
  }
  return native_pair;
}

OrderBookSnapshot BinanceAdapter::fetch_order_book(const std::string &pair,
                                                   int depth) {
  return rest_->fetch_order_book(normalize_pair(pair), depth);
}

std::vector<std::pair<std::string, double>>
BinanceAdapter::fetch_top_pairs_by_volume(int limit) {
  auto tickers = rest_->fetch_24h_tickers();
  // Filter for USD/USDT pairs and sort by volume
  std::vector<std::pair<std::string, double>> result;
  for (auto &ticker : tickers) {
    std::string canonical = canonical_pair(ticker.symbol);
    if (canonical.find("-USD") != std::string::npos) {
      result.push_back({canonical, ticker.volume});
    }
  }
  std::sort(result.begin(), result.end(),
            [](const auto &a, const auto &b) { return a.second > b.second; });
  if (static_cast<int>(result.size()) > limit) {
    result.resize(limit);
  }
  return result;
}

FeeInfo BinanceAdapter::fetch_fees(const std::string &pair) {
  return rest_->fetch_trade_fee(normalize_pair(pair));
}

OrderResult BinanceAdapter::place_limit_order(const OrderRequest &req) {
  return rest_->place_order(normalize_pair(req.pair), req.side, "LIMIT",
                            req.price, req.quantity);
}

OrderResult BinanceAdapter::cancel_order(const std::string &pair,
                                         const std::string &order_id) {
  return rest_->cancel_order(normalize_pair(pair), order_id);
}

OrderResult BinanceAdapter::query_order(const std::string &pair,
                                        const std::string &order_id) {
  return rest_->query_order(normalize_pair(pair), order_id);
}

std::vector<BalanceInfo> BinanceAdapter::fetch_balances() {
  return rest_->fetch_account();
}

void BinanceAdapter::subscribe_order_book(const std::string &pair,
                                          OrderBookCallback cb) {
  std::string native = normalize_pair(pair);
  ws_->subscribe_depth(native, [pair, cb](const OrderBookSnapshot &snap) {
    OrderBookSnapshot canonical_snap = snap;
    canonical_snap.pair = pair;
    cb(canonical_snap);
  });
}

void BinanceAdapter::unsubscribe_order_book(const std::string &pair) {
  ws_->unsubscribe_depth(normalize_pair(pair));
}

void BinanceAdapter::connect() {
  // Build combined stream URL: /stream?streams=btcusd@depth20@100ms/ethusd@depth20@100ms/...
  // This is more reliable than subscribing via JSON on /ws endpoint
  auto streams = ws_->get_pending_streams();
  if (!streams.empty()) {
    std::string stream_path = "/stream?streams=";
    for (size_t i = 0; i < streams.size(); ++i) {
      if (i > 0) stream_path += "/";
      stream_path += streams[i];
    }
    // Replace /ws at end of base URL with /stream?streams=...
    std::string base = config_.ws_base_url;
    // Strip trailing path like /ws if present
    auto pos = base.find("/ws");
    if (pos != std::string::npos) {
      base = base.substr(0, pos);
    }
    std::string combined_url = base + stream_path;
    LOG_INFO("Binance: connecting with combined stream URL: {}", combined_url);
    ws_client_->set_uri(combined_url);
  }

  ws_client_->connect();
  LOG_INFO("Binance.US adapter connected");
}

void BinanceAdapter::disconnect() {
  ws_client_->disconnect();
  LOG_INFO("Binance.US adapter disconnected");
}

bool BinanceAdapter::is_connected() const { return ws_client_->is_connected(); }
