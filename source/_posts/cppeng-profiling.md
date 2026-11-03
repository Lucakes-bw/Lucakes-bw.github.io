---
title: 性能分析与优化：基准测试、perf 与火焰图
date: 2026-11-04 10:40:00
updated: 2026-11-04 10:40:00
categories:
  - C/C++
  - C++工程实践
tags:
  - C++
  - 性能
  - perf
  - 基准测试
order: 5
description: 优化之前要先测量。本文从 wordfreq 最直接的第一版写法出发，用 Google Benchmark 测出每次修改的效果，用 perf 采样、用火焰图找出时间花在哪里，四步把统计 16 MB 文本的时间从 201 毫秒降到 67 毫秒，再用 4 个线程降到 29 毫秒。每一步都有实测数据，也会看到有的「优化」只快了几个百分点。
disableNunjucks: true
---

> **本文目标**：会用 Google Benchmark 写基准测试，读懂它的输出，避免测量被编译器优化掉；会用 perf 采样、看报告、生成火焰图，知道「自身时间」和「累计时间」的区别；体会几种常见的优化：选对容器、去掉中间结果、让热点函数能被内联、避免重复构造对象；知道多线程为什么达不到线性加速。
>
> **前置知识**：[上一篇](/posts/cppeng-sanitizers-coverage/)；《哈希表》《二叉搜索树》（`std::unordered_map` 和 `std::map` 的原理）。
>
> **演示环境**：基准测试在 macOS（Apple M6，2 个性能核 + 4 个能效核）+ Apple Clang 21 上运行；perf 和火焰图在 Docker 的 Ubuntu 24.04（GCC 13）中运行。

## 1. 先测量，再优化

程序慢，第一反应往往是「我觉得是某某地方慢」，然后去改那个地方。这样做常常白费力气：程序的时间通常集中在很少的几处代码上，凭感觉猜中的机会并不大。

正确的顺序是：

1. **有一个可以重复的测量**，知道现在有多快；
2. **找出时间花在哪里**，只优化最花时间的地方；
3. **改一处，测一次**，确认真的变快了，并且结果仍然正确。

第 3 步中的「结果仍然正确」由前两篇的测试保证：本文的每个版本都通过了全部 23 个测试。没有测试，优化就是在冒险。

还有一点：**性能只在优化过的构建中测量**。Debug 构建没有优化，各个部分的相对快慢和 Release 构建完全不同，在 Debug 中找到的热点可能根本不是真正的热点。分析性能用 `RelWithDebInfo`：有优化，又有调试信息，分析工具能把机器码对应到函数和行号。

## 2. 基准测试：Google Benchmark

### 2.1 测试的数据

要测量统计单词的速度，先要有文本。用真实的书当然最好，但书有版权，体积也不好控制。wordfreq 的 `bench/text_gen.h` 生成一段「看起来像英文」的文本：词汇表中有 20000 个用音节拼出来的单词，第 r 常见的单词出现的概率和 1/r 成正比（**齐夫定律**，真实的英文文本大致服从这个规律：最常见的 the 大约占 7%，第二常见的 of 大约占一半），夹杂着逗号、句号和换行。随机数的种子固定，每次生成的内容都一样，测量结果才能比较。

数据的特征会影响结论。比如不同的单词只有几十个时，任何容器都很快；单词有几百万个时，内存访问就成了主要问题。所以测试数据应该尽量接近真实的使用场景。

### 2.2 写基准测试

`bench/counter_bench.cpp`：

```cpp
#include <benchmark/benchmark.h>

#include "text_gen.h"
#include "wordfreq/counter.h"

void BM_CounterAdd(benchmark::State& state)
{
    const std::string& t = text(static_cast<std::size_t>(state.range(0)));   // 生成指定大小的文本
    for (auto _ : state) {
        wordfreq::Counter c;
        c.add(t);
        benchmark::DoNotOptimize(c.total());
    }
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_CounterAdd)->Arg(1 << 20)->Arg(16 << 20)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
```

