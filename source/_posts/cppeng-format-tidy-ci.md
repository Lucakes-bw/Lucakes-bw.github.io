---
title: 代码格式、静态检查与持续集成
date: 2026-11-04 10:50:00
updated: 2026-11-04 10:50:00
categories:
  - C/C++
  - C++工程实践
tags:
  - C++
  - clang-format
  - clang-tidy
  - 持续集成
order: 6
description: 前五篇的检查都要人记得去运行。本文把它们交给机器：clang-format 统一代码格式，clang-tidy 在编译之前发现不必要的复制、移动之后的使用这类问题，GitHub Actions 在每次提交时，在几种系统和编译器上构建、测试，再用 Sanitizer 跑一遍。在 Ubuntu 中按同样的步骤验证时，又发现了两个在 macOS 上没有暴露的问题。最后回顾整章。
disableNunjucks: true
---

> **本文目标**：会写 `.clang-format`，用它格式化代码、检查格式；会写 `.clang-tidy`，读懂它的警告，知道常用的几组检查；会写 GitHub Actions 的工作流，用矩阵在几种环境中构建和测试；知道怎样把这些检查引入一个已有的项目。
>
> **前置知识**：本章前五篇。
>
> **演示环境**：clang-format、clang-tidy 18 和持续集成的步骤在 Docker 的 Ubuntu 24.04 中运行。

## 1. 为什么要交给机器

前五篇做了很多检查：警告当作错误、单元测试、ASan、TSan、覆盖率。它们有一个共同的问题：**要人记得去运行**。赶时间的时候，ASan 那一遍很可能就跳过了；三个人维护一个项目，总有人的电脑上没装 TSan 需要的环境。

另一类问题是代码风格：缩进几个空格、花括号放在哪里、`int* p` 还是 `int *p`。它们不影响程序的正确性，但每个人的习惯不同，代码审查时讨论这些既浪费时间，又容易伤和气。

解决办法都是一样的：**写成配置，交给工具，每次提交时自动运行**。

## 2. clang-format：统一代码格式

### 2.1 配置

在项目根目录放一个 `.clang-format`：

```yaml
# 代码格式：在 Google 风格的基础上改了几处，和本专栏文章中的代码一致
BasedOnStyle: Google
IndentWidth: 4
ColumnLimit: 120
AccessModifierOffset: -4
BreakBeforeBraces: Custom
BraceWrapping:
  AfterFunction: true          # 函数的左花括号另起一行，其他的不另起
AllowShortFunctionsOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: WithoutElse
AllowShortLoopsOnASingleLine: true
DerivePointerAlignment: false
PointerAlignment: Left
IncludeBlocks: Preserve
```

从一个预定义的风格（`LLVM`、`Google`、`Chromium`、`Mozilla`、`WebKit`、`Microsoft`）开始，只改几处和项目习惯不同的地方。选项有一百多个，没有必要一个个去定。**定下来比定成什么样更重要。**

### 2.2 第一次运行

wordfreq 的代码是我手动排版的，看起来已经很整齐了。检查一下：

```bash
clang-format --dry-run src/*.cpp include/wordfreq/*.h app/*.cpp tests/*.cpp bench/*.cpp bench/*.h
```

`--dry-run` 只报告不符合格式的地方，不修改文件。结果有 5 个文件、16 处不符合。看看它想怎么改（`clang-format 文件名` 把格式化以后的内容输出到终端，和原文件比较）：

```diff
--- include/wordfreq/counter.h
+++ -
-    void add(std::string_view text);                    // 切分 text，统计其中的每个单词
-    void addWord(std::string_view word, std::int64_t times = 1);   // 直接加一个已经切分好的单词
-    void merge(const Counter& other);                   // 把另一个 Counter 的结果加进来
+    void add(std::string_view text);                              // 切分 text，统计其中的每个单词
+    void addWord(std::string_view word, std::int64_t times = 1);  // 直接加一个已经切分好的单词
+    void merge(const Counter& other);                             // 把另一个 Counter 的结果加进来
```

```diff
--- app/main.cpp
+++ -
-#include <CLI/CLI.hpp>
 #include <fmt/core.h>
+#include <CLI/CLI.hpp>
```

