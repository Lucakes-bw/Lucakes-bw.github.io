---
title: 用 CMake 组织一个多目录项目
date: 2026-11-04 10:00:00
updated: 2026-11-04 10:00:00
categories:
  - C/C++
  - C++工程实践
tags:
  - C++
  - CMake
  - 构建
order: 1
description: 《开发环境与编程工具》中的 CMake 只有一个目标、两个源文件。真实的项目要把代码分成库和程序，分成好几个目录，还要让测试和性能测试也能用上同一个库。本文从零搭建本章的示例项目 wordfreq，讲清楚目标和「使用要求」、PUBLIC 和 PRIVATE 的区别、怎样集中管理警告选项、构建类型，以及用 CMakePresets.json 把常用的配置固定下来。
disableNunjucks: true
---

> **本文目标**：会把一个项目分成库和程序两个目标；理解 `target_include_directories`、`target_link_libraries` 中 `PUBLIC`、`PRIVATE`、`INTERFACE` 的含义，知道它们是怎样传递的；会用一个函数给自己的目标统一打开警告；理解构建类型；会写 `CMakePresets.json`。
>
> **前置知识**：《开发环境与编程工具》第 6 节（CMake 的基本用法）、《静态库与动态库》。
>
> **演示环境**：macOS + Apple Clang 21 + CMake 4.4。文中的命令在 Linux 和 Windows 上相同。

## 0. 这一章要做什么

前面各章的例子都是一两个文件，写完、编译、运行，就结束了。真实的项目不是这样：代码有几十上百个文件，要在几种平台上编译，要用第三方库，要有测试保证改代码时不出错，要知道慢在哪里，还要几个人一起维护。这些和语言本身无关的事情，统称为**工程实践**。

本章用一个小项目 **wordfreq** 把这些事情串起来。它统计一段英文文本中出现次数最多的单词：

```
$ wordfreq -n 5 dickens.txt
单词 24 个，不同的 10 个
   1  it                    4   16.67%
   2  of                    4   16.67%
   3  the                   4   16.67%
   4  was                   4   16.67%
   5  age                   2    8.33%
```

六篇文章，每篇在上一篇的基础上增加一件事：

| 篇 | 内容 | 用到的工具 |
|---|---|---|
| 1 | 组织多目录项目（本文） | CMake、CMakePresets |
| 2 | 使用第三方库 | find_package、FetchContent |
| 3 | 单元测试 | GoogleTest、CTest |
| 4 | 内存错误、数据竞争和测试覆盖率 | Sanitizer、llvm-cov |
| 5 | 性能分析和优化 | Google Benchmark、perf、火焰图 |
| 6 | 代码格式、静态检查和持续集成 | clang-format、clang-tidy、GitHub Actions |