- `for (auto _ : state)` 中的代码是被测量的部分。循环执行多少次由框架决定：它会一直运行，直到总时间足够长，测出的平均值足够稳定；
- 生成文本放在循环外面，不计入时间；
- `DoNotOptimize`：告诉编译器这个值「被用到了」。否则编译器可能发现 `c` 算完以后没有人用，把整个计算都删掉，测出来的时间就是 0；
- `SetBytesProcessed`：报告中会多一列「每秒处理多少字节」，不同大小的输入可以直接比较；
- `Arg(1 << 20)`、`Arg(16 << 20)`：分别用 1 MB 和 16 MB 的文本各测一次，在函数中用 `state.range(0)` 取得。

在 CMake 中，基准测试和单元测试一样是一个单独的可执行文件，用选项 `WORDFREQ_BUILD_BENCHMARKS` 控制，`release` 预设中打开：

```cmake
FetchContent_Declare(benchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.9.4.tar.gz
    URL_HASH SHA256=b334658edd35efcf06a99d9be21e4e93e092bd5f95074c1673d5c8705d95c104
    SYSTEM)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)       # 不构建 benchmark 自己的测试
FetchContent_MakeAvailable(benchmark)

add_executable(wordfreq_bench counter_bench.cpp)
target_link_libraries(wordfreq_bench PRIVATE wordfreq::wordfreq benchmark::benchmark)
```

### 2.3 读懂输出

```
$ ./build/release/bench/wordfreq_bench --benchmark_repetitions=3 --benchmark_report_aggregates_only=true
Benchmark                              Time             CPU   Iterations UserCounters...
BM_Tokenize/1048576_median          3.26 ms         3.25 ms            3 bytes_per_second=307.628Mi/s
BM_CounterAdd/1048576_median        14.1 ms         14.1 ms            3 bytes_per_second=70.8965Mi/s
BM_CounterAdd/16777216_median        201 ms          200 ms            3 bytes_per_second=79.8082Mi/s
```

- `Time` 是实际经过的时间，`CPU` 是这个线程占用 CPU 的时间。单线程的代码两者应该接近；
- `--benchmark_repetitions=3`：整个测量重复 3 次，`--benchmark_report_aggregates_only` 只显示平均值、中位数、标准差。上面只列出了中位数。同一台电脑上，重复测量之间的差别通常在几个百分点以内，**小于这个范围的「提升」是不可信的**；
- 测量时电脑上不要运行别的耗 CPU 的程序；笔记本电脑最好接上电源。

这是 wordfreq 第一版的结果：统计 16 MB 的文本要 **201 毫秒**，每秒 80 MB。下面就从这个版本开始。

## 3. 第一版：用 perf 找出热点

### 3.1 第一版的代码

最直接的写法：先把文本切成一个 `std::vector<std::string>`，再逐个统计，计数用 `std::map`：

```cpp
std::vector<std::string> tokenize(std::string_view text)
{
    std::vector<std::string> words;
    std::string cur;
    for (char c : text) {
        if (isWordChar(c)) {
            cur += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (!cur.empty()) {
            pushWord(words, cur);          // 去掉两端的撇号，放进 words
            cur.clear();
        }
    }
    if (!cur.empty()) pushWord(words, cur);
    return words;
}

void Counter::add(std::string_view text)
{
    for (const std::string& w : tokenize(text)) addWord(w);
}

// counts_ 是 std::map<std::string, std::int64_t>
```

代码清楚，也通过了所有的测试。现在看看时间花在哪里。

### 3.2 perf

**perf** 是 Linux 内核自带的性能分析工具。`perf record` 以固定的频率打断程序（下面是每秒 999 次），每次记下程序正在执行哪个函数、调用栈是什么样的。某个函数在采样中出现得越多，说明它花的时间越多。这种方法叫**采样**，对程序的速度几乎没有影响，也不需要重新编译。