```diff
--- bench/text_gen.h
+++ -
-        if (p < 6) text += ". ";
-        else if (p < 12) text += ", ";
-        else if (p < 14) text += "\n";
-        else text += ' ';
+        if (p < 6)
+            text += ". ";
+        else if (p < 12)
+            text += ", ";
+        else if (p < 14)
+            text += "\n";
+        else
+            text += ' ';
```

手动对齐的行尾注释，有的对齐了、有的没有，它统一对齐成了一列；`#include` 按字母排序（不区分大小写，`fmt` 排在 `CLI` 前面）；带 `else` 的 `if` 不放在一行上（配置中写的是 `WithoutElse`）。人很难做到这么一致，工具可以。

`-i` 直接修改文件：

```bash
clang-format -i src/*.cpp include/wordfreq/*.h app/*.cpp tests/*.cpp bench/*.cpp bench/*.h
```

### 2.3 日常使用

- **编辑器中保存时自动格式化**。VS Code（C/C++ 或 clangd 扩展）、CLion、Visual Studio 都能识别项目中的 `.clang-format`。这是最省事的方法，写代码时就不用考虑格式了；
- **持续集成中检查**：`clang-format --dry-run --Werror` 在有不符合格式的地方时返回非 0，让检查失败；
- 个别地方确实需要特殊的排版，比如一张手工对齐的表格，用注释关掉格式化：

```cpp
// clang-format off
static const int kTable[] = {
    1,  2,  3,
    10, 20, 30,
};
// clang-format on
```

给已有的项目引入 clang-format 时，第一次格式化会改动很多文件，`git blame` 中这些行都会显示成那一次提交。把这次提交的哈希值写进 `.git-blame-ignore-revs` 文件，再执行 `git config blame.ignoreRevsFile .git-blame-ignore-revs`，`git blame` 就会跳过它（GitHub 的网页也支持这个文件）。

不同版本的 clang-format 对同样的配置，结果可能有细微的差别。团队中最好用同一个大版本，持续集成中也固定版本。

## 3. clang-tidy：静态检查

### 3.1 编译器警告之外

编译器的警告检查的是语言层面的问题：类型转换、未使用的变量。**clang-tidy** 是基于 Clang 的静态检查工具，有几百项检查，能发现更多「能编译、但很可能不对」的写法。

配置文件 `.clang-tidy`：

```yaml
# 静态检查：打开几组常用的检查，再关掉其中和本项目风格冲突、或者误报太多的几项
Checks: >
  bugprone-*,
  performance-*,
  modernize-*,
  readability-*,
  -modernize-use-trailing-return-type,
  -modernize-use-nodiscard,
  -readability-identifier-length,
  -readability-magic-numbers,
  -readability-braces-around-statements,
  -bugprone-easily-swappable-parameters,
  -readability-uppercase-literal-suffix
WarningsAsErrors: ''
HeaderFilterRegex: '(include|src|app)/.*'
```

检查按前缀分组：

| 组 | 检查什么 |
|---|---|
| `bugprone-` | 很可能是 bug 的写法：移动之后再使用、可疑的整数转换、可能逃出的异常…… |
| `performance-` | 不必要的复制、低效的写法 |
| `modernize-` | 可以用更新的语法写得更简单：`nullptr`、范围 for、`emplace_back`…… |
| `readability-` | 可读性：用 `empty()` 判断是否为空、多余的 `else`…… |
| `cppcoreguidelines-` | 《C++ 核心准则》中的规则，比较严格 |

先打开整组（`bugprone-*`），再用 `-` 去掉不适合本项目的几项。比如 `modernize-use-trailing-return-type` 要求把所有函数写成 `auto f() -> int` 的形式，这是一种风格偏好，不是问题；`readability-magic-numbers` 不允许代码中出现数字常量，在 wordfreq 这样的小程序中误报太多。

`HeaderFilterRegex` 决定报告哪些头文件中的问题：只报告自己的头文件，不报告标准库和第三方库的。

clang-tidy 需要知道每个文件是怎样编译的，否则连 `#include` 都找不到。这就是第一篇生成 `compile_commands.json` 的原因：

```bash
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++
clang-tidy -p build src/*.cpp app/main.cpp      # -p：compile_commands.json 所在的目录
```