完整的项目在 [projects/cpp-eng/wordfreq](https://github.com/Lucakes-bw/Lucakes-bw.github.io/tree/main/projects/cpp-eng/wordfreq)，也可以下载 [wordfreq.zip](/downloads/wordfreq.zip)。每篇文章只讲和它相关的部分。

## 1. 目录结构

```
wordfreq/
├── CMakeLists.txt              顶层：项目名、选项、引入各个子目录
├── CMakePresets.json           常用的配置（第 5 节）
├── cmake/
│   └── Warnings.cmake          打开警告的函数（第 3 节）
├── include/wordfreq/           库的公开头文件
│   ├── tokenizer.h             把文本切成单词
│   └── counter.h               统计每个单词出现的次数
├── src/                        库的实现
│   ├── CMakeLists.txt
│   ├── tokenizer.cpp
│   └── counter.cpp
└── app/                        命令行程序
    ├── CMakeLists.txt
    └── main.cpp
```

后面几篇还会加上 `tests/`（单元测试）和 `bench/`（性能测试）。

这个结构有几点值得说明：

**功能放在库里，`main.cpp` 只是一层外壳。** 切分单词、统计次数这些真正的功能都在 `src/` 中，编译成一个库；`app/main.cpp` 只负责读取命令行参数、读文件、输出结果。这样做的原因是：单元测试和性能测试也要调用这些功能，它们链接同一个库就行了，不需要把 `.cpp` 文件再编译一遍，更不需要复制代码。

**公开的头文件放在 `include/wordfreq/` 中。** 使用这个库的代码写的是 `#include "wordfreq/counter.h"`，而不是 `#include "counter.h"`。多一层目录，是为了避免和别的库重名：几乎每个项目都可能有一个 `utils.h` 或者 `config.h`，带上库名作为前缀就不会冲突了。只在库内部使用的头文件，可以直接放在 `src/` 中，不对外公开。

**`build/` 不在源代码目录中。** 所有生成的文件都放在 `build/` 下，源代码目录保持干净，`.gitignore` 中只要写一行 `build/`。

<figure class="diagram">
<svg viewBox="0 0 700 210" role="img" aria-label="wordfreq 的目标：库 wordfreq 编译自 src/，命令行程序 wordfreq_cli、单元测试 wordfreq_tests、性能测试 wordfreq_bench 都链接这个库，源文件不用重复编译">
  <defs><marker id="tg-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="tg-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="240" y="20" width="220" height="52" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="350.0" y="42.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">wordfreq（库）</text><text x="350.0" y="58.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">src/ → libwordfreq.a</text><rect x="20" y="120" width="200" height="52" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="120.0" y="142.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">wordfreq_cli</text><text x="120.0" y="158.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">app/ → 程序 wordfreq</text><line x1="120" y1="120" x2="310" y2="74" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#tg-a)"/><rect x="250" y="120" width="200" height="52" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="350.0" y="142.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">wordfreq_tests</text><text x="350.0" y="158.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">tests/ → 单元测试（第 3 篇）</text><line x1="350" y1="120" x2="350" y2="74" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#tg-a)"/><rect x="480" y="120" width="200" height="52" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="580.0" y="142.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">wordfreq_bench</text><text x="580.0" y="158.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">bench/ → 性能测试（第 5 篇）</text><line x1="580" y1="120" x2="390" y2="74" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#tg-a)"/><text x="240.0" y="100.0" text-anchor="start" style="fill:var(--link);font-size:11px">链接</text><text x="20.0" y="196.0" text-anchor="start" style="fill:var(--muted);font-size:11px">功能只编译一次，放在库中；测试和性能测试在后面几篇加入</text>
  </g>
</svg>
</figure>

## 2. 目标和使用要求

### 2.1 顶层的 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.25...3.31)
project(wordfreq VERSION 1.0.0 DESCRIPTION "统计文本中出现次数最多的单词" LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_EXTENSIONS OFF)                   # -std=c++17，而不是 -std=gnu++17
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)           # 生成 compile_commands.json，给编辑器和检查工具用
endif()

option(WORDFREQ_WARNINGS_AS_ERRORS "把警告当作错误" OFF)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(Warnings)

add_subdirectory(src)
add_subdirectory(app)
```

- `cmake_minimum_required(VERSION 3.25...3.31)`：写成一个范围，意思是「至少要 3.25；3.31 以前引入的新行为全部打开」。CMake 每个版本都会改进一些行为（叫作**策略**，policy），为了不影响旧项目，新行为默认是关闭的。只写一个版本号，那个版本以后的改进你都用不上，有时还会收到一堆「策略没有设置」的警告。
- `PROJECT_IS_TOP_LEVEL`：这个项目可能被别的项目通过 `add_subdirectory` 引入，作为它的一部分。这时语言扩展、是否生成 `compile_commands.json` 应该由外层项目决定，我们不去改全局的设置。
- `option(...)`：定义一个可以在命令行用 `-D` 修改的开关，比如 `cmake -S . -B build -DWORDFREQ_WARNINGS_AS_ERRORS=ON`。选项名都以项目名开头，同样是为了被别的项目引入时不冲突。
- `add_subdirectory(src)`：进入 `src/` 目录，执行那里的 `CMakeLists.txt`。

注意顶层没有设置 C++ 标准，也没有写任何编译选项。这些都写在各个目标上，下面会说明原因。

### 2.2 库：src/CMakeLists.txt

```cmake
add_library(wordfreq
    tokenizer.cpp
    counter.cpp
)
add_library(wordfreq::wordfreq ALIAS wordfreq)

