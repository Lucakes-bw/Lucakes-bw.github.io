# wordfreq

统计文本中出现次数最多的单词。《C/C++ → C++工程实践》一章的示例项目，演示：

- CMake 的目标与使用要求、CMakePresets（第 1 篇）
- 用 find_package 和 FetchContent 引入 fmt、CLI11（第 2 篇）
- GoogleTest + CTest 单元测试（第 3 篇）
- Sanitizer 与测试覆盖率（第 4 篇）
- Google Benchmark 基准测试（第 5 篇）
- clang-format、clang-tidy、GitHub Actions（第 6 篇）

## 构建

需要 CMake 3.25 以上、支持 C++17 的编译器。第一次配置会下载 CLI11、GoogleTest（和没有安装时的 fmt）。

```bash
cmake --list-presets          # 列出所有预设：debug、release、asan、tsan、coverage
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/app/wordfreq -n 10 some.txt
```

性能测试：

```bash
cmake --preset release && cmake --build --preset release
./build/release/bench/wordfreq_bench
./build/release/bench/gentext 50000000 > big.txt     # 生成 50 MB 的测试文本
```

## 目录

```
CMakeLists.txt  CMakePresets.json
cmake/          Warnings.cmake、Sanitizers.cmake、Dependencies.cmake
include/        库的公开头文件
src/            库的实现
app/            命令行程序
tests/          单元测试和命令行测试
bench/          基准测试和测试文本生成器
.github/        持续集成（作为独立仓库使用时生效）
```