### 3.2 它能发现什么

下面是 wordfreq 命令行程序的一个早期草稿，能编译，`-Wall -Wextra` 也没有警告：

```cpp
void printReport(wordfreq::Counter counter, std::size_t topN)
{
    std::vector<wordfreq::Entry> top = counter.top(topN);
    if (top.size() == 0) {
        std::cout << "没有单词" << std::endl;
        return;
    }
    int rank = 0;
    for (auto e : top) {
        double percent = 100.0 * static_cast<double>(e.count) / static_cast<double>(counter.total());
        std::cout << ++rank << "  " << e.word << "  " << percent << "%" << std::endl;
    }
}

int main(int argc, char** argv)
{
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) files.push_back(argv[i]);

    std::vector<std::string> pending = std::move(files);
    std::ostringstream text;
    if (files.empty()) text << std::cin.rdbuf();
    for (std::size_t i = 0; i < pending.size(); i++) {
        std::ifstream in(pending[i]);
        if (in.rdbuf() == NULL) return 1;
        text << in.rdbuf() << '\n';
    }
    // ……
}
```

clang-tidy 报告了 9 处：

```
app/main.cpp:12:36: warning: the parameter 'counter' is copied for each invocation but only used as a const reference; consider making it a const reference [performance-unnecessary-value-param]
   12 | void printReport(wordfreq::Counter counter, std::size_t topN)
      |                                    ^
      |                  const            &
app/main.cpp:15:9: warning: the 'empty' method should be used to check for emptiness instead of 'size' [readability-container-size-empty]
   15 |     if (top.size() == 0) {
      |         ^~~~~~~~~~~~~~~
      |         top.empty()
app/main.cpp:16:40: warning: do not use 'std::endl' with streams; use '\n' instead [performance-avoid-endl]
   16 |         std::cout << "没有单词" << std::endl;
      |                                    ^~~~~~~~~
      |                                    '\n'
app/main.cpp:20:15: warning: loop variable is copied but only used as const reference; consider making it a const reference [performance-for-range-copy]
   20 |     for (auto e : top) {
      |               ^
      |          const  &
app/main.cpp:22:76: warning: do not use 'std::endl' with streams; use '\n' instead [performance-avoid-endl]
app/main.cpp:31:42: warning: use emplace_back instead of push_back [modernize-use-emplace]
   31 |     for (int i = 1; i < argc; ++i) files.push_back(argv[i]);
      |                                          ^~~~~~~~~~
      |                                          emplace_back(
app/main.cpp:35:9: warning: 'files' used after it was moved [bugprone-use-after-move]
   35 |     if (files.empty()) text << std::cin.rdbuf();
      |         ^
app/main.cpp:33:40: note: move occurred here
   33 |     std::vector<std::string> pending = std::move(files);
      |                                        ^
app/main.cpp:36:5: warning: use range-based for loop instead [modernize-loop-convert]
   36 |     for (std::size_t i = 0; i < pending.size(); i++) {
      |     ^   ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
      |         (const auto & i : pending)
app/main.cpp:38:27: warning: use nullptr [modernize-use-nullptr]
   38 |         if (in.rdbuf() == NULL) return 1;
      |                           ^~~~
      |                           nullptr
```

分成三类来看：

**真正的 bug**：`bugprone-use-after-move`。`files` 被移动给了 `pending`，之后又用 `files.empty()` 判断有没有给出文件。被移动以后的 `std::vector` 处于「有效但未指定」的状态，实际上通常是空的，于是即使给出了文件，程序也会去读标准输入，在终端中运行时就卡在那里等你输入。本来想写的是 `pending.empty()`。这种错误编译器不报告，测试也不一定覆盖到（第三篇的命令行测试都给出了文件名，但是测试是在标准输入为空的环境中运行的，读到的是空内容，结果恰好正确）。

**不必要的开销**：`printReport` 按值接收 `Counter`，每次调用都把整个哈希表复制一遍；`for (auto e : top)` 每次循环复制一个 `Entry`（其中有一个 `std::string`）；`std::endl` 除了换行还会刷新缓冲区，循环中每输出一行都要做一次系统调用。

