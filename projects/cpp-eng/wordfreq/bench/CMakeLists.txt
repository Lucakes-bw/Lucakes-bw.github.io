FetchContent_Declare(benchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.9.4.tar.gz
    URL_HASH SHA256=b334658edd35efcf06a99d9be21e4e93e092bd5f95074c1673d5c8705d95c104
    SYSTEM)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)       # 不构建 benchmark 自己的测试
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

add_executable(wordfreq_bench counter_bench.cpp)
target_link_libraries(wordfreq_bench PRIVATE wordfreq::wordfreq benchmark::benchmark)

add_executable(gentext gentext.cpp)
