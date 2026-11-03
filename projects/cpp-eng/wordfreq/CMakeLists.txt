cmake_minimum_required(VERSION 3.25...3.31)
project(wordfreq VERSION 1.0.0 DESCRIPTION "统计文本中出现次数最多的单词" LANGUAGES CXX)

# 只有自己是顶层项目时才打开测试，被别的项目用 add_subdirectory 引入时不构建
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_EXTENSIONS OFF)                   # -std=c++17，而不是 -std=gnu++17
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)           # 生成 compile_commands.json，给 clang-tidy 和编辑器用
endif()

option(WORDFREQ_BUILD_TESTS "构建单元测试" ${PROJECT_IS_TOP_LEVEL})
option(WORDFREQ_BUILD_BENCHMARKS "构建性能测试" OFF)
option(WORDFREQ_WARNINGS_AS_ERRORS "把警告当作错误" OFF)
set(WORDFREQ_SANITIZE "" CACHE STRING "打开的 Sanitizer，比如 address,undefined 或 thread")
option(WORDFREQ_COVERAGE "统计测试覆盖率（Clang）" OFF)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(Warnings)
include(Sanitizers)
include(Dependencies)

add_subdirectory(src)
add_subdirectory(app)

if(WORDFREQ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(WORDFREQ_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()
