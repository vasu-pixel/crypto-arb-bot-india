#include "exchange/exchange_factory.h"
#include "exchange/binance/binance_adapter.h"
#include "exchange/kraken/kraken_adapter.h"
#include "exchange/coinbase/coinbase_adapter.h"
#include "common/logger.h"

#include <stdexcept>

std::unique_ptr<IExchange> ExchangeFactory::create(Exchange exchange, const Config& config) {
    switch (exchange) {
        case Exchange::BINANCE_US:
            LOG_INFO("ExchangeFactory: creating BinanceAdapter");
            return std::make_unique<BinanceAdapter>(config);

        case Exchange::KRAKEN:
            LOG_INFO("ExchangeFactory: creating KrakenAdapter");
            return std::make_unique<KrakenAdapter>(config);

        case Exchange::COINBASE:
            LOG_INFO("ExchangeFactory: creating CoinbaseAdapter");
            return std::make_unique<CoinbaseAdapter>(config);

        default:
            throw std::runtime_error(
                "ExchangeFactory: unsupported exchange: " + exchange_to_string(exchange));
    }
}
