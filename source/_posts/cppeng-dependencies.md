---
title: 使用第三方库：find_package 与 FetchContent
date: 2026-11-04 10:10:00
updated: 2026-11-04 10:10:00
categories:
  - C/C++
  - C++工程实践
tags:
  - C++
  - CMake
  - 依赖管理
order: 2
description: 手写的命令行解析遇到 -n abc 直接崩溃，iostream 的格式控制又长又容易出错。本文给 wordfreq 引入两个第三方库：用 fmt 格式化输出，用 CLI11 解析命令行参数。借此讲清楚 CMake 引入依赖的几种方法：find_package 找已经安装的库，FetchContent 在配置时下载源代码，以及 vcpkg 这类包管理器；还会遇到一个真实的问题：第三方库的警告让 -Werror 的构建失败。
disableNunjucks: true
---

> **本文目标**：会用 `find_package` 使用已经安装的库，知道它是怎样找到库的；会用 `FetchContent` 在配置时下载并构建依赖，并用哈希值固定版本；会把两者结合起来：系统中有就用，没有就下载；知道为什么第三方库的头文件要作为系统头文件引入；了解 vcpkg、Conan 这类包管理器适合什么场合。
>
> **前置知识**：[上一篇](/posts/cppeng-cmake-project/)。
>
> **演示环境**：macOS + Apple Clang 21 + CMake 4.4，fmt 用 Homebrew 安装；另外在 Docker 的 Ubuntu 24.04（GCC 13、CMake 3.28）中验证。

## 1. 为什么要用第三方库

上一篇的 `main.cpp` 自己解析命令行参数：

```cpp
for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-n" && i + 1 < argc) topN = std::stoul(argv[++i]);
    else files.push_back(arg);
}
```

试几个用户可能会输入的参数：

```
$ wordfreq -n abc dickens.txt
libc++abi: terminating due to uncaught exception of type std::invalid_argument: stoul: no conversion
$ wordfreq dickens.txt -n
无法读取文件：-n
$ wordfreq --help
无法读取文件：--help
```

第一个直接崩溃了：`std::stoul` 转换失败时抛出异常，没有人捕获。第二个把 `-n` 当成了文件名。第三个根本没有帮助信息。这些都能修，但要写的代码不少：检查数字、检查范围、检查文件是否存在、拼出帮助信息……而且每个程序都要写一遍。

输出也有问题：

```cpp
std::cout << std::setw(4) << ++rank << "  " << std::left << std::setw(14) << e.word
          << std::right << std::setw(9) << e.count << std::fixed << std::setprecision(2)
          << std::setw(8) << 100.0 * static_cast<double>(e.count) / static_cast<double>(counter.total()) << "%\n";
```

一行输出要 9 个格式控制。而且 `std::left`、`std::fixed`、`std::setprecision` 会一直保持到被改掉为止（`std::setw` 只对下一项有效），忘了写 `std::right`，后面所有的数字都会变成左对齐。

这两件事都有成熟的库：