# PUBLIC：wordfreq 自己编译时要用，链接 wordfreq 的目标也要用
target_include_directories(wordfreq PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_features(wordfreq PUBLIC cxx_std_17)

find_package(Threads REQUIRED)
target_link_libraries(wordfreq PRIVATE Threads::Threads)   # PRIVATE：只有 counter.cpp 用到了线程

wordfreq_set_warnings(wordfreq)
```

`add_library(wordfreq ...)` 定义了一个叫 `wordfreq` 的**目标**（target）。没有写 `STATIC` 还是 `SHARED`，默认是静态库，也可以在配置时用 `-DBUILD_SHARED_LIBS=ON` 改成动态库。

`wordfreq::wordfreq` 是它的一个别名。带 `::` 的名字是 CMake 的约定：如果写错了，比如写成 `wordfrq::wordfreq`，CMake 在配置时就会报错「找不到目标」；而不带 `::` 的 `wordfrq` 会被当成一个普通的库文件名，一直到链接时才出错，错误信息也难懂得多。后面几篇用 `find_package` 找到的第三方库，名字也都是这种形式。

### 2.3 程序：app/CMakeLists.txt

```cmake
add_executable(wordfreq_cli main.cpp)
set_target_properties(wordfreq_cli PROPERTIES OUTPUT_NAME wordfreq)   # 生成的程序叫 wordfreq
target_link_libraries(wordfreq_cli PRIVATE wordfreq::wordfreq)
wordfreq_set_warnings(wordfreq_cli)
```

库已经叫 `wordfreq` 了，同一个项目中的目标不能重名，所以程序的目标叫 `wordfreq_cli`，再用 `OUTPUT_NAME` 把生成的文件名改成 `wordfreq`。

这个文件中**没有**告诉 CMake 头文件在哪里，也没有说用 C++17。但是看一下 `main.cpp` 实际的编译命令（`cmake --build build --verbose` 会显示每一条命令）：

```
/usr/bin/c++  -I./include -std=c++17 -arch arm64 -Wall -Wextra -Wpedantic ... -c ./app/main.cpp
```

`-I./include` 和 `-std=c++17` 都有，它们是从 `wordfreq` 那里**传过来**的。

### 2.4 PUBLIC、PRIVATE 和 INTERFACE

`target_include_directories(wordfreq PUBLIC ...)` 中的 `PUBLIC` 说的是：这个头文件目录，不仅 `wordfreq` 自己编译时要用，**所有链接了 `wordfreq` 的目标**编译时也要用。这种「用我的人也需要的东西」叫作**使用要求**（usage requirements）。CMake 中的三个关键字：

| 关键字 | 自己用 | 链接自己的目标也用 | 适用的情况 |
|---|---|---|---|
| `PRIVATE` | ✓ | | 只在 `.cpp` 中用到 |
| `PUBLIC` | ✓ | ✓ | 公开的头文件中用到 |
| `INTERFACE` | | ✓ | 自己不用，只给别人用（比如只有头文件的库） |

判断的方法很简单：**看公开的头文件**。

- `include/wordfreq/` 这个目录：`main.cpp` 要包含其中的头文件，所以是 `PUBLIC`；
- C++17：`counter.h` 中用到了 `std::string_view`，包含 `counter.h` 的代码必须用 C++17 编译，所以 `cxx_std_17` 是 `PUBLIC`；
- 线程库：只有 `counter.cpp` 中用到了 `std::thread`，公开的头文件中没有，所以是 `PRIVATE`。

<figure class="diagram">
<svg viewBox="0 0 700 232" role="img" aria-label="使用要求的传递：wordfreq 的 PUBLIC 要求（include 目录、C++17）会传给链接它的 wordfreq_cli，PRIVATE 的（线程库、警告选项）只给自己用">
  <defs><marker id="us-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="us-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="14" y="14" width="300" height="170" rx="8" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="30.0" y="40.0" text-anchor="start" style="fill:var(--fg);font-size:13px;font-weight:700">wordfreq（库）</text><text x="30.0" y="72.0" text-anchor="start" style="fill:var(--accent);font-size:12px;font-weight:700">PUBLIC</text><text x="110.0" y="72.0" text-anchor="start" style="fill:var(--fg);font-size:12px">include/ 头文件目录</text><text x="30.0" y="100.0" text-anchor="start" style="fill:var(--accent);font-size:12px;font-weight:700">PUBLIC</text><text x="110.0" y="100.0" text-anchor="start" style="fill:var(--fg);font-size:12px">cxx_std_17</text><text x="30.0" y="128.0" text-anchor="start" style="fill:var(--muted);font-size:12px;font-weight:700">PRIVATE</text><text x="110.0" y="128.0" text-anchor="start" style="fill:var(--fg);font-size:12px">Threads::Threads</text><text x="30.0" y="156.0" text-anchor="start" style="fill:var(--muted);font-size:12px;font-weight:700">PRIVATE</text><text x="110.0" y="156.0" text-anchor="start" style="fill:var(--fg);font-size:12px">警告选项</text><rect x="400" y="14" width="286" height="170" rx="8" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="416.0" y="40.0" text-anchor="start" style="fill:var(--fg);font-size:13px;font-weight:700">wordfreq_cli（程序）</text><text x="416.0" y="72.0" text-anchor="start" style="fill:var(--accent);font-size:12px">✓ -I include</text><text x="416.0" y="100.0" text-anchor="start" style="fill:var(--accent);font-size:12px">✓ -std=c++17</text><text x="416.0" y="128.0" text-anchor="start" style="fill:var(--muted);font-size:12px">✗ 线程选项（不需要）</text><text x="416.0" y="156.0" text-anchor="start" style="fill:var(--muted);font-size:12px">✗ 库的警告选项</text><line x1="316" y1="76" x2="398" y2="76" style="stroke:var(--accent);stroke-width:1.5" marker-end="url(#us-a)"/><text x="357.0" y="66.0" text-anchor="middle" style="fill:var(--link);font-size:11px">PUBLIC 传过去</text><line x1="316" y1="144" x2="398" y2="144" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#us-a)"/><text x="357.0" y="134.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">PRIVATE 不传</text><text x="14.0" y="210.0" text-anchor="start" style="fill:var(--muted);font-size:11px">判断的依据：公开的头文件中用没用到。target_link_libraries(wordfreq_cli PRIVATE wordfreq::wordfreq)</text>
  </g>
</svg>
</figure>

如果把头文件目录误写成 `PRIVATE`，库本身能编译，用它的程序就不行了：

```
[ 60%] Built target wordfreq
[ 80%] Building CXX object app/CMakeFiles/wordfreq_cli.dir/main.cpp.o
app/main.cpp:10:10: fatal error: 'wordfreq/counter.h' file not found
   10 | #include "wordfreq/counter.h"
      |          ^~~~~~~~~~~~~~~~~~~~
1 error generated.
```

反过来，把线程库写成 `PUBLIC` 不会出错，只是 `main.cpp` 也会被加上线程相关的选项，这是不必要的依赖。对于静态库还有一点要注意：即使写的是 `PRIVATE`，链接最终的程序时线程库还是会被加上，因为静态库只是一堆 `.o` 文件，`counter.o` 中用到的线程函数总得有人提供。CMake 知道这一点，会自动处理，这正是用目标描述依赖、而不是自己拼编译命令的好处。

### 2.5 为什么不用全局的命令

旧的 CMake 教程中常见这样的写法：

```cmake
include_directories(${PROJECT_SOURCE_DIR}/include)
add_compile_options(-Wall -Wextra)
set(CMAKE_CXX_STANDARD 17)
```

它们对当前目录和子目录中的**所有**目标都生效。项目小的时候看不出问题，项目一大，问题就来了：

- 每个目标到底用了哪些头文件目录、哪些选项，要看它所在的目录和所有上层目录的 `CMakeLists.txt` 才知道；
- 第三方库也在这些目录之下时，会被加上我们的警告选项，冒出一大堆不归我们管的警告（第 2 篇会遇到这个问题）；
- 库需要的东西，使用者要自己记得加上，忘了就是编译错误。

`target_` 开头的命令只作用于一个目标，使用要求会沿着 `target_link_libraries` 自动传递，每个目标需要什么一目了然。**现代 CMake 的核心就是：用目标和使用要求描述项目，尽量不用全局的命令。**

## 3. 警告

### 3.1 用一个函数统一打开警告

每个目标都要打开同样的一组警告，在三个 `CMakeLists.txt` 中各写一遍，以后要改就得改三处。把它写成一个函数，放在 `cmake/Warnings.cmake` 中：

```cmake
# wordfreq_set_warnings(<目标>)：给自己写的目标打开一组警告。
# 只用在自己的目标上：第三方库的警告不是我们能改的。
function(wordfreq_set_warnings target)
    if(MSVC)
        set(warnings /W4 /permissive-)
        set(werror /WX)
    else()
        set(warnings -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
                     -Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual)
        set(werror -Werror)
    endif()
    target_compile_options(${target} PRIVATE ${warnings})
    if(WORDFREQ_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE ${werror})
    endif()
endfunction()
```

顶层的 `list(APPEND CMAKE_MODULE_PATH ...)` 把 `cmake/` 目录加到了模块的搜索路径中，所以 `include(Warnings)` 能找到 `Warnings.cmake`。

警告选项用的是 `PRIVATE`：这是我们对自己代码的要求，不应该强加给使用这个库的人。

| 选项 | 检查什么 |
|---|---|
| `-Wall -Wextra` | 最常用的一组警告：未使用的变量、有符号和无符号比较等 |
| `-Wpedantic` | 使用了标准中没有的编译器扩展 |
| `-Wshadow` | 内层的变量名遮住了外层的同名变量 |
| `-Wconversion` `-Wsign-conversion` | 可能丢失数据的隐式类型转换 |
| `-Wold-style-cast` | 使用了 C 风格的强制转换 `(int)x` |
| `-Wnon-virtual-dtor` | 有虚函数的类，析构函数却不是虚函数 |
| `-Woverloaded-virtual` | 派生类的函数遮住了基类的虚函数 |

### 3.2 一个真实的警告

给输出加上百分比一列，最直接的写法是：

```cpp
std::cout << ... << std::setw(8) << 100.0 * e.count / counter.total() << "%\n";
```

编译时：

```
app/main.cpp:41:64: warning: implicit conversion from 'std::int64_t' (aka 'long long') to 'double' may lose precision [-Wimplicit-int-float-conversion]
   41 |                   << std::setw(8) << 100.0 * e.count / counter.total() << "%\n";
      |                                                      ~ ~~~~~~~~^~~~~~~
```

`double` 只有 53 位有效数字，超过 2⁵³ 的 64 位整数转成 `double` 时会丢失精度。单词的个数显然到不了 2⁵³，所以这里没有问题，但编译器不知道。确认没有问题以后，用 `static_cast` 写明这是有意的转换，警告就消失了，读代码的人也能看出这里考虑过：

```cpp
100.0 * static_cast<double>(e.count) / static_cast<double>(counter.total())
```

`-Wconversion` 这一类警告在旧代码上通常会报出成百上千条，但在新项目中从一开始就打开，成本很低，还能及早发现真正的截断错误。

### 3.3 把警告当作错误

警告的问题是容易被忽略：编译输出一滚动就看不见了，几十条警告中混进一条新的，谁也不会注意。所以很多项目会用 `-Werror` 把警告当作错误，**保证警告的数量一直是零**。

但是 `-Werror` 不应该写死。换一个新版本的编译器，可能会多出几种警告，用户编译你的项目就失败了。所以这里把它做成选项 `WORDFREQ_WARNINGS_AS_ERRORS`，默认关闭；自己开发时、持续集成（第 6 篇）中打开。

## 4. 构建类型

不指定构建类型时，上面 `main.cpp` 的编译命令中既没有 `-g`，也没有 `-O`：不能调试，也没有优化。CMake 有 4 种预定义的构建类型：

| `CMAKE_BUILD_TYPE` | GCC / Clang 的选项 | 用途 |
|---|---|---|
| `Debug` | `-g` | 调试，没有优化，变量的值都能看到 |
| `Release` | `-O3 -DNDEBUG` | 发布，最快；`-DNDEBUG` 会去掉 `assert` |
| `RelWithDebInfo` | `-O2 -g -DNDEBUG` | 有优化，又能调试，适合性能分析（第 5 篇） |
| `MinSizeRel` | `-Os -DNDEBUG` | 让程序尽量小 |

```bash
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
```

一个构建目录只对应一种构建类型，想同时有调试版和发布版，就用两个构建目录。

Visual Studio 和 Xcode 这类**多配置**的生成器是例外：一个构建目录中同时包含 Debug 和 Release，在编译时选择：`cmake --build build --config Release`。`CMAKE_BUILD_TYPE` 对它们不起作用。

## 5. CMakePresets.json：把常用的配置固定下来

现在配置一个发布版要输入：

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DWORDFREQ_WARNINGS_AS_ERRORS=ON
```

后面几篇还会加上 Sanitizer、覆盖率等选项，命令越来越长。每个人记的版本不一样，「在我这里能编译」的问题就来了。`CMakePresets.json` 把这些配置写进项目里，提交到版本库，大家用同一套配置：

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "base",
      "hidden": true,
      "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": { "WORDFREQ_WARNINGS_AS_ERRORS": "ON" }
    },
    { "name": "debug", "displayName": "Debug：调试用", "inherits": "base",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" } },
    { "name": "release", "displayName": "Release：优化", "inherits": "base",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" } }
  ],
  "buildPresets": [
    { "name": "debug", "configurePreset": "debug" },
    { "name": "release", "configurePreset": "release" }
  ]
}
```

- `base` 是一个隐藏的预设（`hidden`），只用来被继承：构建目录是 `build/<预设名>`，警告当作错误；
- `debug` 和 `release` 继承 `base`，各自设置构建类型。

使用的时候：

```bash
cmake --list-presets
```

```
Available configure presets:

  "debug"   - Debug：调试用
  "release" - Release：优化
