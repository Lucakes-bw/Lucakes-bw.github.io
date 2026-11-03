FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    SYSTEM)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)          # Windows：和我们的程序使用同一种运行库
FetchContent_MakeAvailable(googletest)

add_executable(wordfreq_tests
    tokenizer_test.cpp
    counter_test.cpp
)
target_link_libraries(wordfreq_tests PRIVATE wordfreq::wordfreq GTest::gtest_main)
wordfreq_set_warnings(wordfreq_tests)

include(GoogleTest)
gtest_discover_tests(wordfreq_tests)      # 每个 TEST 都注册成一个 CTest 测试

# 命令行程序的测试：运行 wordfreq，用正则表达式检查输出
add_test(NAME cli.counts COMMAND wordfreq_cli -n 2 ${CMAKE_CURRENT_SOURCE_DIR}/data/dickens.txt)
set_tests_properties(cli.counts PROPERTIES
    PASS_REGULAR_EXPRESSION "单词 24 个，不同的 10 个\n +1  it +4  +16.67%")

add_test(NAME cli.rejects_zero COMMAND wordfreq_cli -n 0 ${CMAKE_CURRENT_SOURCE_DIR}/data/dickens.txt)
set_tests_properties(cli.rejects_zero PROPERTIES WILL_FAIL TRUE)    # 退出码不是 0 才算通过

add_test(NAME cli.missing_file COMMAND wordfreq_cli no-such-file.txt)
set_tests_properties(cli.missing_file PROPERTIES WILL_FAIL TRUE)