**可以写得更好**：`empty()`、`emplace_back`、范围 for、`nullptr`。

每条警告都给出了修改建议（`^` 下面的那一行）。加上 `-fix` 参数，clang-tidy 会直接修改文件。不过修改以后要看一下：自动修改的结果在语法上是对的，但不一定是最好的写法，比如 `modernize-loop-convert` 建议的循环变量名还是 `i`。

clang-tidy 也不是什么都能发现：`in.rdbuf() == NULL` 这一行，它只建议把 `NULL` 换成 `nullptr`，但这个判断本身就是错的：文件打不开时，`rdbuf()` 并不返回空指针，应该检查 `in.is_open()` 或者 `!in`。逻辑上的错误，还是要靠测试和审查。

### 3.3 在正式的代码上

对正式版本的 wordfreq 运行 clang-tidy，`src/` 中没有问题，`app/main.cpp` 中有一处：

```
app/main.cpp:29:5: error: an exception may be thrown in function 'main' which should not throw exceptions [bugprone-exception-escape,-warnings-as-errors]
   29 | int main(int argc, char** argv)
      |     ^
```

`main` 中调用的函数可能抛出异常：内存不足时的 `std::bad_alloc`，`std::thread` 创建失败时的 `std::system_error`……异常一旦逃出 `main`，程序会调用 `std::terminate` 直接结束，什么有用的信息都不会留下。修改：把原来的 `main` 改名为 `run`，新的 `main` 只负责捕获异常：

```cpp
int main(int argc, char** argv)
{
    // 异常不能逃出 main：那样程序会直接调用 std::terminate，不会输出任何有用的信息
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        fmt::print(stderr, "错误：{}\n", e.what());
    } catch (...) {
        fmt::print(stderr, "错误：未知的异常\n");
    }
    return 1;
}
```

同一次运行中还有一条 `readability-uppercase-literal-suffix`：建议把 `1u` 写成 `1U`。这是风格问题，各个项目的习惯不同，我把它加到了 `.clang-tidy` 关掉的检查中。

确实需要保留某个写法时，可以在那一行加上 `// NOLINT(检查名)`，并写明原因。

### 3.4 引入已有的项目

在一个几万行的老项目上第一次运行 clang-tidy，可能会得到几千条警告。不要试图一次改完：

1. 先只打开 `bugprone-*`，这一组的警告最可能是真正的 bug，逐条看；
2. 在持续集成中只检查**新修改的文件**（clang-tidy 自带的 `clang-tidy-diff.py` 脚本可以只检查 diff 中的行）；
3. 其他几组慢慢打开，每次一组。

## 4. 持续集成：GitHub Actions

### 4.1 每次提交都检查

**持续集成**（CI）是指每次提交代码，都由服务器自动构建、运行测试和各种检查。它解决了「要人记得去运行」的问题，还解决了另一个问题：**只在自己的电脑上试过**。第二篇中 fmt 的警告让 `-Werror` 的构建失败，就是只在 Linux 的 GCC 上才出现的问题。

GitHub Actions 是 GitHub 提供的持续集成服务，在仓库中放一个 `.github/workflows/ci.yml` 就能使用。wordfreq 的工作流分成三部分。