```bash
perf record -F 999 -g ./wordfreq -n 3 big.txt       # -g：同时记录调用栈
perf report --no-children
```

为了得到完整的调用栈，编译时加上了 `-fno-omit-frame-pointer`（优化时编译器默认不保留帧指针，perf 就不知道函数是被谁调用的）。用 50 MB 的文本运行，报告中排在前面的函数：

```
    38.75%  wordfreq  wordfreq      [.] wordfreq::Counter::addWord(std::basic_string_view<char>, long)
    16.91%  wordfreq  libc.so.6     [.] memcmp
    12.09%  wordfreq  libc.so.6     [.] __memcpy_generic
     6.72%  wordfreq  wordfreq      [.] wordfreq::tokenize[abi:cxx11](std::basic_string_view<char>)
     4.37%  wordfreq  libc.so.6     [.] tolower
     2.91%  wordfreq  wordfreq      [.] memcmp@plt
     2.02%  wordfreq  wordfreq      [.] wordfreq::(anonymous namespace)::pushWord(...)
```

（模板参数做了简化。）`--no-children` 显示的是每个函数的**自身时间**：采样时程序正好在这个函数的代码中。`addWord` 占了 39%，加上 `memcmp` 的 17%（再加上 `memcmp@plt` 的 3%），一半以上的时间都在查 `std::map`：`std::map::find` 被内联进了 `addWord`，所以显示为 `addWord` 的时间，`memcmp` 是比较两个字符串。

另一种视角是**累计时间**（`--children`）：一个函数自身的时间，加上它调用的所有函数的时间。

```
    95.21%     0.45%  [.] wordfreq::Counter::add(std::basic_string_view<char>)
    63.55%    42.47%  [.] wordfreq::Counter::addWord(std::basic_string_view<char>, long)
    30.88%     7.69%  [.] wordfreq::tokenize[abi:cxx11](std::basic_string_view<char>)
    19.84%     1.56%  [.] wordfreq::(anonymous namespace)::pushWord(...)
    17.50%    10.70%  [.] __memcpy_generic
    11.04%     1.34%  [.] std::vector<std::string>::_M_realloc_insert(...)
```

第一列是累计时间，第二列是自身时间：`add` 自身几乎不花时间，但它调用的函数一共占了 95%；其中统计（`addWord`）占 64%，切分（`tokenize`）占 31%。`tokenize` 中有 11% 花在 `vector` 扩容时搬运字符串上。

### 3.3 火焰图

报告是一张长长的表，调用关系不直观。**火焰图**把所有采样中的调用栈画在一起：

<figure>
<a href="/img/cpp-eng/flame-v0.svg"><img src="/img/cpp-eng/flame-v0.svg" alt="第一版的火焰图：最宽的一块是 Counter::add，它上面分成两块：左边较宽的 addWord，其中一大块是 memcmp；右边较窄的 tokenize，其中有 pushWord 和 vector 扩容" width="1000"></a>
<figcaption>第一版的火焰图（点击图片打开交互版本，可以点击放大某一部分）</figcaption>
</figure>

读法：

- 纵向是调用栈，下面的函数调用了上面的函数；
- **横向的宽度是采样中出现的比例**，也就是累计时间。宽度和左右位置无关，同一层按函数名的字母顺序排列，不是时间顺序；
- 颜色只是为了区分相邻的块，没有含义；
- 看图的方法：先找最宽的块，再沿着它往上看，顶端的「平顶」就是自身时间多的函数。

这张图中，`Counter::add` 之上分成两块：左边宽的是 `addWord`，它的上面有一大片 `memcmp`；右边窄一些的是 `tokenize`，其中可以看到 `pushWord` 和 `vector` 的扩容。两边那些又高又窄的「塔」是程序申请和释放大块内存时进入内核的调用栈（分配、回收内存页），层数很深，但占的比例很小。

