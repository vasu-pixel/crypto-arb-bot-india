#pragma once

#include "common/types.h"
#include "common/config.h"

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <utility>

class IExchange {
public:
    virtual ~IExchange() = default;

    // ── Identity ────────────────────────────────────────────────────────
    virtual Exchange exchange_id() const = 0;
    virtual std::string exchange_name() const = 0;

    // ── Symbol mapping ──────────────────────────────────────────────────
    // Convert canonical pair (e.g. "BTC-USD") to the exchange's native format.
    virtual std::string normalize_pair(const std::string& canonical_pair) const = 0;
    // Convert exchange-native pair back to canonical "BASE-QUOTE" form.
    virtual std::string canonical_pair(const std::string& native_pair) const = 0;

    // ── Market data ─────────────────────────────────────────────────────
    virtual OrderBookSnapshot fetch_order_book(const std::string& pair, int depth = 20) = 0;
    virtual std::vector<std::pair<std::string, double>> fetch_top_pairs_by_volume(int limit = 10) = 0;
    virtual FeeInfo fetch_fees(const std::string& pair) = 0;

    // ── Trading ─────────────────────────────────────────────────────────
    virtual OrderResult place_limit_order(const OrderRequest& request) = 0;
    virtual OrderResult cancel_order(const std::string& pair, const std::string& order_id) = 0;
    virtual OrderResult query_order(const std::string& pair, const std::string& order_id) = 0;

    // ── Account ─────────────────────────────────────────────────────────
    virtual std::vector<BalanceInfo> fetch_balances() = 0;

    // ── WebSocket streaming ─────────────────────────────────────────────
    using OrderBookCallback = std::function<void(const OrderBookSnapshot&)>;

    virtual void subscribe_order_book(const std::string& pair, OrderBookCallback callback) = 0;
    virtual void unsubscribe_order_book(const std::string& pair) = 0;

    // ── Connection lifecycle ────────────────────────────────────────────
    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;
};