<figure class="diagram">
<svg viewBox="0 0 700 312" role="img" aria-label="wordfreq 的持续集成：每次推送或拉取请求触发三个并行的 job：build-and-test 在 Ubuntu GCC、Ubuntu Clang、macOS Clang 上构建测试；sanitizers 分别用 asan 和 tsan 预设运行测试；lint 检查格式和运行 clang-tidy。全部通过才能合并">
  <defs><marker id="ci-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="ci-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="14" y="98" width="120" height="52" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="74.0" y="120.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">git push</text><text x="74.0" y="136.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">或拉取请求</text><rect x="190" y="14" width="300" height="102" rx="8" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="206.0" y="36.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">build-and-test</text><text x="330.0" y="36.0" text-anchor="start" style="fill:var(--fg);font-size:12px">· Ubuntu + GCC</text><text x="330.0" y="60.0" text-anchor="start" style="fill:var(--fg);font-size:12px">· Ubuntu + Clang</text><text x="330.0" y="84.0" text-anchor="start" style="fill:var(--fg);font-size:12px">· macOS + Clang</text><line x1="134" y1="124" x2="188" y2="65.0" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ci-a)"/><line x1="490" y1="65.0" x2="548" y2="124" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ci-a)"/><rect x="190" y="126" width="300" height="78" rx="8" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="206.0" y="148.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">sanitizers</text><text x="330.0" y="148.0" text-anchor="start" style="fill:var(--fg);font-size:12px">· asan 预设</text><text x="330.0" y="172.0" text-anchor="start" style="fill:var(--fg);font-size:12px">· tsan 预设</text><line x1="134" y1="124" x2="188" y2="165.0" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ci-a)"/><line x1="490" y1="165.0" x2="548" y2="124" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ci-a)"/><rect x="190" y="214" width="300" height="78" rx="8" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="206.0" y="236.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">lint</text><text x="330.0" y="236.0" text-anchor="start" style="fill:var(--fg);font-size:12px">· clang-format</text><text x="330.0" y="260.0" text-anchor="start" style="fill:var(--fg);font-size:12px">· clang-tidy</text><line x1="134" y1="124" x2="188" y2="253.0" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ci-a)"/><line x1="490" y1="253.0" x2="548" y2="124" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ci-a)"/><rect x="550" y="98" width="136" height="52" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="618.0" y="120.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">全部通过</text><text x="618.0" y="136.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">才能合并</text><text x="14.0" y="304.0" text-anchor="start" style="fill:var(--muted);font-size:11px">三个 job、7 台虚拟机并行运行；每一步都用和本地相同的 cmake --preset 命令</text>
  </g>
</svg>
</figure>

### 4.2 构建和测试：矩阵

```yaml
# wordfreq 的持续集成：每次推送和每个拉取请求都运行
name: CI

on:
  push:
  pull_request:

jobs:
  # 在几种系统和编译器上构建，运行全部测试
  build-and-test:
    strategy:
      fail-fast: false                 # 一个组合失败了，其他的继续跑完
      matrix:
        include:
          - { os: ubuntu-24.04, cxx: g++ }
          - { os: ubuntu-24.04, cxx: clang++ }
          - { os: macos-15, cxx: clang++ }
    runs-on: ${{ matrix.os }}
    env:
      CXX: ${{ matrix.cxx }}
    steps:
      - uses: actions/checkout@v4
      - name: 安装 Ninja
        run: ${{ runner.os == 'Linux' && 'sudo apt-get install -y ninja-build' || 'brew install ninja' }}
      - name: 配置
        run: cmake --preset debug -G Ninja
      - name: 编译
        run: cmake --build --preset debug
      - name: 测试
        run: ctest --preset debug
```

- `on`：什么时候运行。这里是每次推送和每个拉取请求；
- `jobs`：要做的事情，每个 job 在一台全新的虚拟机中运行；
- `matrix`：同一个 job 用几组不同的参数各运行一次。这里是 3 个组合：Ubuntu + GCC、Ubuntu + Clang、macOS + Clang。它们是**并行**运行的；
- `env: CXX`：CMake 根据环境变量 `CXX` 选择 C++ 编译器；
- `-G Ninja`：Ninja 比 Make 快，第一篇提到过。预设中没有写死生成器，在命令行指定。

步骤中用的是 `cmake --preset debug`，和自己电脑上的命令完全一样。**持续集成中不要写一套和本地不同的命令**，否则「CI 中失败、本地却通过」的问题会很难查。这正是第一篇把配置写进 `CMakePresets.json` 的另一个好处。

这里没有包括 Windows：wordfreq 的代码没有在 MSVC 上验证过，而 `/W4 /WX` 很可能会报出一些 GCC 和 Clang 没有的警告。要支持 Windows，就在矩阵中加上 `windows-2022`，然后把它报出的问题一个个修好。

### 4.3 Sanitizer

