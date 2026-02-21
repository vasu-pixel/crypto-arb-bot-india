#include "exchange/kraken/kraken_adapter.h"
#include "common/logger.h"
#include <algorithm>
#include <stdexcept>

KrakenAdapter::KrakenAdapter(const Config& config) {
    auto it = config.exchanges.find(Exchange::KRAKEN);
    if (it == config.exchanges.end()) {
        throw std::runtime_error("KrakenAdapter: no exchange config for KRAKEN");
    }
    config_ = it->second;
    rest_client_ = std::make_unique<RestClient>(config_.rest_base_url);
    auth_ = std::make_unique<KrakenAuth>(config_.api_key, config_.secret_key);
    rest_ = std::make_unique<KrakenRest>(*rest_client_, *auth_);
    ws_client_ = std::make_unique<ExchangeWsClient>(config_.ws_base_url);
    ws_ = std::make_unique<KrakenWs>(config_.ws_base_url, *ws_client_);
    build_pair_maps();
}

void KrakenAdapter::build_pair_maps() {
    // REST pair names (Kraken native)
    rest_pair_map_["BTC-USD"] = "XBTUSD";
    rest_pair_map_["ETH-USD"] = "ETHUSD";
    rest_pair_map_["SOL-USD"] = "SOLUSD";
    rest_pair_map_["XRP-USD"] = "XRPUSD";
    rest_pair_map_["ADA-USD"] = "ADAUSD";
    rest_pair_map_["DOGE-USD"] = "DOGEUSD";
    rest_pair_map_["DOT-USD"] = "DOTUSD";
    rest_pair_map_["LINK-USD"] = "LINKUSD";
    rest_pair_map_["AVAX-USD"] = "AVAXUSD";
    rest_pair_map_["MATIC-USD"] = "MATICUSD";

    // WS v2 uses "BTC/USD" format
    ws_pair_map_["BTC-USD"] = "BTC/USD";
    ws_pair_map_["ETH-USD"] = "ETH/USD";
    ws_pair_map_["SOL-USD"] = "SOL/USD";
    ws_pair_map_["XRP-USD"] = "XRP/USD";
    ws_pair_map_["ADA-USD"] = "ADA/USD";
    ws_pair_map_["DOGE-USD"] = "DOGE/USD";
    ws_pair_map_["DOT-USD"] = "DOT/USD";
    ws_pair_map_["LINK-USD"] = "LINK/USD";
    ws_pair_map_["AVAX-USD"] = "AVAX/USD";
    ws_pair_map_["MATIC-USD"] = "MATIC/USD";
}

std::string KrakenAdapter::normalize_pair(const std::string& canonical_pair) const {
    auto it = rest_pair_map_.find(canonical_pair);
    if (it != rest_pair_map_.end()) return it->second;
    // Fallback: replace '-' with nothing for REST
    std::string result = canonical_pair;
    result.erase(std::remove(result.begin(), result.end(), '-'), result.end());
    return result;
}

std::string KrakenAdapter::canonical_pair(const std::string& native_pair) const {
    // Check REST map
    for (auto& [canonical, native] : rest_pair_map_) {
        if (native == native_pair) return canonical;
    }
    // Check WS map
    for (auto& [canonical, ws_name] : ws_pair_map_) {
        if (ws_name == native_pair) return canonical;
    }
    // Heuristic for "BTC/USD" format
    auto slash_pos = native_pair.find('/');
    if (slash_pos != std::string::npos) {
        return native_pair.substr(0, slash_pos) + "-" + native_pair.substr(slash_pos + 1);
    }
    return native_pair;
}

OrderBookSnapshot KrakenAdapter::fetch_order_book(const std::string& pair, int depth) {
    auto snap = rest_->fetch_order_book(normalize_pair(pair), depth);
    snap.pair = pair; // canonical
    return snap;
}

std::vector<std::pair<std::string, double>> KrakenAdapter::fetch_top_pairs_by_volume(int limit) {
    auto tickers = rest_->fetch_all_tickers();
    std::vector<std::pair<std::string, double>> result;
    for (auto& [native, volume] : tickers) {
        std::string canonical = canonical_pair(native);
        if (canonical.find("-USD") != std::string::npos) {
            result.push_back({canonical, volume});
        }
    }
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (static_cast<int>(result.size()) > limit) result.resize(limit);
    return result;
}

FeeInfo KrakenAdapter::fetch_fees(const std::string& pair) {
    auto info = rest_->fetch_trade_fees(normalize_pair(pair));
    info.pair = pair;
    return info;
}

OrderResult KrakenAdapter::place_limit_order(const OrderRequest& req) {
    std::string side_str = req.side == Side::BUY ? "buy" : "sell";
    return rest_->place_order(normalize_pair(req.pair), side_str, "limit",
                              req.price, req.quantity);
}

OrderResult KrakenAdapter::cancel_order(const std::string& pair, const std::string& order_id) {
    return rest_->cancel_order(order_id);
}

OrderResult KrakenAdapter::query_order(const std::string& pair, const std::string& order_id) {
    return rest_->query_order(order_id);
}

std::vector<BalanceInfo> KrakenAdapter::fetch_balances() {
    return rest_->fetch_balances();
}

void KrakenAdapter::subscribe_order_book(const std::string& pair, OrderBookCallback cb) {
    auto it = ws_pair_map_.find(pair);
    std::string ws_pair = (it != ws_pair_map_.end()) ? it->second : pair;

    ws_->subscribe_depth(ws_pair, [this, pair, cb](const OrderBookSnapshot& snap) {
        OrderBookSnapshot canonical_snap = snap;
        canonical_snap.pair = pair;
        cb(canonical_snap);
    });
}

void KrakenAdapter::unsubscribe_order_book(const std::string& pair) {
    auto it = ws_pair_map_.find(pair);
    std::string ws_pair = (it != ws_pair_map_.end()) ? it->second : pair;
    ws_->unsubscribe_depth(ws_pair);
}

void KrakenAdapter::connect() {
    ws_client_->connect();
    LOG_INFO("Kraken adapter connected");
}

void KrakenAdapter::disconnect() {
    ws_client_->disconnect();
    LOG_INFO("Kraken adapter disconnected");
}

bool KrakenAdapter::is_connected() const {
    return ws_client_->is_connected();
}
