#include "exchange/exchange_factory.h"
#include "common/logger.h"
#include "exchange/binance/binance_adapter.h"
#include "exchange/bybit/bybit_adapter.h"
#include "exchange/okx/okx_adapter.h"
#include "exchange/mexc/mexc_adapter.h"
#include "exchange/gateio/gateio_adapter.h"

#include <stdexcept>

std::unique_ptr<IExchange> ExchangeFactory::create(Exchange exchange,
                                                   const Config &config) {
  switch (exchange) {
  case Exchange::BINANCE:
    LOG_INFO("ExchangeFactory: creating BinanceAdapter");
    return std::make_unique<BinanceAdapter>(config);

  case Exchange::OKX:
    LOG_INFO("ExchangeFactory: creating OkxAdapter");
    return std::make_unique<OkxAdapter>(config);

  case Exchange::BYBIT:
    LOG_INFO("ExchangeFactory: creating BybitAdapter");
    return std::make_unique<BybitAdapter>(config);

  case Exchange::MEXC:
    LOG_INFO("ExchangeFactory: creating MexcAdapter");
    return std::make_unique<MexcAdapter>(config);

  case Exchange::GATEIO:
    LOG_INFO("ExchangeFactory: creating GateioAdapter");
    return std::make_unique<GateioAdapter>(config);

  default:
    throw std::runtime_error("ExchangeFactory: unsupported exchange: " +
                             std::string(exchange_to_string(exchange)));
  }
}