```

```bash
cmake --preset debug              # 配置，构建目录是 build/debug
cmake --build --preset debug      # 编译
```

因为 `base` 打开了 `WORDFREQ_WARNINGS_AS_ERRORS`，第 3.2 节那两条警告在这里变成了错误，编译失败：

```
app/main.cpp:41:64: error: implicit conversion from 'std::int64_t' (aka 'long long') to 'double' may lose precision [-Werror,-Wimplicit-int-float-conversion]
app/main.cpp:41:48: error: implicit conversion from 'const std::int64_t' (aka 'const long long') to 'double' may lose precision [-Werror,-Wimplicit-int-float-conversion]
2 errors generated.
```

CLion、VS Code（CMake Tools 扩展）、Visual Studio 都能直接识别 `CMakePresets.json`，在界面上选择预设。如果某些设置只和你自己的电脑有关（比如编译器的路径），写在 `CMakeUserPresets.json` 中，它可以继承 `CMakePresets.json` 中的预设，并且不提交到版本库。

## 6. compile_commands.json

顶层设置了 `CMAKE_EXPORT_COMPILE_COMMANDS`，配置以后构建目录中会多出一个 `compile_commands.json`，记录了每个源文件完整的编译命令：

```json
{
  "directory": ".../build/debug/app",
  "command": "/usr/bin/c++  -I.../include -g -std=c++17 -arch arm64 -Wall -Wextra ... -c .../app/main.cpp",
  "file": ".../app/main.cpp",
  "output": "app/CMakeFiles/wordfreq_cli.dir/main.cpp.o"
},
```

它是给其他工具看的。编辑器的代码补全（clangd）、第 6 篇的 clang-tidy，都需要知道每个文件是用什么选项编译的，否则它们连 `#include "wordfreq/counter.h"` 在哪里都找不到。clangd 会在源代码目录和它的 `build/` 子目录中查找这个文件；使用预设时文件在 `build/debug/` 中，可以在项目根目录建一个符号链接指向它，或者在 clangd 的配置中写明路径。

