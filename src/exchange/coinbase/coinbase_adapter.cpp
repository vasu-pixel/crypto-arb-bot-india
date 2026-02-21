#include "exchange/coinbase/coinbase_adapter.h"
#include "common/crypto_utils.h"
#include "common/logger.h"
#include <algorithm>
#include <stdexcept>

CoinbaseAdapter::CoinbaseAdapter(const Config &config) {
  auto it = config.exchanges.find(Exchange::COINBASE);
  if (it == config.exchanges.end()) {
    throw std::runtime_error(
        "CoinbaseAdapter: no exchange config for COINBASE");
  }
  config_ = it->second;
  rest_client_ = std::make_unique<RestClient>(config_.rest_base_url);
  auth_ = std::make_unique<CoinbaseAuth>(config_.api_key, config_.secret_key);
  rest_ = std::make_unique<CoinbaseRest>(*rest_client_, *auth_);
  ws_client_ = std::make_unique<ExchangeWsClient>(config_.ws_base_url);
  ws_ = std::make_unique<CoinbaseWs>(config_.ws_base_url, *ws_client_, *auth_);

  // Fetch fees once on construction
  try {
    cached_fees_ = rest_->fetch_transaction_summary();
  } catch (...) {
    cached_fees_.maker_fee = 0.004;
    cached_fees_.taker_fee = 0.006;
    cached_fees_.taker_fee = 0.006;
  }

  ws_client_->set_on_connect([this]() { ws_->on_connected(); });
}

// Coinbase already uses "BTC-USD" format, same as canonical
std::string
CoinbaseAdapter::normalize_pair(const std::string &canonical_pair) const {
  return canonical_pair;
}

std::string
CoinbaseAdapter::canonical_pair(const std::string &native_pair) const {
  return native_pair;
}

OrderBookSnapshot CoinbaseAdapter::fetch_order_book(const std::string &pair,
                                                    int depth) {
  return rest_->fetch_product_book(pair, depth);
}

std::vector<std::pair<std::string, double>>
CoinbaseAdapter::fetch_top_pairs_by_volume(int limit) {
  auto products = rest_->fetch_products();
  // Filter USD pairs
  std::vector<std::pair<std::string, double>> result;
  for (auto &[id, volume] : products) {
    if (id.find("-USD") != std::string::npos &&
        id.find("-USDT") == std::string::npos) {
      result.push_back({id, volume});
    }
  }
  std::sort(result.begin(), result.end(),
            [](const auto &a, const auto &b) { return a.second > b.second; });
  if (static_cast<int>(result.size()) > limit)
    result.resize(limit);
  return result;
}

FeeInfo CoinbaseAdapter::fetch_fees(const std::string &pair) {
  FeeInfo info = cached_fees_;
  info.pair = pair;
  return info;
}

OrderResult CoinbaseAdapter::place_limit_order(const OrderRequest &req) {
  std::string side_str = req.side == Side::BUY ? "BUY" : "SELL";
  return rest_->create_order(req.pair, side_str, req.price, req.quantity,
                             req.client_order_id);
}

OrderResult CoinbaseAdapter::cancel_order(const std::string &pair,
                                          const std::string &order_id) {
  return rest_->cancel_order(order_id);
}

OrderResult CoinbaseAdapter::query_order(const std::string &pair,
                                         const std::string &order_id) {
  return rest_->get_order(order_id);
}

std::vector<BalanceInfo> CoinbaseAdapter::fetch_balances() {
  return rest_->fetch_accounts();
}

void CoinbaseAdapter::subscribe_order_book(const std::string &pair,
                                           OrderBookCallback cb) {
  ws_->subscribe_level2(pair, std::move(cb));
}

void CoinbaseAdapter::unsubscribe_order_book(const std::string &pair) {
  ws_->unsubscribe_level2(pair);
}

void CoinbaseAdapter::connect() {
  ws_client_->connect();
  LOG_INFO("Coinbase adapter connected");
}

void CoinbaseAdapter::disconnect() {
  ws_client_->disconnect();
  LOG_INFO("Coinbase adapter disconnected");
}

bool CoinbaseAdapter::is_connected() const {
  return ws_client_->is_connected();
}