```yaml
  # 用 Sanitizer 运行测试：asan 包括 AddressSanitizer 和 UndefinedBehaviorSanitizer
  sanitizers:
    strategy:
      fail-fast: false
      matrix:
        preset: [asan, tsan]
    runs-on: ubuntu-24.04
    env:
      CXX: g++                         # Ubuntu 的 clang 包不带 Sanitizer 的运行库，GCC 自带
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get install -y ninja-build
      # 较新的 Linux 内核把地址随机化的范围调大了，ThreadSanitizer 会因此启动失败，调回 28 位
      - name: 调整地址随机化
        if: matrix.preset == 'tsan'
        run: sudo sysctl vm.mmap_rnd_bits=28
      - run: cmake --preset ${{ matrix.preset }} -G Ninja
      - run: cmake --build --preset ${{ matrix.preset }}
      - run: ctest --preset ${{ matrix.preset }}
```

ASan 和 TSan 不能同时打开，所以用矩阵分成两个 job。在 Linux 上，ASan 还会检查内存泄漏，这是第四篇在 macOS 上做不到的。

两处注释说明了两个环境问题：

- 最初这个 job 用的是 Clang。在 Ubuntu 24.04 中验证时，链接失败了：

  ```
  /usr/bin/ld: cannot find /usr/lib/llvm-18/lib/clang/18/lib/linux/libclang_rt.tsan-aarch64.a: No such file or directory
  ```

  Ubuntu 把 Clang 的 Sanitizer 运行库放在了单独的 `libclang-rt-18-dev` 包中，默认没有安装。可以装上这个包，也可以改用 GCC，GCC 的运行库随编译器一起安装。这里选了后者；
- 较新的 Linux 内核增大了地址随机化的位数，ThreadSanitizer 需要的地址空间布局会和它冲突，启动时直接报错退出。这是 GitHub 的 ubuntu-24.04 运行环境中已知的问题（我在 Docker 中验证时没有遇到，因为 Docker Desktop 的虚拟机用的是另一个内核），把 `vm.mmap_rnd_bits` 调回 28 是常用的解决办法。

### 4.4 格式和静态检查

```yaml
  # 代码格式和静态检查
  lint:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get install -y ninja-build clang-format clang-tidy
      - name: 检查代码格式
        run: clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.h')
      - name: 静态检查
        run: |
          cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DWORDFREQ_BUILD_TESTS=OFF
          clang-tidy -p build --warnings-as-errors='*' src/*.cpp app/*.cpp
```

`git ls-files` 列出仓库中所有的 `.cpp` 和 `.h`，不会把 `build/` 中下载的第三方代码也算进去。`--warnings-as-errors='*'` 让 clang-tidy 的任何警告都使检查失败。3.3 节的 `bugprone-exception-escape` 就是在这一步发现的。

### 4.5 验证

这个工作流的每一步，都在 Docker 的 Ubuntu 24.04 中按同样的命令运行过，全部通过：

```
[ok]   g++ 配置
[ok]   g++ 编译
[ok]   g++ 测试
[ok]   clang++ 配置
[ok]   clang++ 编译
[ok]   clang++ 测试
[ok]   asan 配置
[ok]   asan 编译
[ok]   asan 测试
[ok]   tsan 配置
[ok]   tsan 编译
[ok]   tsan 测试
[ok]   clang-format
[ok]   tidy 配置
[ok]   clang-tidy
```

wordfreq 放在博客仓库的一个子目录中，而 GitHub 只执行仓库根目录下 `.github/workflows/` 中的工作流，所以它在这里不会运行。下载 [wordfreq.zip](/downloads/wordfreq.zip)，把它作为一个独立的仓库推送到 GitHub，就能在 Actions 页面看到这些 job 运行了。

在仓库设置中打开**分支保护**，要求这些检查通过才能合并拉取请求，「忘记运行测试」就再也不会发生了。

## 5. 回顾整章

| 问题 | 工具 | 在哪一篇 |
|---|---|---|
| 几十个文件怎样组织、怎样编译 | CMake 的目标和使用要求、预设 | 第 1 篇 |
| 怎样使用别人的库 | find_package、FetchContent | 第 2 篇 |
| 改完代码还对不对 | GoogleTest、CTest | 第 3 篇 |
| 测试通过了，有没有隐藏的内存错误和数据竞争 | ASan、UBSan、TSan | 第 4 篇 |
| 测试测到了哪些代码 | llvm-cov | 第 4 篇 |
| 慢在哪里 | Google Benchmark、perf、火焰图 | 第 5 篇 |
| 风格统一、常见的错误写法 | clang-format、clang-tidy | 第 6 篇 |
| 每次都检查、在几种环境中检查 | GitHub Actions | 第 6 篇 |