Visual Studio 的生成器不支持这个功能，Makefile 和 Ninja 都支持。

## 7. 常见错误

1. **在顶层用 `include_directories`、`add_compile_options`**。项目能编译，但每个目标的选项来自哪里很难看清，第三方库也会受影响。改用 `target_` 开头的命令。
2. **该用 `PUBLIC` 的地方用了 `PRIVATE`**。库能编译，使用它的程序找不到头文件，或者报出 C++17 的语法错误。判断的依据是：**公开的头文件中用没用到**。
3. **目标名和别的项目冲突**。目标名在整个构建中是全局的，被别的项目引入时，`utils`、`common` 这样的名字很可能重名。加上项目名作为前缀，再提供一个 `项目名::` 的别名。
4. **在源代码目录中直接运行 `cmake .`**。生成的文件和源代码混在一起，很难清理。始终用 `-B` 指定一个单独的构建目录。
5. **改了 `CMakeLists.txt` 中的选项，结果没有变化**。用 `-D` 设置过的缓存变量会保存在构建目录的 `CMakeCache.txt` 中，后来改 `CMakeLists.txt` 中的默认值不会覆盖它。删掉构建目录重新配置，或者再用 `-D` 设置一次。

## 8. 小结

- 把功能写成库，程序、测试、性能测试都链接这个库；
- 用 `target_` 开头的命令描述每个目标需要什么，用 `PUBLIC`、`PRIVATE`、`INTERFACE` 说明哪些要求要传给使用者，判断的依据是公开的头文件；
- 用函数统一管理警告选项，`-Werror` 做成选项，在开发和持续集成中打开；
- 用构建类型区分调试版和发布版，一个构建目录一种类型；
- 用 `CMakePresets.json` 把常用的配置写进项目。

