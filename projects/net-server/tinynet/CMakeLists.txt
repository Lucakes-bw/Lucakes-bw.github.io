cmake_minimum_required(VERSION 3.25...3.31)
project(tinynet VERSION 1.0.0 DESCRIPTION "一个小型的 Reactor 网络库（Linux）" LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "tinynet 使用 epoll、eventfd、timerfd，只能在 Linux 上构建（可以用 WSL 或 Docker）")
endif()

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

option(TINYNET_BUILD_TESTS "构建单元测试" ${PROJECT_IS_TOP_LEVEL})
option(TINYNET_BUILD_EXAMPLES "构建示例程序" ${PROJECT_IS_TOP_LEVEL})
option(TINYNET_WARNINGS_AS_ERRORS "把警告当作错误" OFF)
set(TINYNET_SANITIZE "" CACHE STRING "打开的 Sanitizer，比如 address,undefined 或 thread")

include(cmake/Warnings.cmake)
if(TINYNET_SANITIZE)
    add_compile_options(-fsanitize=${TINYNET_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=${TINYNET_SANITIZE})
endif()

find_package(Threads REQUIRED)

add_library(tinynet
    src/Acceptor.cpp src/Buffer.cpp src/Channel.cpp src/EventLoop.cpp src/EventLoopThreadPool.cpp
    src/InetAddress.cpp src/Poller.cpp src/Sockets.cpp src/TcpConnection.cpp src/TcpServer.cpp src/TimerQueue.cpp
)
add_library(tinynet::tinynet ALIAS tinynet)
target_include_directories(tinynet PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_features(tinynet PUBLIC cxx_std_17)
target_link_libraries(tinynet PUBLIC Threads::Threads)     # 头文件中用到了 std::thread、std::mutex
tinynet_set_warnings(tinynet)

add_library(tinynet_http src/http/HttpParser.cpp src/http/HttpResponse.cpp src/http/HttpServer.cpp)
add_library(tinynet::http ALIAS tinynet_http)
target_link_libraries(tinynet_http PUBLIC tinynet::tinynet)
tinynet_set_warnings(tinynet_http)

if(TINYNET_BUILD_EXAMPLES)
    add_executable(echo examples/echo.cpp)
    target_link_libraries(echo PRIVATE tinynet::tinynet)
    add_executable(http_server examples/http_server.cpp)
    target_link_libraries(http_server PRIVATE tinynet::http)
    tinynet_set_warnings(echo)
    tinynet_set_warnings(http_server)
    add_executable(thread_per_conn bench/thread_per_conn.cpp)    # 压测的对照组
    target_link_libraries(thread_per_conn PRIVATE Threads::Threads)
endif()

if(TINYNET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
