#include "exchange/gateio/gateio_adapter.h"
#include "common/logger.h"
#include <algorithm>
#include <stdexcept>

GateioAdapter::GateioAdapter(const Config& config) {
    auto it = config.exchanges.find(Exchange::GATEIO);
    if (it == config.exchanges.end()) {
        throw std::runtime_error("GateioAdapter: no exchange config for GATEIO");
    }
    config_ = it->second;
    rest_client_ = std::make_unique<RestClient>(config_.rest_base_url);
    auth_ = std::make_unique<GateioAuth>(config_.api_key, config_.secret_key);
    rest_ = std::make_unique<GateioRest>(*rest_client_, *auth_);
    ws_client_ = std::make_unique<ExchangeWsClient>(config_.ws_base_url);
    ws_ = std::make_unique<GateioWs>(config_.ws_base_url, *ws_client_);
    build_pair_map();

    ws_client_->set_on_connect([this]() { ws_->on_connected(); });
}

void GateioAdapter::build_pair_map() {
    // Gate.io uses "BTC_USDT" format (underscore separator)
    pair_map_["BTC-USDT"] = "BTC_USDT";
    pair_map_["ETH-USDT"] = "ETH_USDT";
    pair_map_["SOL-USDT"] = "SOL_USDT";
    pair_map_["XRP-USDT"] = "XRP_USDT";
    pair_map_["ADA-USDT"] = "ADA_USDT";
    pair_map_["DOGE-USDT"] = "DOGE_USDT";
    pair_map_["DOT-USDT"] = "DOT_USDT";
    pair_map_["LINK-USDT"] = "LINK_USDT";
    pair_map_["AVAX-USDT"] = "AVAX_USDT";
    pair_map_["ARB-USDT"] = "ARB_USDT";
    pair_map_["OP-USDT"] = "OP_USDT";
}

std::string GateioAdapter::normalize_pair(const std::string& canonical_pair) const {
    auto it = pair_map_.find(canonical_pair);
    if (it != pair_map_.end()) return it->second;
    // Fallback: replace hyphen with underscore
    std::string native = canonical_pair;
    std::replace(native.begin(), native.end(), '-', '_');
    return native;
}

std::string GateioAdapter::canonical_pair(const std::string& native_pair) const {
    // Reverse lookup
    for (const auto& [canonical, native] : pair_map_) {
        if (native == native_pair) return canonical;
    }
    // Fallback: replace underscore with hyphen
    std::string canonical = native_pair;
    std::replace(canonical.begin(), canonical.end(), '_', '-');
    return canonical;
}

OrderBookSnapshot GateioAdapter::fetch_order_book(const std::string& pair, int depth) {
    auto snap = rest_->fetch_order_book(normalize_pair(pair), depth);
    snap.pair = pair;
    return snap;
}

std::vector<std::pair<std::string, double>>
GateioAdapter::fetch_top_pairs_by_volume(int limit) {
    auto tickers = rest_->fetch_tickers();
    std::vector<std::pair<std::string, double>> result;
    for (auto& [native, volume] : tickers) {
        std::string canonical_name = canonical_pair(native);
        if (canonical_name.find("-USDT") != std::string::npos) {
            result.push_back({canonical_name, volume});
        }
    }
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (static_cast<int>(result.size()) > limit)
        result.resize(limit);
    return result;
}

FeeInfo GateioAdapter::fetch_fees(const std::string& pair) {
    auto info = rest_->fetch_trade_fee(normalize_pair(pair));
    info.pair = pair;
    return info;
}

OrderResult GateioAdapter::place_limit_order(const OrderRequest& req) {
    std::string side_str = req.side == Side::BUY ? "buy" : "sell";
    return rest_->place_order(normalize_pair(req.pair), side_str, req.price, req.quantity);
}

OrderResult GateioAdapter::cancel_order(const std::string& pair, const std::string& order_id) {
    return rest_->cancel_order(normalize_pair(pair), order_id);
}

OrderResult GateioAdapter::query_order(const std::string& pair, const std::string& order_id) {
    return rest_->query_order(normalize_pair(pair), order_id);
}

std::vector<BalanceInfo> GateioAdapter::fetch_balances() {
    return rest_->fetch_balances();
}

void GateioAdapter::subscribe_order_book(const std::string& pair, OrderBookCallback cb) {
    std::string native = normalize_pair(pair);
    ws_->subscribe_depth(native, [pair, cb](const OrderBookSnapshot& snap) {
        OrderBookSnapshot canonical_snap = snap;
        canonical_snap.pair = pair;
        cb(canonical_snap);
    });
}

void GateioAdapter::unsubscribe_order_book(const std::string& pair) {
    ws_->unsubscribe_depth(normalize_pair(pair));
}

void GateioAdapter::connect() {
    ws_client_->connect();
    LOG_INFO("Gate.io adapter connected");
}

void GateioAdapter::disconnect() {
    ws_client_->disconnect();
    LOG_INFO("Gate.io adapter disconnected");
}

bool GateioAdapter::is_connected() const { return ws_client_->is_connected(); }