## 9. 练习

1. 用 `-DBUILD_SHARED_LIBS=ON` 把 `wordfreq` 编译成动态库，运行程序，然后用 `otool -L`（macOS）或 `ldd`（Linux）看看它依赖了哪些库。程序为什么能找到 `libwordfreq`？（提示：CMake 默认会设置 rpath，参考《静态库与动态库》第 4.2 节。）
2. 如果 `counter.h` 中包含了 `<thread>`，并且 `Counter` 有一个 `std::thread` 类型的成员，线程库应该用 `PUBLIC` 还是 `PRIVATE`？
3. 在 `CMakePresets.json` 中增加一个 `relwithdebinfo` 预设，继承 `base`，构建类型是 `RelWithDebInfo`。

<details>
<summary>点击查看答案</summary>

1. 在 macOS 上用 `otool -L build/app/wordfreq` 可以看到一行 `@rpath/libwordfreq.dylib`，在 Linux 上 `ldd` 显示 `libwordfreq.so => .../build/src/libwordfreq.so`。CMake 构建程序时，会把它依赖的、在构建目录中的动态库所在的目录写进程序的 rpath，所以在构建目录中直接运行能找到。安装（`cmake --install`）时，CMake 会把 rpath 改成安装后的路径，默认是清空，需要用 `CMAKE_INSTALL_RPATH` 设置。

2. 严格来说，头文件中用到了 `std::thread`，需要的只是 `<thread>` 这个标准库头文件，它不需要额外的头文件目录。但在某些平台上使用 `std::thread` 需要 `-pthread` 编译选项，而包含了 `counter.h` 的代码也会实例化 `std::thread` 的内联函数，所以应该改成 `PUBLIC`，让使用者也得到这个选项。规则仍然是：公开的头文件中用到了，就是 `PUBLIC`。

3.
```json
{ "name": "relwithdebinfo", "displayName": "RelWithDebInfo：优化 + 调试信息", "inherits": "base",
  "cacheVariables": { "CMAKE_BUILD_TYPE": "RelWithDebInfo" } }
```
再在 `buildPresets` 中加上 `{ "name": "relwithdebinfo", "configurePreset": "relwithdebinfo" }`。

</details>

下一篇：[使用第三方库](/posts/cppeng-dependencies/)。用 fmt 改写输出，用 CLI11 处理命令行参数，看看 CMake 有哪几种方法把别人的库引进来。