火焰图是用 Brendan Gregg 的 [FlameGraph](https://github.com/brendangregg/FlameGraph) 脚本生成的：

```bash
perf script | ./stackcollapse-perf.pl | ./flamegraph.pl > flame.svg
```

macOS 上没有 perf，可以用 Xcode 自带的 Instruments（Time Profiler），或者命令行的 `sample` 命令；Windows 上有 Visual Studio 的性能探查器。它们的原理都是采样，看法相同。

## 4. 四步优化

### 4.1 第一步：std::map 换成 std::unordered_map

`std::map` 是红黑树，查找一个单词要从根往下比较十几次（20000 个单词，log₂20000 ≈ 14），每次都是一次字符串比较，还要在内存中跳来跳去。`std::unordered_map` 是哈希表，算一次哈希值，通常只比较一两次。

只改了 `counter.h` 中的一行：

```cpp
std::unordered_map<std::string, std::int64_t> counts_;
```

| 版本 | 16 MB 用时 | 速度 |
|---|---|---|
| 第一版 | 201 ms | 80 MB/s |
| 换成 `unordered_map` | **98 ms** | 163 MB/s |

快了一倍。这是收益最大的一步，因为它针对的是占时间最多的地方。

需要说明的是，`std::map` 的结果是按单词排好序的，`std::unordered_map` 不是。wordfreq 的 `top` 本来就要按次数重新排序，所以没有影响；如果某个功能依赖 `std::map` 的顺序，这一步就要另外处理。这也是为什么换容器之前，先要有测试。

### 4.2 第二步：去掉中间的 vector

第一版先把整个文本切成一个 `std::vector<std::string>`，再遍历它。16 MB 的文本有 300 万个单词，就要构造 300 万个 `std::string`，`vector` 还要扩容十几次，每次扩容都要搬运已有的字符串。这些字符串用完就扔掉了。

改成边切分边统计：`forEachWord` 每找到一个单词，就调用一次回调函数，单词放在一个反复使用的缓冲区中（第一篇的 `tokenizer.h` 中已经有这个函数了）：

```cpp
void Counter::add(std::string_view text)
{
    forEachWord(text, buffer_, [this](std::string_view w) { addWord(w); });
}
```

`buffer_` 是 `Counter` 的成员，第一次用到的时候分配，以后反复使用，不再分配内存。

| 版本 | 16 MB 用时 | 速度 |
|---|---|---|
| 换成 `unordered_map` | 98 ms | 163 MB/s |
| 去掉中间的 vector | **79 ms** | 204 MB/s |

再看这个版本的 perf 报告：

```
    28.96%  [.] std::_Hashtable<std::string, ...>::_M_find_before_node(...)
    20.90%  [.] wordfreq::isWordChar(char)
    11.64%  [.] wordfreq::forEachWord<...>(...)
     6.87%  [.] __memcpy_generic
     6.57%  [.] std::string::_M_replace(unsigned long, unsigned long, char const*, unsigned long)
     3.88%  [.] memcmp
     3.58%  [.] std::string::_M_construct<char const*>(...)
     3.28%  [.] std::_Hash_bytes(void const*, unsigned long, unsigned long)
     2.39%  [.] wordfreq::Counter::addWord(std::basic_string_view<char>, long)
```

<figure>
<a href="/img/cpp-eng/flame-v2.svg"><img src="/img/cpp-eng/flame-v2.svg" alt="第二步之后的火焰图：forEachWord 之上有两块明显的平顶，一块是哈希表的查找，另一块是 isWordChar" width="1000"></a>
<figcaption>第二步之后的火焰图。forEachWord 上面，isWordChar 是一块很宽的平顶</figcaption>
</figure>

哈希表的查找（`_M_find_before_node`）排第一，这是预料之中的。但排第二的 `isWordChar` 有点奇怪：它只是几个比较，怎么会占 21% 的时间？

### 4.3 第三步：让热点函数能被内联

`isWordChar` 声明在 `tokenizer.h` 中，**定义在 `tokenizer.cpp` 中**：

```cpp
// tokenizer.h
bool isWordChar(char c);

// tokenizer.cpp
bool isWordChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '\'';
}
```

`forEachWord` 是写在头文件中的模板，在 `counter.cpp` 中实例化。编译 `counter.cpp` 时，编译器只看到了 `isWordChar` 的声明，看不到函数体，只能生成一次真正的函数调用。16 MB 的文本，就是 1600 多万次函数调用，每次只做几个比较。perf 报告中 `isWordChar` 单独出现，本身就说明它**没有被内联**：被内联的函数不会作为一个独立的函数出现在采样中。

修改：把它写在头文件中，加上 `inline`：

```cpp
// 判断一个字节能不能出现在单词中：ASCII 字母、数字和撇号（don't、it's）。
// 写在头文件中并且是 inline 的：forEachWord 对每个字节都要调用它，必须能被内联
inline bool isWordChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '\'';
}
```

| 版本 | 16 MB 用时 | 速度 |
|---|---|---|
| 去掉中间的 vector | 79 ms | 204 MB/s |
| `isWordChar` 内联 | **74 ms** | 216 MB/s |

只快了 6% 左右。perf 中 `isWordChar` 占了 21%，内联以后那些比较还是要做，省掉的只是函数调用本身的开销。**一个函数在报告中占的比例，不等于优化它能省下的时间。**

另一种方法是不改代码，打开**链接时优化**（LTO）：链接时编译器能看到所有的 `.cpp`，可以跨文件内联。在 CMake 中：

```cmake
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)     # 或者 -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
```

在第二步的版本上打开 LTO，测得 73.7 ms，和手动内联几乎一样。LTO 的代价是链接变慢，对大项目来说可能慢很多。像 `isWordChar` 这种明确会被频繁调用的小函数，直接写成内联更简单。

### 4.4 第四步：不要重复构造字符串

现在每处理一个单词：

1. `forEachWord` 把单词复制到 `buffer_` 中，转成小写，把 `string_view` 传给回调；
2. `addWord(std::string_view word)` 中，`counts_.find(std::string(word))` 又用它构造了一个新的 `std::string`，因为 C++17 的 `unordered_map<std::string, ...>::find` 只接受 `std::string`。

第 2 步的字符串内容和 `buffer_` 完全相同。那么直接用 `buffer_` 查找就行了：

```cpp
void Counter::add(std::string_view text)
{
    // 回调收到的单词就在 buffer_ 中，直接用 buffer_ 查找，不用再构造一个 std::string
    forEachWord(text, buffer_, [this](std::string_view) { bump(buffer_, 1); });
}

void Counter::addWord(std::string_view word, std::int64_t times)
{
    bump(std::string(word), times);
}

void Counter::bump(const std::string& word, std::int64_t times)
{
    auto it = counts_.find(word);
    if (it == counts_.end()) it = counts_.emplace(word, 0).first;
    it->second += times;
    total_ += times;
}
```

| 版本 | 16 MB 用时 | 速度 |
|---|---|---|
| `isWordChar` 内联 | 76 ms | 210 MB/s |
| 不重复构造字符串 | **67 ms** | 239 MB/s |

（这两次是在同一时间、交替运行测得的，第三步的结果和上一节差了 2 ms，就是测量本身的波动。）

C++20 可以让 `unordered_map` 直接用 `std::string_view` 查找（给哈希函数和比较函数加上 `is_transparent`，叫作**异构查找**），就不需要这个技巧了。

### 4.5 小结：四步的效果

<figure class="diagram">
<svg viewBox="0 0 700 250" role="img" aria-label="统计 16 MB 文本的用时：第一版 201 毫秒，换成 unordered_map 98，去掉中间的 vector 79，isWordChar 内联 74，不重复构造字符串 67，再用 4 个线程 29">
  <defs><marker id="st-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="st-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="170.0" y="35.0" text-anchor="end" style="fill:var(--fg);font-size:12px">第一版</text><rect x="180" y="16" width="440.0" height="26" rx="4" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="626.0" y="34.0" text-anchor="start" style="fill:var(--fg);font-size:12px">201 ms</text><text x="170.0" y="71.0" text-anchor="end" style="fill:var(--fg);font-size:12px">① unordered_map</text><rect x="180" y="52" width="214.5" height="26" rx="4" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="400.5" y="70.0" text-anchor="start" style="fill:var(--fg);font-size:12px">98 ms</text><text x="170.0" y="107.0" text-anchor="end" style="fill:var(--fg);font-size:12px">② 去掉中间的 vector</text><rect x="180" y="88" width="172.9" height="26" rx="4" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="358.9" y="106.0" text-anchor="start" style="fill:var(--fg);font-size:12px">79 ms</text><text x="170.0" y="143.0" text-anchor="end" style="fill:var(--fg);font-size:12px">③ isWordChar 内联</text><rect x="180" y="124" width="162.0" height="26" rx="4" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="348.0" y="142.0" text-anchor="start" style="fill:var(--fg);font-size:12px">74 ms</text><text x="170.0" y="179.0" text-anchor="end" style="fill:var(--fg);font-size:12px">④ 不重复构造字符串</text><rect x="180" y="160" width="146.7" height="26" rx="4" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="332.7" y="178.0" text-anchor="start" style="fill:var(--fg);font-size:12px">67 ms</text><text x="170.0" y="215.0" text-anchor="end" style="fill:var(--fg);font-size:12px">④ + 4 个线程</text><rect x="180" y="196" width="63.5" height="26" rx="4" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="249.5" y="214.0" text-anchor="start" style="fill:var(--fg);font-size:12px">29 ms</text><text x="14.0" y="238.0" text-anchor="start" style="fill:var(--muted);font-size:11px">Apple M6，Google Benchmark 的中位数；每一步都通过了全部 23 个测试</text>
  </g>
</svg>
</figure>

| 版本 | 16 MB 用时 | 相对第一版 |
|---|---|---|
| 第一版：`std::map` + 中间的 `vector` | 201 ms | 1× |
| ① `std::unordered_map` | 98 ms | 2.1× |
| ② 去掉中间的 `vector` | 79 ms | 2.5× |
| ③ `isWordChar` 内联 | 74 ms | 2.7× |
| ④ 不重复构造字符串 | 67 ms | **3.0×** |

剩下的时间主要在哈希表中：`std::unordered_map` 的每个元素是单独分配的节点，查找时要沿着指针跳到不同的内存位置。再往下优化，可以换成开放寻址的哈希表（比如 Abseil 的 `flat_hash_map`），但那要引入新的依赖。对 wordfreq 来说，3 倍已经够了，**知道什么时候停下来**，也是优化的一部分。

## 5. 多线程

最后看看 `countParallel` 用多个线程的效果：

```
BM_CountParallel/1/real_time_median       67.2 ms
BM_CountParallel/2/real_time_median       41.5 ms
BM_CountParallel/4/real_time_median       29.1 ms
```

（`UseRealTime()` 让框架按实际经过的时间计算，多线程的测试必须这样，CPU 时间只算了主线程。）

4 个线程快了 2.3 倍，而不是 4 倍。原因有几个：

1. **合并是串行的**。每个线程统计完自己的一段以后，主线程把它们逐个合并。每个局部结果都有接近 20000 个不同的单词，合并的时间不随线程数减少。一个程序中不能并行的部分占的比例，决定了加速的上限，这叫**阿姆达尔定律**；
2. **核心不一样快**。这台电脑有 2 个性能核和 4 个能效核，第 3、4 个线程很可能跑在能效核上；
3. **线程的创建和切分文本**也有开销，文本越小，占的比例越大。

所以多线程的收益要实际测量，不能按核数推算。

## 6. 常见错误

1. **在 Debug 构建中测量性能**。没有优化时，函数调用、`std::vector` 的边界检查等开销被放大，热点完全不同。
2. **基准测试中的结果没有被使用**。编译器把计算整个删掉了，测出来的时间是 0，或者快得不可思议。用 `DoNotOptimize`。
3. **只测一次**。几个百分点的差别可能只是波动。用 `--benchmark_repetitions` 多测几次，看中位数和标准差。
4. **凭感觉优化**。没有测量就修改，改完也不知道有没有效果。
5. **perf 报告中看不到调用关系**。优化时帧指针被省略了，`-g` 采集不到完整的调用栈。编译时加 `-fno-omit-frame-pointer`，或者用 `perf record --call-graph dwarf`（不需要重新编译，但数据文件大得多）。
6. **为了速度牺牲正确性**。每一步优化都要运行全部测试，最好再用 Sanitizer 运行一遍：第四步的 `bump(buffer_, 1)` 依赖 `buffer_` 中恰好是当前的单词，如果以后有人修改 `forEachWord`，这个假设就可能被打破，测试会发现它。

## 7. 小结

- 优化的顺序：可重复的测量 → 找出热点 → 改一处测一次，并且保证测试通过；
- Google Benchmark 测量一段代码的速度，注意 `DoNotOptimize`、重复测量；
- perf 采样找出热点，自身时间说明「时间花在这个函数的代码中」，累计时间说明「这个函数和它调用的函数一共花了多少」；火焰图把调用栈画在一起，最宽的块就是最该关注的地方；
- 收益最大的优化通常是算法和数据结构（换容器快了一倍），其次是减少不必要的工作（去掉中间结果、避免重复构造），最后才是内联这类细节。

## 8. 练习

1. 把 `bench/text_gen.h` 中的词汇表从 20000 个改成 200 个，重新测量第一版和第一步（`std::unordered_map`）。差距还有这么大吗？为什么？
2. 在第四步的基础上，给 `counts_` 调用一次 `reserve(30000)`（在 `Counter` 的构造函数中），会更快吗？测一测。
3. 用 perf 的 `perf stat` 看一下第一版和第四步的指令数（`instructions`）和缓存未命中（`cache-misses`）。哪一个的变化更大？（在 Docker 等虚拟机中，硬件计数器可能不可用。）

<details>
<summary>点击查看答案</summary>

1. 实测（16 MB 文本）：第一版 156 ms，换成 `std::unordered_map` 以后 96 ms，快了 1.6 倍；20000 个单词时是 2.1 倍。差距变小了：只有 200 个单词时，红黑树只有 8 层左右，整棵树都在 CPU 缓存中，每次查找的代价低得多。（单词少了，每个单词也更短，所以两组数字不能直接比较绝对值。）这说明测试数据必须接近真实的情况：数据的特征变了，优化的收益也会变。

2. 实测反而**慢了** 8%：65.5 ms 变成了 70.9 ms（重复测了两轮，结果一致）。哈希表只在插入新单词时可能扩容，20000 个单词只扩容十几次，和 300 万次查找相比本来就可以忽略，`reserve` 省下的很少；而预留 30000 个元素以后桶数组变大了，查找时访问的内存更分散，缓存的命中率下降。我做这个实验之前，也以为它会快一点。这正是「改一处，测一次」的意义。

3. 一般来说，第一版的指令数多得多（构造和销毁 300 万个字符串、树的十几次比较），缓存未命中也多（红黑树的节点分散在内存中）。具体的数字取决于 CPU，这正是这个练习的目的：用工具看，而不是猜。

</details>

下一篇：[代码格式、静态检查与持续集成](/posts/cppeng-format-tidy-ci/)。把前面的所有检查交给机器，每次提交时自动运行。
