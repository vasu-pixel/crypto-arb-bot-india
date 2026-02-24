#include "exchange/mexc/mexc_adapter.h"
#include "common/logger.h"
#include <algorithm>
#include <stdexcept>

MexcAdapter::MexcAdapter(const Config& config) {
    auto it = config.exchanges.find(Exchange::MEXC);
    if (it == config.exchanges.end()) {
        throw std::runtime_error("MexcAdapter: no exchange config for MEXC");
    }
    config_ = it->second;
    rest_client_ = std::make_unique<RestClient>(config_.rest_base_url);
    auth_ = std::make_unique<MexcAuth>(config_.api_key, config_.secret_key);
    rest_ = std::make_unique<MexcRest>(*rest_client_, *auth_);
    ws_client_ = std::make_unique<ExchangeWsClient>(config_.ws_base_url);
    ws_ = std::make_unique<MexcWs>(config_.ws_base_url, *ws_client_);
    build_pair_map();

    ws_client_->set_on_connect([this]() { ws_->on_connected(); });
}

void MexcAdapter::build_pair_map() {
    // MEXC uses "BTCUSDT" format (no separator)
    pair_map_["BTC-USDT"] = "BTCUSDT";
    pair_map_["ETH-USDT"] = "ETHUSDT";
    pair_map_["SOL-USDT"] = "SOLUSDT";
    pair_map_["XRP-USDT"] = "XRPUSDT";
    pair_map_["ADA-USDT"] = "ADAUSDT";
    pair_map_["DOGE-USDT"] = "DOGEUSDT";
    pair_map_["DOT-USDT"] = "DOTUSDT";
    pair_map_["LINK-USDT"] = "LINKUSDT";
    pair_map_["AVAX-USDT"] = "AVAXUSDT";
    pair_map_["ARB-USDT"] = "ARBUSDT";
    pair_map_["OP-USDT"] = "OPUSDT";
}

std::string MexcAdapter::normalize_pair(const std::string& canonical_pair) const {
    auto it = pair_map_.find(canonical_pair);
    if (it != pair_map_.end()) return it->second;
    // Fallback: strip hyphens
    std::string native = canonical_pair;
    native.erase(std::remove(native.begin(), native.end(), '-'), native.end());
    return native;
}

std::string MexcAdapter::canonical_pair(const std::string& native_pair) const {
    // Reverse lookup
    for (const auto& [canonical, native] : pair_map_) {
        if (native == native_pair) return canonical;
    }
    // Fallback: insert hyphen before "USDT"
    auto pos = native_pair.find("USDT");
    if (pos != std::string::npos && pos > 0) {
        return native_pair.substr(0, pos) + "-USDT";
    }
    return native_pair;
}

OrderBookSnapshot MexcAdapter::fetch_order_book(const std::string& pair, int depth) {
    auto snap = rest_->fetch_order_book(normalize_pair(pair), depth);
    snap.pair = pair;
    return snap;
}

std::vector<std::pair<std::string, double>>
MexcAdapter::fetch_top_pairs_by_volume(int limit) {
    auto tickers = rest_->fetch_tickers();
    std::vector<std::pair<std::string, double>> result;
    for (auto& [symbol, volume] : tickers) {
        std::string canonical = canonical_pair(symbol);
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

FeeInfo MexcAdapter::fetch_fees(const std::string& pair) {
    auto info = rest_->fetch_trade_fee(normalize_pair(pair));
    info.pair = pair;
    return info;
}

OrderResult MexcAdapter::place_limit_order(const OrderRequest& req) {
    std::string side_str = req.side == Side::BUY ? "BUY" : "SELL";
    return rest_->place_order(normalize_pair(req.pair), side_str, req.price, req.quantity);
}

OrderResult MexcAdapter::cancel_order(const std::string& pair, const std::string& order_id) {
    return rest_->cancel_order(normalize_pair(pair), order_id);
}

OrderResult MexcAdapter::query_order(const std::string& pair, const std::string& order_id) {
    return rest_->query_order(normalize_pair(pair), order_id);
}

std::vector<BalanceInfo> MexcAdapter::fetch_balances() {
    return rest_->fetch_balances();
}

void MexcAdapter::subscribe_order_book(const std::string& pair, OrderBookCallback cb) {
    std::string native = normalize_pair(pair);
    ws_->subscribe_depth(native, [pair, cb](const OrderBookSnapshot& snap) {
        OrderBookSnapshot canonical_snap = snap;
        canonical_snap.pair = pair;
        cb(canonical_snap);
    });
}

void MexcAdapter::unsubscribe_order_book(const std::string& pair) {
    ws_->unsubscribe_depth(normalize_pair(pair));
}

void MexcAdapter::connect() {
    ws_client_->connect();
    LOG_INFO("MEXC adapter connected");
}

void MexcAdapter::disconnect() {
    ws_client_->disconnect();
    LOG_INFO("MEXC adapter disconnected");
}

bool MexcAdapter::is_connected() const { return ws_client_->is_connected(); }
