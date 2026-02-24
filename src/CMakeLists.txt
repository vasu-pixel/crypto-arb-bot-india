# -- Common library --
add_library(common STATIC
    common/config.cpp
    common/logger.cpp
    common/time_utils.cpp
    common/crypto_utils.cpp
)
target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(common PUBLIC
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    OpenSSL::Crypto
)

# -- Exchange library --
add_library(exchange STATIC
    exchange/rest_client.cpp
    exchange/ws_client.cpp
    exchange/exchange_factory.cpp
    exchange/binance/binance_auth.cpp
    exchange/binance/binance_rest.cpp
    exchange/binance/binance_ws.cpp
    exchange/binance/binance_adapter.cpp
    exchange/okx/okx_auth.cpp
    exchange/okx/okx_rest.cpp
    exchange/okx/okx_ws.cpp
    exchange/okx/okx_adapter.cpp
    exchange/bybit/bybit_auth.cpp
    exchange/bybit/bybit_rest.cpp
    exchange/bybit/bybit_ws.cpp
    exchange/bybit/bybit_adapter.cpp
    exchange/mexc/mexc_auth.cpp
    exchange/mexc/mexc_rest.cpp
    exchange/mexc/mexc_ws.cpp
    exchange/mexc/mexc_adapter.cpp
    exchange/gateio/gateio_auth.cpp
    exchange/gateio/gateio_rest.cpp
    exchange/gateio/gateio_ws.cpp
    exchange/gateio/gateio_adapter.cpp
)
target_link_libraries(exchange PUBLIC
    common
    CURL::libcurl
    websocketpp
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

# -- OrderBook library --
add_library(orderbook STATIC
    orderbook/order_book.cpp
    orderbook/order_book_aggregator.cpp
    orderbook/depth_analyzer.cpp
)
target_link_libraries(orderbook PUBLIC common)

# -- Strategy library --
add_library(strategy STATIC
    strategy/fee_manager.cpp
)
target_link_libraries(strategy PUBLIC orderbook exchange)

# -- Execution library --
add_library(execution STATIC
    execution/execution_engine.cpp
    execution/order_manager.cpp
    execution/inventory_tracker.cpp
    execution/paper_executor.cpp
)
target_link_libraries(execution PUBLIC exchange strategy)

# -- Persistence library --
add_library(persistence STATIC
    persistence/trade_logger.cpp
)
target_link_libraries(persistence PUBLIC common)

# -- Backtest library --
add_library(backtest STATIC
    backtest/backtest_engine.cpp
    backtest/data_loader.cpp
    backtest/simulated_exchange.cpp
    backtest/backtest_report.cpp
)
target_link_libraries(backtest PUBLIC execution persistence)

# -- WS Server library --
add_library(ws_server_lib STATIC
    server/ws_server.cpp
    server/broadcast_queue.cpp
    server/message_types.cpp
)
target_link_libraries(ws_server_lib PUBLIC
    common
    websocketpp
    Threads::Threads
)

# -- Main executable --
add_executable(arb_bot main.cpp)
target_link_libraries(arb_bot PRIVATE
    common exchange orderbook strategy execution backtest persistence ws_server_lib
)
