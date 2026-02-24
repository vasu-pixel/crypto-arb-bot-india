#pragma once

#include "common/types.h"
#include "common/config.h"
#include "exchange/exchange_interface.h"

#include <memory>

// Forward declarations for exchange adapters
class BinanceAdapter;
class OkxAdapter;
class BybitAdapter;
class MexcAdapter;
class GateioAdapter;

class ExchangeFactory {
public:
    static std::unique_ptr<IExchange> create(Exchange exchange, const Config& config);
};
