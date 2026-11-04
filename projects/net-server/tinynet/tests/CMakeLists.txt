include(FetchContent)
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    SYSTEM)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(tinynet_tests
    buffer_test.cpp
    timer_test.cpp
    eventloop_test.cpp
    tcp_server_test.cpp
    http_parser_test.cpp
    http_server_test.cpp
)
target_link_libraries(tinynet_tests PRIVATE tinynet::http GTest::gtest_main)
tinynet_set_warnings(tinynet_tests)

include(GoogleTest)
gtest_discover_tests(tinynet_tests DISCOVERY_TIMEOUT 30)
