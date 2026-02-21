include(GoogleTest)

# Helper function to add a test target
function(add_unit_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE
        gtest_main
        ${ARGN}
    )
    gtest_discover_tests(${TEST_NAME})
endfunction()

# Unit tests
add_unit_test(test_order_book      test_order_book.cpp      orderbook)
add_unit_test(test_depth_analyzer  test_depth_analyzer.cpp  orderbook)
add_unit_test(test_spread_detector test_spread_detector.cpp strategy persistence)
add_unit_test(test_trade_logger    test_trade_logger.cpp    persistence)
add_unit_test(test_broadcast_queue test_broadcast_queue.cpp ws_server_lib)
add_unit_test(test_paper_executor  test_paper_executor.cpp  execution strategy persistence)