这些工具和 C++ 的语法无关，但它们决定了一个项目能不能长期、多人地维护下去。对一个新项目，建议从第一天起就把它们都加上：在一个只有几个文件的项目中，配置这些只要一两个小时；等到项目有了几万行代码，再引入 `-Werror`、clang-tidy 和 Sanitizer，面对的可能是成百上千个需要修改的地方。

wordfreq 完整的代码在 [projects/cpp-eng/wordfreq](https://github.com/Lucakes-bw/Lucakes-bw.github.io/tree/main/projects/cpp-eng/wordfreq)，可以作为新项目的模板。

## 6. 常见错误

1. **格式化和逻辑修改放在同一次提交中**。几百行的格式变化中夹着一行真正的修改，审查的人很难看出来。格式化单独提交。
2. **clang-tidy 打开所有的检查**（`Checks: '*'`）。其中包括很多互相矛盾的风格检查（比如有的要求花括号，有的要求省略），警告多到没人看。按组打开，关掉不适合的。
3. **持续集成中的命令和本地不一样**。CI 中失败，本地无法重现。都用预设。
4. **持续集成太慢，大家开始不等它跑完就合并**。用矩阵并行运行；缓存下载的依赖；把很慢的检查（比如性能测试）放到每天运行一次的工作流中。
5. **检查失败时，把检查关掉**。偶尔失败的测试、误报的警告，应该找到原因，修好或者有针对性地屏蔽（`NOLINT` 加上原因），而不是整个关掉。

## 7. 小结

- clang-format 用一个配置文件统一代码格式，编辑器中保存时自动格式化，持续集成中检查；
- clang-tidy 依靠 `compile_commands.json` 检查每个文件，`bugprone-` 一组最可能发现真正的 bug；
- GitHub Actions 用矩阵在几种系统和编译器上并行构建、测试，Sanitizer 和静态检查各自是一个 job；持续集成中的命令和本地相同，都用预设；
- 在干净的环境中验证，能发现只在自己的电脑上不会出现的问题。

## 8. 练习

1. 用 `clang-tidy --list-checks -checks='bugprone-*'` 看看 `bugprone` 组中有哪些检查，挑三个你觉得最有用的。
2. 在工作流中增加一个 job：用 `release` 预设构建并运行一次基准测试，把结果作为构建产物（artifact）上传。为什么不应该用持续集成中的基准测试结果来判断性能有没有变差？
3. 给 `build-and-test` 加上依赖的缓存：用 `actions/cache` 缓存 `build/debug/_deps`，看看第二次运行快了多少。缓存的键（key）应该用什么？

<details>
<summary>点击查看答案</summary>

1. 例如：`bugprone-use-after-move`（移动之后再使用）、`bugprone-dangling-handle`（`string_view` 等指向临时对象，和第四篇 4.1 节的问题相同）、`bugprone-narrowing-conversions`（可能丢失数据的转换）、`bugprone-infinite-loop`（循环条件中的变量在循环体中没有被修改）、`bugprone-suspicious-string-compare`（`if (strcmp(a, b))` 这种容易弄反的判断）。

2.
```yaml
  bench:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get install -y ninja-build
      - run: cmake --preset release -G Ninja && cmake --build --preset release
      - run: ./build/release/bench/wordfreq_bench --benchmark_out=bench.json --benchmark_out_format=json
      - uses: actions/upload-artifact@v4
        with: { name: bench, path: bench.json }
```
持续集成的虚拟机和别人共享物理机器，每次分到的硬件也可能不同，测量的波动往往有百分之十几甚至更多，比大部分优化的效果还大。要监控性能的变化，应该用一台固定的、专用的机器，并且多次测量。

3. 键应该包含所有影响依赖内容的东西：操作系统、编译器，以及声明依赖版本的文件。例如 `key: deps-${{ matrix.os }}-${{ matrix.cxx }}-${{ hashFiles('cmake/Dependencies.cmake', 'tests/CMakeLists.txt') }}`。依赖的版本一改，哈希值就变了，缓存自动失效。

</details>
