#include "exchange/okx/okx_adapter.h"
#include "common/logger.h"
#include <algorithm>
#include <stdexcept>

OkxAdapter::OkxAdapter(const Config& config) {
    auto it = config.exchanges.find(Exchange::OKX);
    if (it == config.exchanges.end()) {
        throw std::runtime_error("OkxAdapter: no exchange config for OKX");
    }
    config_ = it->second;
    rest_client_ = std::make_unique<RestClient>(config_.rest_base_url);
    auth_ = std::make_unique<OkxAuth>(config_.api_key, config_.secret_key);
    rest_ = std::make_unique<OkxRest>(*rest_client_, *auth_);
    ws_client_ = std::make_unique<ExchangeWsClient>(config_.ws_base_url);
    ws_ = std::make_unique<OkxWs>(config_.ws_base_url, *ws_client_);
    build_pair_map();

    ws_client_->set_on_connect([this]() { ws_->on_connected(); });
}

void OkxAdapter::build_pair_map() {
    // OKX uses "BTC-USDT" format — same as our canonical format
    pair_map_["BTC-USDT"] = "BTC-USDT";
    pair_map_["ETH-USDT"] = "ETH-USDT";
    pair_map_["SOL-USDT"] = "SOL-USDT";
    pair_map_["XRP-USDT"] = "XRP-USDT";
    pair_map_["ADA-USDT"] = "ADA-USDT";
    pair_map_["DOGE-USDT"] = "DOGE-USDT";
    pair_map_["DOT-USDT"] = "DOT-USDT";
    pair_map_["LINK-USDT"] = "LINK-USDT";
    pair_map_["AVAX-USDT"] = "AVAX-USDT";
    pair_map_["ARB-USDT"] = "ARB-USDT";
    pair_map_["OP-USDT"] = "OP-USDT";
}

std::string OkxAdapter::normalize_pair(const std::string& canonical_pair) const {
    auto it = pair_map_.find(canonical_pair);
    if (it != pair_map_.end()) return it->second;
    // OKX native format is same as canonical: "BASE-QUOTE"
    return canonical_pair;
}

std::string OkxAdapter::canonical_pair(const std::string& native_pair) const {
    // OKX uses "BTC-USDT" which is already canonical
    return native_pair;
}

OrderBookSnapshot OkxAdapter::fetch_order_book(const std::string& pair, int depth) {
    auto snap = rest_->fetch_order_book(normalize_pair(pair), depth);
    snap.pair = pair;
    return snap;
}

std::vector<std::pair<std::string, double>>
OkxAdapter::fetch_top_pairs_by_volume(int limit) {
    auto tickers = rest_->fetch_tickers();
    std::vector<std::pair<std::string, double>> result;
    for (auto& [inst_id, volume] : tickers) {
        std::string canonical = canonical_pair(inst_id);
        if (canonical.find("-USDT") != std::string::npos) {
            result.push_back({canonical, volume});
        }
    }
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (static_cast<int>(result.size()) > limit)
        result.resize(limit);
    return result;
}

FeeInfo OkxAdapter::fetch_fees(const std::string& pair) {
    auto info = rest_->fetch_trade_fee(normalize_pair(pair));
    info.pair = pair;
    return info;
}

OrderResult OkxAdapter::place_limit_order(const OrderRequest& req) {
    std::string side_str = req.side == Side::BUY ? "buy" : "sell";
    return rest_->place_order(normalize_pair(req.pair), side_str, req.price, req.quantity);
}

OrderResult OkxAdapter::cancel_order(const std::string& pair, const std::string& order_id) {
    return rest_->cancel_order(normalize_pair(pair), order_id);
}

OrderResult OkxAdapter::query_order(const std::string& pair, const std::string& order_id) {
    return rest_->query_order(normalize_pair(pair), order_id);
}

std::vector<BalanceInfo> OkxAdapter::fetch_balances() {
    return rest_->fetch_balances();
}

void OkxAdapter::subscribe_order_book(const std::string& pair, OrderBookCallback cb) {
    std::string native = normalize_pair(pair);
    ws_->subscribe_depth(native, [pair, cb](const OrderBookSnapshot& snap) {
        OrderBookSnapshot canonical_snap = snap;
        canonical_snap.pair = pair;
        cb(canonical_snap);
    });
}

void OkxAdapter::unsubscribe_order_book(const std::string& pair) {
    ws_->unsubscribe_depth(normalize_pair(pair));
}

void OkxAdapter::connect() {
    ws_client_->connect();
    LOG_INFO("OKX adapter connected");
}

void OkxAdapter::disconnect() {
    ws_client_->disconnect();
    LOG_INFO("OKX adapter disconnected");
}

bool OkxAdapter::is_connected() const { return ws_client_->is_connected(); }
