#pragma once

#include "common/types.h"
#include "common/config.h"
#include "exchange/exchange_interface.h"

#include <memory>

// Forward declarations for exchange adapters
class BinanceAdapter;
class KrakenAdapter;
class CoinbaseAdapter;

class ExchangeFactory {
public:
    static std::unique_ptr<IExchange> create(Exchange exchange, const Config& config);
};
