---
title: 单元测试：GoogleTest 与 CTest
date: 2026-11-04 10:20:00
updated: 2026-11-04 10:20:00
categories:
  - C/C++
  - C++工程实践
tags:
  - C++
  - 测试
  - GoogleTest
  - CMake
order: 3
description: 后面两篇要给 wordfreq 找 bug、做优化，每改一次代码都要确认没有改坏。本文用 GoogleTest 给分词和计数写单元测试，讲清楚断言、测试夹具和参数化测试；再用 CTest 把单元测试和命令行程序的测试统一起来运行。还会故意改坏一行代码，看看失败的测试是怎样指出问题的。
disableNunjucks: true
---

> **本文目标**：会用 FetchContent 引入 GoogleTest，用 `gtest_discover_tests` 把每个测试注册到 CTest；会写 `TEST`、`TEST_F`、`TEST_P` 三种测试，分清 `EXPECT_*` 和 `ASSERT_*`；知道该测什么；会用 CTest 筛选、并行运行测试，以及测试命令行程序。
>
> **前置知识**：[上一篇](/posts/cppeng-dependencies/)。
>
> **演示环境**：macOS + Apple Clang 21 + CMake 4.4，GoogleTest 1.17。

## 1. 为什么要写测试

wordfreq 现在能用了。接下来的两篇，要用 Sanitizer 检查它的内存错误和数据竞争，还要优化它的速度。每改一次代码，都要回答同一个问题：**改完以后，结果还对吗？**

手动验证的方法是运行几个例子，看看输出对不对。这能做一两次，但做不了几十次；而且时间一长，就不记得当初试过哪些情况了。单元测试就是把这些「试过的情况」写成代码：

```cpp
TEST(Tokenizer, StripsApostrophesAroundWords)
{
    EXPECT_EQ(tokenize("'quoted' ''' rock'n'roll'"), (Words{"quoted", "rock'n'roll"}));
}
```

它会一直留在项目中，每次改代码以后，一条命令全部运行一遍，几秒钟就知道有没有改坏。

## 2. 引入 GoogleTest