- **[fmt](https://github.com/fmtlib/fmt)**：类似 Python 的格式化语法，C++20 的 `std::format` 就是根据它标准化的。项目用 C++17，所以用 fmt；
- **[CLI11](https://github.com/CLIUtils/CLI11)**：命令行参数解析，只有头文件。

用上它们以后（完整的代码见第 5 节）：

```cpp
app.add_option("-n,--top", topN, "列出前几个单词")->capture_default_str()->check(CLI::Range(1, 100000));
...
fmt::print("{:>4}  {:<14}{:>9}  {:>6.2f}%\n", ++rank, e.word, e.count, percent);
```

```
$ wordfreq -n abc dickens.txt
--top: Value abc not in range [1 - 100000]
Run with --help for more information.
$ wordfreq dickens.txt -n
--top: 1 required UINT:INT in [1 - 100000] missing
Run with --help for more information.
$ wordfreq nosuch.txt
files: File does not exist: nosuch.txt
Run with --help for more information.
```

问题是：怎样让 CMake 找到这两个库？

## 2. 四种方法

| 方法 | 做法 | 优点 | 缺点 |
|---|---|---|---|
| 系统安装 + `find_package` | 用 apt、Homebrew 等安装，CMake 去找 | 不用下载，配置快 | 版本由系统决定；每个人都要先安装 |
| `FetchContent` | 配置时下载源代码，和项目一起编译 | 什么都不用装，版本由项目决定 | 第一次配置要下载、编译时间更长 |
| 包管理器（vcpkg、Conan） | 包管理器负责下载、编译，再交给 `find_package` | 依赖多、依赖之间又有依赖时最省心 | 多一个工具要学、要装 |
| git 子模块 + `add_subdirectory` | 把依赖的源代码放进自己的仓库 | 完全离线 | 仓库变大；更新麻烦 |

wordfreq 用前两种：fmt 先用 `find_package` 找已经安装的，找不到再用 `FetchContent` 下载；CLI11 只有头文件，直接下载。

<figure class="diagram">
<svg viewBox="0 0 700 200" role="img" aria-label="Dependencies.cmake 的流程：先 find_package(fmt 10)，macOS 上找到 Homebrew 的 12.2 直接使用，Ubuntu 上 apt 的 9.1 版本不够，用 FetchContent 下载 11.2.0，两条路得到的都是 fmt::fmt 目标">
  <defs><marker id="dp-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="dp-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="14" y="70" width="170" height="52" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="99.0" y="92.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">find_package</text><text x="99.0" y="108.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">(fmt 10 QUIET)</text><rect x="250" y="14" width="200" height="52" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="350.0" y="36.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">使用已安装的 fmt</text><text x="350.0" y="52.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">macOS：Homebrew 12.2</text><rect x="250" y="126" width="200" height="52" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="350.0" y="148.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">FetchContent 下载</text><text x="350.0" y="164.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">Ubuntu：apt 只有 9.1</text><line x1="184" y1="86" x2="248" y2="44" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#dp-a)"/><text x="205.0" y="58.0" text-anchor="middle" style="fill:var(--link);font-size:11px">找到</text><line x1="184" y1="108" x2="248" y2="150" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#dp-a)"/><text x="200.0" y="144.0" text-anchor="middle" style="fill:var(--link);font-size:11px">没找到</text><rect x="520" y="70" width="166" height="52" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="603.0" y="92.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">fmt::fmt</text><text x="603.0" y="108.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">同一个目标名</text><line x1="450" y1="40" x2="518" y2="86" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#dp-a)"/><line x1="450" y1="152" x2="518" y2="108" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#dp-a)"/>
  </g>
</svg>
</figure>

## 3. find_package：使用已经安装的库

### 3.1 用法

```cmake
find_package(fmt 10 REQUIRED)
target_link_libraries(wordfreq_cli PRIVATE fmt::fmt)
```

- `fmt` 是包名，`10` 是**最低**版本：找到 10.x、11.x、12.x 都可以（具体怎样算兼容，由库自己决定）；
- `REQUIRED`：找不到就报错，停止配置。换成 `QUIET`，找不到时不报错，只把变量 `fmt_FOUND` 设为假，后面可以自己处理；
- 找到以后，库提供一个**导入目标** `fmt::fmt`，链接它就够了。头文件目录、需要的编译选项、库文件的路径，都作为使用要求带在这个目标上，和上一篇自己定义的 `wordfreq::wordfreq` 完全一样。

### 3.2 它是怎样找到的

在 macOS 上用 Homebrew 安装 fmt（`brew install fmt`）以后，配置时显示：

```
-- 使用已安装的 fmt 12.2.0
```

`CMakeCache.txt` 中记录了它是在哪里找到的：

```
fmt_DIR:PATH=/opt/homebrew/lib/cmake/fmt
```

这个目录中有几个文件：

```
fmt-config.cmake
fmt-config-version.cmake
fmt-targets.cmake
fmt-targets-release.cmake
```

它们是 fmt 安装时生成的，叫作**包配置文件**。`fmt-config-version.cmake` 回答「版本是否满足要求」，`fmt-targets.cmake` 定义了 `fmt::fmt` 这个目标。`find_package(fmt)` 做的事情，就是在一系列目录中寻找 `fmt-config.cmake`（或者 `fmtConfig.cmake`），找到就执行它。这叫**配置模式**（config mode），现在绝大多数库都支持。

搜索的目录包括 `CMAKE_PREFIX_PATH` 中的目录、`/usr`、`/usr/local`、Homebrew 的 `/opt/homebrew` 等，在每个目录下再看 `lib/cmake/<包名>/` 等几个固定的子目录。库装在别的地方时，告诉 CMake 去哪里找：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/mylibs      # 前缀目录，可以用分号分隔多个
cmake -S . -B build -Dfmt_DIR=/opt/mylibs/lib/cmake/fmt  # 或者直接给出配置文件所在的目录
```

一些很老的库没有提供包配置文件，CMake 自带了一批 `Find<包名>.cmake` 脚本来找它们，比如上一篇用到的 `find_package(Threads)`，还有 `ZLIB`、`OpenSSL`、`CURL` 等。这叫**模块模式**（module mode），对使用者来说用法一样。

### 3.3 版本不够时

在 Ubuntu 24.04 中，用 apt 安装的 fmt 是 9.1。`find_package(fmt 10)` 找到了它，但版本不满足要求，于是当作没找到。如果写的是 `REQUIRED`，配置就失败了：系统包管理器提供的版本往往比较旧，这是只依赖 `find_package` 的主要问题。

## 4. FetchContent：配置时下载

### 4.1 用法

```cmake
include(FetchContent)

FetchContent_Declare(CLI11
    URL https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.5.0.tar.gz
    URL_HASH SHA256=17e02b4cddc2fa348e5dbdbb582c59a3486fa2b2433e70a0c3bacb871334fd55
    SYSTEM)
FetchContent_MakeAvailable(CLI11)

target_link_libraries(wordfreq_cli PRIVATE CLI11::CLI11)
```

- `FetchContent_Declare` 说明从哪里获取，这里是 GitHub 上发布版本的源代码压缩包；
- `FetchContent_MakeAvailable` 下载、解压，然后对它执行 `add_subdirectory`。CLI11 自己的 `CMakeLists.txt` 定义了 `CLI11::CLI11` 目标，我们直接链接；
- `SYSTEM` 见 4.3 节。

第一次配置时会显示下载进度：

```
-- 使用已安装的 fmt 12.2.0
-- [download 1% complete]
-- [download 2% complete]
……
-- [download 100% complete]
-- Configuring done (6.5s)
```

下载的内容放在构建目录的 `_deps/` 中：`cli11-src/` 是源代码，`cli11-build/` 是它的构建目录。以后再配置不会重新下载。

### 4.2 固定版本：URL_HASH

也可以从 git 仓库获取：

```cmake
FetchContent_Declare(CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.5.0)
```

但用发布版本的压缩包加上 `URL_HASH` 更好：

1. **快**：压缩包只有一个版本的文件，git 要克隆整个历史（`GIT_SHALLOW TRUE` 可以缓解）；
2. **可靠**：下载的内容和哈希值不一致时，CMake 会报错并停止。标签是可以被移动的，被篡改的压缩包也逃不过哈希检查。今天能构建的版本，一年以后构建出来还是同一份代码。

哈希值可以自己算出来：

```bash
curl -sL https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.5.0.tar.gz | shasum -a 256
```

如果用 `GIT_TAG`，也应该写一个具体的提交哈希或者版本标签，**不要写分支名**（比如 `main`）：每次重新配置都可能拿到不同的代码。

### 4.3 第三方库的警告：SYSTEM

最开始的版本中，`FetchContent_Declare` 没有写 `SYSTEM`。在 macOS 上一切正常，因为 fmt 来自 Homebrew。拿到 Ubuntu 中，打开 `WORDFREQ_WARNINGS_AS_ERRORS` 构建，fmt 要下载源代码，然后编译失败了：

```
-- 没有找到 fmt 10 以上的版本，下载源代码
-- {fmt} version: 11.2.0
...
FAILED: app/CMakeFiles/wordfreq_cli.dir/main.cpp.o
/usr/bin/c++  -I/src/include -I/b/_deps/fmt-src/include -isystem /b/_deps/cli11-src/include -O3 -DNDEBUG -std=c++17 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion ... -Werror ... -c /src/app/main.cpp
/b/_deps/fmt-src/include/fmt/base.h:1682:21: error: conversion to 'long unsigned int' from 'int' may change the sign of the result [-Werror=sign-conversion]
/b/_deps/fmt-src/include/fmt/base.h:1683:42: error: conversion to 'long unsigned int' from 'int' may change the sign of the result [-Werror=sign-conversion]
...
cc1plus: all warnings being treated as errors
```

报错的不是我们的代码，而是 fmt 的头文件 `base.h`。`main.cpp` 包含了 fmt 的头文件，编译 `main.cpp` 时用的是我们的警告选项，于是 fmt 头文件中的代码也被检查了，GCC 的 `-Wsign-conversion` 在其中发现了几处可疑的转换。

仔细看编译命令：fmt 的目录是用 `-I` 加上的，而 CLI11 的目录是用 `-isystem` 加上的。用 `-isystem` 引入的目录被编译器当作**系统头文件**，不报告其中的警告。CLI11 的 `CMakeLists.txt` 自己把头文件目录标记成了系统目录，fmt 的没有。

在我们这一边就能解决：CMake 3.25 起，`FetchContent_Declare` 可以加上 `SYSTEM`，这个依赖的所有头文件目录都会用 `-isystem` 引入。所以本项目的 `cmake_minimum_required` 要求至少 3.25。

用 `find_package` 找到的导入目标，头文件目录默认就是系统目录，所以在 macOS 上没有这个问题。这也说明：**同一个项目要在几种环境中构建**，只在自己的电脑上能编译是不够的。第 6 篇会用持续集成在几种系统上自动构建。

### 4.4 网络慢或者离线

`FetchContent` 的代价是第一次配置要下载。在网络慢的环境中（本文的 Docker 容器），下载 fmt、CLI11 和 GoogleTest 三个依赖就花了 4 分多钟。几个办法：

- **同一个构建目录**不会重复下载，平时只是增量编译，影响不大；
- 删除构建目录重新配置前，可以把下载的内容放到一个共享的目录中：`-DFETCHCONTENT_BASE_DIR=~/.cache/fetchcontent`；
- 已经下载过、又不想让 CMake 再联网检查更新：`-DFETCHCONTENT_FULLY_DISCONNECTED=ON`；
- 已经把源代码放在本地：`-DFETCHCONTENT_SOURCE_DIR_CLI11=/path/to/CLI11`，就不会下载了。

## 5. 两者结合：有就用，没有就下载

fmt 是一个很常用的库，很多人的电脑上已经装了。为了用上已经安装的版本，同时保证没有安装的人也能构建，`cmake/Dependencies.cmake` 是这样写的：

```cmake
include(FetchContent)

# 下载的第三方库都加上 SYSTEM：它们的头文件用 -isystem 引入，里面的警告不会算到我们头上

# fmt：先找系统中已经安装的（Homebrew、apt、vcpkg……），版本太旧或者没有，再下载源代码
find_package(fmt 10 QUIET)
if(fmt_FOUND)
    message(STATUS "使用已安装的 fmt ${fmt_VERSION}")
else()
    message(STATUS "没有找到 fmt 10 以上的版本，下载源代码")
    FetchContent_Declare(fmt
        URL https://github.com/fmtlib/fmt/archive/refs/tags/11.2.0.tar.gz
        URL_HASH SHA256=bc23066d87ab3168f27cef3e97d545fa63314f5c79df5ea444d41d56f962c6af
        SYSTEM)
    FetchContent_MakeAvailable(fmt)
endif()

# CLI11：只有头文件，直接下载
FetchContent_Declare(CLI11
    URL https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.5.0.tar.gz
    URL_HASH SHA256=17e02b4cddc2fa348e5dbdbb582c59a3486fa2b2433e70a0c3bacb871334fd55
    SYSTEM)
FetchContent_MakeAvailable(CLI11)
```

两条路得到的都是 `fmt::fmt` 目标，所以使用的地方不用区分：

```cmake
target_link_libraries(wordfreq_cli PRIVATE wordfreq::wordfreq fmt::fmt CLI11::CLI11)
```

CMake 3.24 起，`FetchContent_Declare` 还支持 `FIND_PACKAGE_ARGS`，可以把「先找，找不到再下载」写在一处：

```cmake
FetchContent_Declare(fmt
    URL ... URL_HASH ...
    FIND_PACKAGE_ARGS 10)          # 先 find_package(fmt 10)，找到就不下载
```

本文用了显式的写法，是为了在配置时打印出用的是哪一个。

新的 `main.cpp`：

```cpp
int main(int argc, char** argv)
{
    CLI::App app{"统计文本中出现次数最多的单词"};
    std::size_t topN = 10;
    unsigned threads = 1;
    std::vector<std::string> files;
    app.add_option("-n,--top", topN, "列出前几个单词")->capture_default_str()->check(CLI::Range(1, 100000));
    app.add_option("-j,--threads", threads, "统计用的线程数，0 表示和 CPU 核数相同")->capture_default_str();
    app.add_option("files", files, "要统计的文件，不给出时读取标准输入")->check(CLI::ExistingFile);
    CLI11_PARSE(app, argc, argv);

    // ……读取文件，统计……

    fmt::print("单词 {} 个，不同的 {} 个\n", counter.total(), counter.unique());
    std::size_t rank = 0;
    for (const auto& e : counter.top(topN)) {
        double percent = 100.0 * static_cast<double>(e.count) / static_cast<double>(counter.total());
        fmt::print("{:>4}  {:<14}{:>9}  {:>6.2f}%\n", ++rank, e.word, e.count, percent);
    }
    return 0;
}
```

`CLI11_PARSE` 解析参数，出错或者遇到 `--help` 时，输出信息并从 `main` 返回。`{:>4}` 是右对齐、宽度 4，`{:<14}` 是左对齐、宽度 14，`{:>6.2f}` 是右对齐、宽度 6、两位小数，每个格式只作用于它自己那一项，不存在 iostream 那种「忘了改回来」的问题。帮助信息是自动生成的：

```
$ wordfreq --help
统计文本中出现次数最多的单词

wordfreq [OPTIONS] [files...]

POSITIONALS:
  files TEXT:FILE ...         要统计的文件，不给出时读取标准输入

OPTIONS:
  -h,     --help              Print this help message and exit
  -n,     --top UINT:INT in [1 - 100000] [10]
                              列出前几个单词
  -j,     --threads UINT [1]  统计用的线程数，0 表示和 CPU 核数相同
```

（`-j` 多线程统计的实现在第 4 篇中会仔细检查。）

## 6. 包管理器：vcpkg 和 Conan

依赖只有两三个时，`FetchContent` 就够用了。但如果依赖了 OpenSSL、Boost、Qt 这样的大型库，或者依赖之间又有依赖，每个都从源代码编译会非常慢，版本之间的兼容也难以保证。这时应该用包管理器。

以 [vcpkg](https://vcpkg.io/) 为例，在项目根目录写一个 `vcpkg.json`，列出依赖：

```json
{
  "name": "wordfreq",
  "dependencies": ["fmt", "cli11"]
}
```

配置时把 vcpkg 提供的工具链文件交给 CMake：

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
```

vcpkg 会在配置时下载、编译这些依赖（有二进制缓存，第二次很快），并把它们的安装目录加到 `find_package` 的搜索路径中。**项目的 `CMakeLists.txt` 中仍然只有 `find_package`**，不需要知道依赖是从哪里来的。上面「有就用，没有就下载」的写法和 vcpkg 也能配合：用 vcpkg 时 `find_package` 能找到 fmt，就不会再下载。

Conan 的思路类似，另外支持把编译好的二进制包放在自己的服务器上共享。本章的项目依赖很少，不展开介绍这两个工具。

选择的建议：

- 依赖少、只有头文件或者编译很快：`FetchContent`；
- 依赖多、有大型库、团队有很多项目：vcpkg 或 Conan；
- 无论用哪种，**让 `CMakeLists.txt` 只依赖目标名**（`fmt::fmt`），不要写死库文件和头文件的路径，以后换一种方法时，改动只在一个地方。

## 7. 常见错误

1. **用 `include_directories` 和 `link_libraries` 手动指定第三方库的路径**。换一台电脑路径就不对了。用 `find_package` 或者 `FetchContent`，链接导入的目标。
2. **`GIT_TAG` 写了分支名**。不同时间配置的人拿到不同的代码，「上周还能编译」的问题就来了。
3. **第三方库的头文件没有作为系统头文件引入**。`-Werror` 的构建在某个环境中失败，而失败的位置不在自己的代码中。加上 `SYSTEM`。
4. **只在自己的电脑上试过**。自己的电脑上装了某个库，`find_package` 找到了，别人的电脑上没有，或者版本不同。用一个干净的环境（Docker、持续集成）验证。
5. **把整个依赖的源代码复制进项目，然后修改它**。以后要升级这个依赖时，你的修改很难合并。如果确实需要修改，应该向上游提交，或者用补丁文件（`FetchContent_Declare` 支持 `PATCH_COMMAND`）。

## 8. 小结

- `find_package` 使用已经安装的库，靠库安装时提供的包配置文件找到它，结果是一个导入目标；
- `FetchContent` 在配置时下载源代码，和项目一起编译；用 `URL_HASH` 固定版本，用 `SYSTEM` 屏蔽第三方头文件的警告；
- 两者可以结合：系统中有合适的版本就用，没有就下载；
- 依赖多的时候用 vcpkg、Conan，`CMakeLists.txt` 中仍然只写 `find_package`。

## 9. 练习

1. 去掉 `CLI11` 的 `URL_HASH` 中的一个字符，重新配置，看看会怎样。
2. 把 fmt 的 `find_package` 改成 `find_package(fmt 99 QUIET)`，配置时会发生什么？
3. 不改 `CMakeLists.txt`，怎样让 CMake 忽略系统中安装的 fmt，强制下载？（提示：`CMAKE_DISABLE_FIND_PACKAGE_<包名>`。）

<details>
<summary>点击查看答案</summary>

1. 下载完成后哈希值不一致，配置失败，错误信息中会列出期望的哈希值和实际的哈希值。这就是 `URL_HASH` 的作用：下载的内容和你写下的不一致，就不会继续。

2. 系统中没有 99 以上的版本，`fmt_FOUND` 为假，会打印「没有找到 fmt 10 以上的版本，下载源代码」（提示信息中的版本号是写死的），然后下载 11.2.0。这说明「有就用，没有就下载」的两条路都能工作。

3. `cmake -S . -B build -DCMAKE_DISABLE_FIND_PACKAGE_fmt=ON`。这个变量让 `find_package(fmt)` 直接当作没找到。它在测试「没有安装依赖时能不能构建」时很有用。

</details>

下一篇：[单元测试](/posts/cppeng-unit-testing/)。用 GoogleTest 给 wordfreq 写测试，接入 CTest。
