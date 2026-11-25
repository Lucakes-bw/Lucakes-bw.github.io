cmake_minimum_required(VERSION 3.16)
project(minilog CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)
find_package(Threads REQUIRED)

add_library(minilog STATIC
  src/pattern.cpp
  src/filter.cpp
  src/sinks.cpp
  src/async_worker.cpp
  src/logger.cpp
)
target_include_directories(minilog PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(minilog PUBLIC Threads::Threads)

enable_testing()

foreach(t test_arch test_pipeline test_async)
  add_executable(${t} tests/${t}.cpp)
  target_link_libraries(${t} PRIVATE minilog)
  add_test(NAME ${t} COMMAND ${t})
endforeach()

foreach(b bench_sink_dispatch bench_async_throughput)
  add_executable(${b} bench/${b}.cpp)
  target_link_libraries(${b} PRIVATE minilog)
endforeach()

add_executable(bench_three_ways bench/bench_three_ways.cpp legacy/monolithic_logger.cpp)
target_link_libraries(bench_three_ways PRIVATE minilog)