[GoogleTest](https://github.com/google/googletest) 是 C++ 最常用的测试框架。和上一篇的 CLI11 一样，用 FetchContent 引入，写在 `tests/CMakeLists.txt` 中：

```cmake
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
```

- 测试是一个普通的可执行文件，链接被测的库 `wordfreq::wordfreq`。第一篇把功能写成库，好处在这里体现出来了：测试不需要重新编译 `tokenizer.cpp` 和 `counter.cpp`；
- `GTest::gtest_main` 提供了 `main` 函数，它会运行所有的测试，测试文件中只需要写测试；
- `INSTALL_GTEST OFF`：GoogleTest 默认会在 `cmake --install` 时把自己也装上，我们不需要。

顶层的 `CMakeLists.txt` 用一个选项控制是否构建测试：

```cmake
option(WORDFREQ_BUILD_TESTS "构建单元测试" ${PROJECT_IS_TOP_LEVEL})

if(WORDFREQ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

默认值是 `PROJECT_IS_TOP_LEVEL`：自己开发时构建测试；wordfreq 被别的项目引入时，不构建它的测试，也就不会下载 GoogleTest。

`enable_testing()` 打开 CTest，必须写在顶层的 `CMakeLists.txt` 中，`ctest` 命令是从构建目录的根开始找测试的。

<figure class="diagram">
<svg viewBox="0 0 700 150" role="img" aria-label="gtest_discover_tests 的过程：编译出 wordfreq_tests 以后，用 --gtest_list_tests 运行它，得到所有测试的名字，再逐个注册成 CTest 测试，ctest 就能单独筛选和并行运行它们">
  <defs><marker id="gd-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="gd-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="14" y="40" width="140" height="52" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="84.0" y="62.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">编译</text><text x="84.0" y="78.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">wordfreq_tests</text><rect x="190" y="40" width="150" height="52" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="265.0" y="62.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">运行一次</text><text x="265.0" y="78.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">--gtest_list_tests</text><rect x="376" y="40" width="170" height="52" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="461.0" y="62.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">得到每个测试的名字</text><text x="461.0" y="78.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">Tokenizer.… CounterTest.…</text><rect x="582" y="40" width="104" height="52" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="634.0" y="62.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">注册到</text><text x="634.0" y="78.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">CTest</text><line x1="154" y1="66" x2="188" y2="66" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#gd-a)"/><line x1="340" y1="66" x2="374" y2="66" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#gd-a)"/><line x1="546" y1="66" x2="580" y2="66" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#gd-a)"/><text x="14.0" y="130.0" text-anchor="start" style="fill:var(--muted);font-size:11px">所以 ctest -R、ctest -j 能以单个 GoogleTest 测试为单位筛选、并行</text>
  </g>
</svg>
</figure>

## 3. 第一组测试

### 3.1 TEST 和断言

`tests/tokenizer_test.cpp`：

```cpp
#include "wordfreq/tokenizer.h"

#include <gtest/gtest.h>

using wordfreq::tokenize;
using Words = std::vector<std::string>;

TEST(Tokenizer, SplitsOnSpacesAndPunctuation)
{
    EXPECT_EQ(tokenize("Hello, world!"), (Words{"hello", "world"}));
    EXPECT_EQ(tokenize("  one\ttwo\nthree  "), (Words{"one", "two", "three"}));
}

TEST(Tokenizer, EmptyAndSeparatorOnlyInput)
{
    EXPECT_TRUE(tokenize("").empty());
    EXPECT_TRUE(tokenize(" ,.;!? \n").empty());
}
```

`TEST(测试套件名, 测试名)` 定义一个测试，同一类的测试用同一个套件名。名字里不要有下划线，GoogleTest 内部会用下划线把两个名字拼起来，可能产生冲突。

断言有两个系列：

| 断言 | 失败时 |
|---|---|
| `EXPECT_EQ(a, b)`、`EXPECT_TRUE(x)`…… | 记录失败，**继续**执行这个测试后面的语句 |
| `ASSERT_EQ(a, b)`、`ASSERT_TRUE(x)`…… | 记录失败，**立即结束**这个测试 |

一般用 `EXPECT_*`，一次运行能看到更多的问题。如果后面的语句依赖前面的结果，就用 `ASSERT_*`，比如：

```cpp
auto top = counter.top(3);
ASSERT_EQ(top.size(), 3u);      // 数量不对，下面的 top[2] 就越界了，不能继续
EXPECT_EQ(top[2].word, "end");
```

常用的还有：`EXPECT_NE`、`EXPECT_LT`、`EXPECT_GE` 等比较大小；`EXPECT_STREQ` 比较 C 字符串的内容（`EXPECT_EQ` 比较的是两个指针）；`EXPECT_DOUBLE_EQ` 比较浮点数（允许最后几位有误差）；`EXPECT_THROW(语句, 异常类型)` 检查是否抛出异常。

注意 `EXPECT_EQ(tokenize(...), (Words{"hello", "world"}))` 中第二个参数外面的括号：`EXPECT_EQ` 是一个宏，宏的参数用逗号分隔，`Words{"hello", "world"}` 中的逗号会被当成参数之间的分隔符。加上括号就好了。

### 3.2 测什么

测试最有价值的地方是**边界和特殊情况**，正常的情况往往一试就对。对分词来说：

| 测试 | 检查的情况 |
|---|---|
| `SplitsOnSpacesAndPunctuation` | 空格、Tab、换行、标点都是分隔符 |
| `EmptyAndSeparatorOnlyInput` | 空字符串、只有分隔符 |
| `ConvertsToLowerCase` | 大小写不同的同一个词 |
| `KeepsApostrophesInsideWords` | `don't`、`it's` 中的撇号属于单词 |
| `StripsApostrophesAroundWords` | `'quoted'` 两边的撇号不属于单词；只有撇号的 `'''` 不是单词 |
| `DigitsArePartOfWords` | `R2D2` 是一个词，`3.14` 是两个 |
| `NonAsciiBytesAreSeparators` | `café` 会被切成 `caf`：已知的限制 |

最后一个值得说明。wordfreq 只认识 ASCII 字母，`café` 中的 `é` 在 UTF-8 中是两个字节，都会被当成分隔符。这不是我们想要的结果，但现在就是这样工作的：

```cpp
// 非 ASCII 的字节都是分隔符：这是已知的限制，测试把它固定下来，以后改了规则测试会提醒
TEST(Tokenizer, NonAsciiBytesAreSeparators)
{
    EXPECT_EQ(tokenize("caf\xc3\xa9 na\xc3\xafve"), (Words{"caf", "na", "ve"}));
}
```

把已知的行为（包括限制）写成测试，好处是：以后有人修改了规则，这个测试会失败，提醒他确认这是有意的修改，并同时更新测试。

### 3.3 一个失败的测试是什么样的

假设在实现「去掉两端的撇号」时，只写了去掉开头的那一行，漏掉了结尾的：

```cpp
while (begin < end && text[begin] == '\'') ++begin;     // 去掉开头的撇号
// 漏掉了：while (end > begin && text[end - 1] == '\'') --end;
```

直接运行测试程序：

```
$ ./build/debug/tests/wordfreq_tests --gtest_filter='Tokenizer.*'
...
[ RUN      ] Tokenizer.StripsApostrophesAroundWords
./tests/tokenizer_test.cpp:32: Failure
Expected equality of these values:
  tokenize("'quoted' ''' rock'n'roll'")
    Which is: { "quoted'", "rock'n'roll'" }
  (Words{"quoted", "rock'n'roll"})
    Which is: { "quoted", "rock'n'roll" }

[  FAILED  ] Tokenizer.StripsApostrophesAroundWords (0 ms)
...
[  PASSED  ] 6 tests.
[  FAILED  ] 1 test, listed below:
[  FAILED  ] Tokenizer.StripsApostrophesAroundWords
```

失败信息给出了文件和行号、两边的表达式和它们实际的值。`{ "quoted'", "rock'n'roll'" }`：结尾的撇号都留下来了，问题在哪里一目了然。`--gtest_filter` 只运行名字匹配的测试，可以用 `*` 通配，多个模式用 `:` 分隔，前面加 `-` 表示排除。

一个好的测试，失败时应该让人**不用调试就能猜到原因**。所以每个测试只检查一件事，名字说明它检查的是什么。

## 4. 测试夹具：TEST_F

计数器的几个测试都要先统计同一段文本。每个测试都写一遍很啰嗦，用**测试夹具**（fixture）：

```cpp
// 测试夹具：每个 TEST_F 开始前都会构造一个新的 CounterTest，counter 中已经统计好了 text
class CounterTest : public ::testing::Test {
protected:
    void SetUp() override { counter.add("the cat and the hat. The end, and the END"); }
    Counter counter;
};

TEST_F(CounterTest, CountsTotalAndUnique)
{
    EXPECT_EQ(counter.total(), 10);
    EXPECT_EQ(counter.unique(), 5u);
}

TEST_F(CounterTest, TopSortsByCountThenWord)
{
    // and 和 end 都是 2 次，按字典序 and 在前
    std::vector<Entry> expected{{"the", 4}, {"and", 2}, {"end", 2}};
    EXPECT_EQ(counter.top(3), expected);
}

TEST_F(CounterTest, Merge)
{
    Counter other;
    other.add("cat dog");
    counter.merge(other);
    EXPECT_EQ(counter.total(), 12);
    EXPECT_EQ(counter.count("cat"), 2);
}
```

`TEST_F` 的第一个参数是夹具类的名字。测试体中可以直接使用夹具的 `protected` 成员。

关键的一点是：**每个 `TEST_F` 都会构造一个新的夹具对象**。`Merge` 测试往 `counter` 中加了两个单词，不会影响其他测试，它们拿到的 `counter` 都是刚刚 `SetUp` 过的。测试之间互不影响，才能单独运行、以任意顺序运行、并行运行。

<figure class="diagram">
<svg viewBox="0 0 700 200" role="img" aria-label="测试夹具的生命周期：每个 TEST_F 都构造一个新的 CounterTest 对象，调用 SetUp，运行测试体，再析构；Merge 测试修改了 counter，不会影响下一个测试">
  <defs><marker id="fx-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="fx-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="14" y="14" width="212" height="150" rx="8" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="120.0" y="36.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">CountsTotalAndUnique</text><text x="30.0" y="64.0" text-anchor="start" style="fill:var(--fg);font-size:12px">① 构造 CounterTest</text><text x="30.0" y="90.0" text-anchor="start" style="fill:var(--fg);font-size:12px">② SetUp()：统计那句话</text><text x="30.0" y="116.0" text-anchor="start" style="fill:var(--fg);font-size:12px">③ 测试体</text><text x="30.0" y="142.0" text-anchor="start" style="fill:var(--muted);font-size:12px">④ 析构</text><rect x="244" y="14" width="212" height="150" rx="8" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="350.0" y="36.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">Merge</text><text x="260.0" y="64.0" text-anchor="start" style="fill:var(--fg);font-size:12px">① 构造 CounterTest</text><text x="260.0" y="90.0" text-anchor="start" style="fill:var(--fg);font-size:12px">② SetUp()：统计那句话</text><text x="260.0" y="116.0" text-anchor="start" style="fill:var(--fg);font-size:12px">③ 测试体：merge(&quot;cat dog&quot;)</text><text x="260.0" y="142.0" text-anchor="start" style="fill:var(--muted);font-size:12px">④ 析构</text><rect x="474" y="14" width="212" height="150" rx="8" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="580.0" y="36.0" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">TopSortsByCountThenWord</text><text x="490.0" y="64.0" text-anchor="start" style="fill:var(--fg);font-size:12px">① 构造 CounterTest</text><text x="490.0" y="90.0" text-anchor="start" style="fill:var(--fg);font-size:12px">② SetUp()：统计那句话</text><text x="490.0" y="116.0" text-anchor="start" style="fill:var(--fg);font-size:12px">③ 测试体</text><text x="490.0" y="142.0" text-anchor="start" style="fill:var(--muted);font-size:12px">④ 析构</text><text x="14.0" y="188.0" text-anchor="start" style="fill:var(--muted);font-size:11px">中间的测试修改了 counter，但下一个测试拿到的是新构造、新 SetUp 的对象：测试之间互不影响</text>
  </g>
</svg>
</figure>

`EXPECT_EQ(counter.top(3), expected)` 能够比较两个 `std::vector<Entry>`，是因为 `Entry` 定义了 `operator==`；失败时要显示它们的值，GoogleTest 会尝试用 `operator<<` 输出，没有的话就输出对象的字节。为了失败信息更好读，可以给 `Entry` 定义一个 `PrintTo` 函数或者 `operator<<`，这留作练习。

## 5. 参数化测试：TEST_P

`countParallel` 用多个线程统计，线程数是一个参数。不管用几个线程，结果都应该和单线程完全相同。线程数 1、2、3、4、8 各写一个测试，内容几乎一样；用**参数化测试**写一次就行了：

```cpp
class ParallelTest : public ::testing::TestWithParam<unsigned> {};

TEST_P(ParallelTest, SameAsSingleThread)
{
    std::string text;
    for (int i = 0; i < 200; ++i) text += "alpha beta, gamma! alpha's beta-gamma\n";
    Counter single;
    single.add(text);
    Counter parallel = wordfreq::countParallel(text, GetParam());
    EXPECT_EQ(parallel.total(), single.total());
    EXPECT_EQ(parallel.top(10), single.top(10));
}

INSTANTIATE_TEST_SUITE_P(Threads, ParallelTest, ::testing::Values(1u, 2u, 3u, 4u, 8u));
```

`GetParam()` 取得当前的参数。`INSTANTIATE_TEST_SUITE_P` 用 5 个参数值各生成一个测试，名字是 `Threads/ParallelTest.SameAsSingleThread/1` 等。

为什么选 3？因为文本的长度不一定能被线程数整除，3 个线程时每一段的边界几乎一定会落在某个单词的中间，可以检查「在单词边界处切开」的逻辑。测试用的文本也故意包含了撇号和连字符。**参数和数据的选择，要针对代码中最容易出错的地方。**

除了 `Values`，还有 `Range(1, 9)`（1 到 8）、`Bool()`（`false` 和 `true`）、`Combine(...)`（几组参数的所有组合）等。

## 6. CTest

### 6.1 运行测试

`gtest_discover_tests` 在编译完测试程序以后，运行它一次（加上 `--gtest_list_tests` 参数），列出其中所有的测试，再逐个注册到 CTest 中。所以在 CTest 看来，每个 GoogleTest 的测试都是一个独立的测试：

```
$ ctest --preset debug
Test project ./build/debug
      Start  1: Tokenizer.SplitsOnSpacesAndPunctuation
 1/21 Test  #1: Tokenizer.SplitsOnSpacesAndPunctuation ......   Passed    0.00 sec
      Start  2: Tokenizer.EmptyAndSeparatorOnlyInput
 2/21 Test  #2: Tokenizer.EmptyAndSeparatorOnlyInput ........   Passed    0.00 sec
...
      Start 18: Threads/ParallelTest.SameAsSingleThread/8
18/21 Test #18: Threads/ParallelTest.SameAsSingleThread/8 ...   Passed    0.00 sec
      Start 19: cli.counts
19/21 Test #19: cli.counts ..................................   Passed    0.28 sec
...
100% tests passed out of 21

Total Test time (real) =   0.33 sec
```

`ctest --preset debug` 用的是 `CMakePresets.json` 中的测试预设：

```json
"testPresets": [
  { "name": "base", "hidden": true, "output": { "outputOnFailure": true } },
  { "name": "debug", "inherits": "base", "configurePreset": "debug" }
]
```

`outputOnFailure` 表示测试失败时显示它的输出。CTest 默认不显示测试的输出，只告诉你哪一个失败了：

```
95% tests passed, 1 tests failed out of 21

The following tests FAILED:
	  5 - Tokenizer.StripsApostrophesAroundWords (Failed)
Errors while running CTest
```

常用的选项：

| 命令 | 作用 |
|---|---|
| `ctest -N` | 只列出测试，不运行 |
| `ctest -R Apostrophes` | 只运行名字匹配正则表达式的测试；`-E` 是排除 |
| `ctest -j 8` | 8 个测试并行运行 |
| `ctest --rerun-failed` | 只运行上次失败的测试 |
| `ctest --output-on-failure` | 失败时显示输出（不用预设时） |
| `ctest --repeat until-fail:100` | 重复运行，直到失败为止，用来抓偶尔才失败的测试 |

`-j` 能用，正是因为每个测试互不影响（第 4 节）。

### 6.2 测试命令行程序

单元测试检查的是库，`main.cpp` 中的参数解析、输出格式没有被测到。CTest 可以直接运行任何命令作为测试：

```cmake
# 命令行程序的测试：运行 wordfreq，用正则表达式检查输出
add_test(NAME cli.counts COMMAND wordfreq_cli -n 2 ${CMAKE_CURRENT_SOURCE_DIR}/data/dickens.txt)
set_tests_properties(cli.counts PROPERTIES
    PASS_REGULAR_EXPRESSION "单词 24 个，不同的 10 个\n +1  it +4  +16.67%")

add_test(NAME cli.rejects_zero COMMAND wordfreq_cli -n 0 ${CMAKE_CURRENT_SOURCE_DIR}/data/dickens.txt)
set_tests_properties(cli.rejects_zero PROPERTIES WILL_FAIL TRUE)    # 退出码不是 0 才算通过

add_test(NAME cli.missing_file COMMAND wordfreq_cli no-such-file.txt)
set_tests_properties(cli.missing_file PROPERTIES WILL_FAIL TRUE)
```

- `COMMAND` 中写的是**目标名** `wordfreq_cli`，CTest 会替换成生成的程序的完整路径，不用关心它在哪个目录、在 Windows 上叫不叫 `wordfreq.exe`；
- `PASS_REGULAR_EXPRESSION`：输出匹配这个正则表达式才算通过（这时不再检查退出码）；
- `WILL_FAIL`：把结果反过来，程序失败（退出码不是 0）才算通过。用来检查错误的参数确实被拒绝了。

这种测试很粗糙，只能检查输出的一部分。如果要逐字比较输出，可以像[数据结构专栏的实战项目](/posts/ds-project-metro/)那样写一个测试脚本，把期望的输出放在文件中。

## 7. 让代码容易测试

回头看 wordfreq 的设计，有几处是为了测试才这样写的：

1. **功能在库中，`main` 只是外壳**（第一篇）。测试直接调用 `tokenize`、`Counter`，不用通过命令行。
2. **函数的输入输出是值，不是文件和终端**。`Counter::add` 接收的是 `std::string_view`，而不是文件名；`top` 返回 `std::vector<Entry>`，而不是直接打印。测试中构造一个字符串就能调用，结果也能直接比较。
3. **会变化的东西作为参数传进来**。`countParallel` 的线程数是参数，而不是在函数内部调用 `std::thread::hardware_concurrency()`。否则在 2 核和 16 核的电脑上，测试测到的是不同的情况，3 个线程的情况可能永远测不到。

反过来，如果一段代码很难测试，比如必须准备好文件、网络、特定的时间才能运行，通常说明它的职责太多了，应该把「计算」和「输入输出」分开。

## 8. 常见错误

1. **测试之间共享状态**。用全局变量或者静态变量在测试之间传递数据，单独运行某个测试、或者用 `ctest -j` 并行运行时，结果就不一样了。每个测试自己准备数据，或者用夹具。
2. **`EXPECT_EQ` 的参数中有逗号**。`EXPECT_EQ(v, std::vector<int>{1, 2})` 编译失败，错误信息很长。给参数加上括号。
3. **用 `EXPECT_EQ` 比较 C 字符串**。比较的是指针，用 `EXPECT_STREQ`。
4. **用 `EXPECT_EQ` 比较浮点数**。`0.1 + 0.2` 不等于 `0.3`。用 `EXPECT_DOUBLE_EQ` 或者 `EXPECT_NEAR(a, b, 误差)`。
5. **测试通过了，但什么也没测**。比如测试中调用了函数，却忘了写断言。可以先故意改坏被测的代码，确认测试会失败（像 3.3 节那样），再改回来。
6. **`enable_testing()` 写在了子目录中**。在构建目录的根运行 `ctest`，显示 `No tests were found!!!`。

## 9. 小结

- 测试是一个普通的程序，链接被测的库；GoogleTest 用 FetchContent 引入，`gtest_discover_tests` 把每个测试注册到 CTest；
- `EXPECT_*` 失败后继续，`ASSERT_*` 失败后结束这个测试；
- 重点测试边界和特殊情况，已知的限制也写成测试；
- 夹具让几个测试共用准备数据的代码，并且每个测试拿到的都是新的对象；参数化测试用同一份代码测试多组参数；
- CTest 统一运行单元测试和命令行测试，可以筛选、并行、重复运行。

## 10. 练习

1. 给 `Entry` 定义 `void PrintTo(const Entry& e, std::ostream* os)`，输出 `{the, 4}` 这样的格式。故意让 `TopSortsByCountThenWord` 失败，对比定义前后的失败信息。
2. `counter.top(0)` 和 `counter.top(100)` 已经测试过了。`Counter` 还有哪些边界情况没有测到？
3. 给 `cli.counts` 加一个测试，检查从标准输入读取时的结果。（提示：`add_test` 的 `COMMAND` 不经过 shell，不能用 `<` 重定向。可以用 `${CMAKE_COMMAND} -E` 系列命令，或者写一个小脚本。）

<details>
<summary>点击查看答案</summary>

1. 在 `counter.h` 中、`namespace wordfreq` 里面加上：

```cpp
inline void PrintTo(const Entry& e, std::ostream* os) { *os << '{' << e.word << ", " << e.count << '}'; }
```

（需要包含 `<ostream>`。）GoogleTest 通过参数相关查找（ADL）找到它，所以必须和 `Entry` 在同一个命名空间中。定义之前，失败信息中的 `Entry` 显示为一串字节，在 macOS 上是 `{ 32-byte object <74-68 65-00 ...> }`（前几个字节是 `the` 的 ASCII 码，libc++ 把短字符串直接存在对象里）；定义之后显示为 `{ {the, 4}, {and, 2}, {end, 2} }`。

2. 例如：`addWord` 的 `times` 参数大于 1；`merge` 一个空的 `Counter`，或者自己和自己 `merge`（`counter.merge(counter)` 在遍历 `counts_` 的同时修改它，虽然修改的只是已有元素的值、不会插入新元素，但这种用法很容易出错，值得用测试确认结果是每个词的次数翻倍）；`count` 查询一个大小写不同的单词（`"The"` 应该是 0，因为保存的都是小写）；单词数量很多时 `top` 的结果是否稳定。

3. 一种方法是用 CMake 自己作为 shell：

```cmake
add_test(NAME cli.stdin
    COMMAND ${CMAKE_COMMAND} -E env sh -c "$<TARGET_FILE:wordfreq_cli> -n 1 < ${CMAKE_CURRENT_SOURCE_DIR}/data/dickens.txt")
set_tests_properties(cli.stdin PROPERTIES PASS_REGULAR_EXPRESSION "单词 24 个")
```

`$<TARGET_FILE:wordfreq_cli>` 是一个生成器表达式，会被替换成程序的完整路径。不过这依赖 `sh`，在 Windows 上不能用。更可移植的做法是写一个 CMake 脚本（`cmake -P`），用 `execute_process` 的 `INPUT_FILE` 参数。

</details>

下一篇：[Sanitizer 与测试覆盖率](/posts/cppeng-sanitizers-coverage/)。测试全部通过，就没有 bug 了吗？
