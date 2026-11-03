---
title: Sanitizer 与测试覆盖率
date: 2026-11-04 10:30:00
updated: 2026-11-04 10:30:00
categories:
  - C/C++
  - C++工程实践
tags:
  - C++
  - Sanitizer
  - 测试
  - 覆盖率
order: 4
description: 测试全部通过，不代表没有 bug：越界读、使用已经释放的内存、数据竞争，在普通的构建中可能照常运行，也可能随机失败，还可能悄悄算错。本文用三个真实的 bug 演示 AddressSanitizer 和 ThreadSanitizer 怎样把它们揪出来，在 CMake 中把 Sanitizer 做成预设，让它成为日常测试的一部分；再用 llvm-cov 统计测试覆盖率，找出测试没有走到的分支。
disableNunjucks: true
---

> **本文目标**：会在 CMake 中用一个选项打开 Sanitizer，并做成预设；能读懂 AddressSanitizer 和 ThreadSanitizer 的报告，找到出错的位置；知道 Sanitizer 的代价和局限；会用 Clang 的源码级覆盖率找出没有测到的代码和分支。
>
> **前置知识**：[上一篇](/posts/cppeng-unit-testing/)；《开发环境与编程工具》第 7 节（Sanitizer 的基本用法）；《多线程入门》。
>
> **演示环境**：macOS + Apple Clang 21 + CMake 4.4。

## 1. 测试通过，就没有 bug 了吗

C++ 中有一类错误叫**未定义行为**：越界访问、使用已经释放的内存、两个线程同时修改同一个变量……标准对它们的结果不做任何规定。程序可能崩溃，可能输出错误的结果，也可能**看起来完全正常**。

最后一种最危险：测试通过了，你以为没有问题，程序到了用户手中、换了一台电脑、换了一个编译选项，才出错。

《开发环境与编程工具》中介绍过的 **Sanitizer** 正是为此设计的：编译时在程序中插入检查代码，运行时一旦发生这类错误，立即停下来报告位置。本文把它用到 wordfreq 上，看三个真实的 bug：

| bug | 普通构建中的表现 | 用什么发现 |
|---|---|---|
| `top(k)` 中 k 超过单词数时越界 | 崩溃，不知道原因 | AddressSanitizer |
| `string_view` 指向已经销毁的字符串 | 悄悄地输出错误的结果 | AddressSanitizer |
| 多个线程同时修改一个 `unordered_map` | 20 次中失败 3 次 | ThreadSanitizer |

## 2. 在 CMake 中打开 Sanitizer

### 2.1 一个选项

`cmake/Sanitizers.cmake`：

```cmake
# WORDFREQ_SANITIZE=address,undefined 或 thread：所有目标（包括第三方库）都加上 -fsanitize
if(WORDFREQ_SANITIZE)
    if(MSVC)
        add_compile_options(/fsanitize=${WORDFREQ_SANITIZE})
    else()
        add_compile_options(-fsanitize=${WORDFREQ_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=all)
        add_link_options(-fsanitize=${WORDFREQ_SANITIZE})
    endif()
endif()
```

第一篇说过尽量不用全局的 `add_compile_options`，这里是一个例外：Sanitizer 应该作用于**所有**代码，包括 GoogleTest、fmt 这些第三方库。原因有两个：

- 内存可能在一个库中分配、在另一个库中释放，只检查一部分代码，就会漏掉跨越边界的错误；
- 对 ThreadSanitizer 来说，没有插入检查的代码中的同步操作它看不到，会产生误报。

所以这个文件在顶层、在引入依赖之前被 `include`，之后定义的所有目标都会受到影响。

两个附加的选项：

- `-fno-omit-frame-pointer`：保留帧指针，报告中的调用栈更完整；
- `-fno-sanitize-recover=all`：UndefinedBehaviorSanitizer 默认只打印错误、继续运行，测试会照常通过，错误淹没在输出中。加上这个选项，发现错误立即结束程序，测试就会失败。

### 2.2 做成预设

```json
{ "name": "asan", "displayName": "AddressSanitizer + UndefinedBehaviorSanitizer", "inherits": "debug",
  "cacheVariables": { "WORDFREQ_SANITIZE": "address,undefined" } },
{ "name": "tsan", "displayName": "ThreadSanitizer", "inherits": "debug",
  "cacheVariables": { "WORDFREQ_SANITIZE": "thread" } }
```

AddressSanitizer（ASan）和 UndefinedBehaviorSanitizer（UBSan）可以同时打开，ThreadSanitizer（TSan）和 ASan 不能同时使用，所以分成两个预设。用法和普通的构建一样：

```bash
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

它们继承了 `debug`：没有优化，报告中的行号最准确。

## 3. 例一：越界读

`Counter::top(k)` 返回出现次数最多的 k 个单词。第一版的实现忘了处理 k 大于单词总数的情况：

```cpp
std::vector<Entry> Counter::top(std::size_t k) const
{
    std::vector<Entry> all;
    // ……把所有单词放进 all……
    // 漏掉了：k = std::min(k, all.size());
    std::partial_sort(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(k), all.end(), before);
    all.resize(k);
    return all;
}
```

上一篇的测试 `TopWithKLargerThanUnique` 调用了 `counter.top(100)`，而 counter 中只有 5 个单词；`EmptyCounter` 和 `ParallelTest` 也调用了 `top(10)`，单词都不到 10 个。普通的 Debug 构建中，这几个测试直接崩溃：

```
$ ctest --preset debug
...
The following tests FAILED:
	 11 - CounterTest.TopWithKLargerThanUnique (Bus error)
	 13 - Counter.EmptyCounter (SEGFAULT)
	 14 - Threads/ParallelTest.SameAsSingleThread/1 (Bus error)
	 ...
	 18 - Threads/ParallelTest.SameAsSingleThread/8 (Bus error)
```

测试失败了，这很好。但「Bus error」「SEGFAULT」除了说明程序访问了不该访问的内存以外，什么也没有告诉我们。用 ASan 构建再运行第一个测试：

```
==22676==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x60e000000cb8 at pc 0x000101008360 ...
READ of size 8 at 0x60e000000cb8 thread T0
    #0 in wordfreq::Counter::top(unsigned long) const::$_0::operator()(...) const counter.cpp:41
    #1 in void std::__1::__sift_down<...>(...) sift_down.h:55
    #2 in void std::__1::__make_heap<...>(...) make_heap.h:49
    #3 in std::__1::__partial_sort_impl<...>(...) partial_sort.h:41
    ...
    #6 in wordfreq::Counter::top(unsigned long) const counter.cpp:43
    #7 in CounterTest_TopWithKLargerThanUnique_Test::TestBody() counter_test.cpp:42
    ...
0x60e000000cb8 is located 3032 bytes after 160-byte region [0x60e000000040,0x60e0000000e0)
allocated by thread T0 here:
    #0 in operator new(unsigned long)
    ...
```

（为了便于阅读，删去了地址和过长的模板参数。）从上往下读：

1. **错误的类型**：`heap-buffer-overflow`，堆上的缓冲区越界；`READ of size 8`，读了 8 个字节；
2. **出错时的调用栈**：在 `counter.cpp:43` 调用的 `std::partial_sort` 中，比较函数（`counter.cpp:41`，就是那个 lambda）读了越界的内存。往下是测试 `counter_test.cpp:42`；
3. **越界的地址在哪里**：在一块 160 字节的内存之后 3032 字节。160 字节正好是 5 个 `Entry`（每个 32 字节），这块内存就是 `all` 的缓冲区。`partial_sort` 拿到的中间位置是 `begin() + 100`，远远超出了 5 个元素。

问题的位置和原因都清楚了。

<figure class="diagram">
<svg viewBox="0 0 700 170" role="img" aria-label="AddressSanitizer 的红区：all 的缓冲区是 160 字节，正好 5 个 Entry，前后是不能访问的红区；partial_sort 用的 begin()+100 远在缓冲区之后，读到了不能访问的内存">
  <defs><marker id="rz-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rz-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="14" y="50" width="60" height="46" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="44.0" y="77.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">红区</text><rect x="74" y="50" width="56" height="46" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="102.0" y="69.3" text-anchor="middle" style="fill:var(--fg);font-size:11px">Entry 0</text><text x="102.0" y="84.3" text-anchor="middle" style="fill:var(--muted);font-size:10px">32 字节</text><rect x="130" y="50" width="56" height="46" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="158.0" y="69.3" text-anchor="middle" style="fill:var(--fg);font-size:11px">Entry 1</text><text x="158.0" y="84.3" text-anchor="middle" style="fill:var(--muted);font-size:10px">32 字节</text><rect x="186" y="50" width="56" height="46" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="214.0" y="69.3" text-anchor="middle" style="fill:var(--fg);font-size:11px">Entry 2</text><text x="214.0" y="84.3" text-anchor="middle" style="fill:var(--muted);font-size:10px">32 字节</text><rect x="242" y="50" width="56" height="46" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="270.0" y="69.3" text-anchor="middle" style="fill:var(--fg);font-size:11px">Entry 3</text><text x="270.0" y="84.3" text-anchor="middle" style="fill:var(--muted);font-size:10px">32 字节</text><rect x="298" y="50" width="56" height="46" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="326.0" y="69.3" text-anchor="middle" style="fill:var(--fg);font-size:11px">Entry 4</text><text x="326.0" y="84.3" text-anchor="middle" style="fill:var(--muted);font-size:10px">32 字节</text><rect x="354" y="50" width="60" height="46" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="384.0" y="77.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">红区</text><text x="430.0" y="78.0" text-anchor="start" style="fill:var(--muted);font-size:13px">……</text><rect x="500" y="50" width="90" height="46" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="545.0" y="69.9" text-anchor="middle" style="fill:var(--fg);font-size:13px">？</text><text x="545.0" y="84.9" text-anchor="middle" style="fill:var(--muted);font-size:10px">begin()+100</text><line x1="545" y1="20" x2="545" y2="48" style="stroke:var(--danger);stroke-width:1.5" marker-end="url(#rz-d)"/><text x="545.0" y="14.0" text-anchor="middle" style="fill:var(--danger);font-size:11px">READ of size 8</text><text x="74.0" y="120.0" text-anchor="start" style="fill:var(--muted);font-size:11px">all 的缓冲区：160 字节</text><text x="500.0" y="120.0" text-anchor="start" style="fill:var(--danger);font-size:11px">3032 字节之后</text><text x="14.0" y="156.0" text-anchor="start" style="fill:var(--muted);font-size:11px">影子内存记录每 8 个字节能不能访问；每次读写之前先查一下，访问红区立即报错</text>
  </g>
</svg>
</figure>

ASan 是怎样发现的？它在每块分配的内存前后加上一段不能访问的「红区」（redzone），并用一块**影子内存**记录每 8 个字节是否可以访问。插入的检查代码在每次读写内存之前查一下影子内存，访问了红区或者已经释放的内存，就报告错误。所以 ASan 能发现的是**真正发生了的**非法访问，几乎没有误报。

## 4. 例二：悬空的 string_view

### 4.1 编译器能发现的情况

`main.cpp` 读取文件以后，要把内容交给 `countParallel`。有人想「`std::string_view` 只是看一下，不复制，更快」，写成了：

```cpp
std::string_view text = loadInput(files);    // loadInput 返回 std::string
```

`loadInput` 返回的 `std::string` 是一个临时对象，这一行结束时就被销毁了，`text` 指向的内存已经释放。这种简单的情况，Clang 在编译时就能发现：

```
app/main.cpp:56:29: error: object backing the pointer will be destroyed at the end of the full-expression [-Werror,-Wdangling-gsl]
```

警告当作错误的好处在这里体现出来了。

### 4.2 编译器发现不了的情况

后来又加了一个功能：Windows 的记事本保存 UTF-8 文件时，开头可能会加上三个字节 `EF BB BF`（叫作 BOM），要去掉它：

```cpp
// 去掉 UTF-8 文件开头可能有的 BOM（EF BB BF 三个字节）
std::string_view stripBom(std::string_view s)
{
    if (s.substr(0, 3) == "\xEF\xBB\xBF") s.remove_prefix(3);
    return s;
}

int main(int argc, char** argv)
{
    // ……
    std::string_view text = stripBom(loadInput(files));
    // ……
}
```

问题和 4.1 节完全一样：`loadInput` 返回的临时字符串在这一行结束时被销毁，`stripBom` 返回的 `string_view` 指向它。但这一次多了一层函数调用，编译器不知道 `stripBom` 返回的 `string_view` 指向的就是参数的内容，没有任何警告。

普通的 Debug 构建，运行：

```
$ ./build/debug/app/wordfreq -n 3 tests/data/dickens.txt
单词 0 个，不同的 0 个
```

没有崩溃，没有错误信息，只是结果不对：释放的内存被别的数据覆盖了，碰巧成了一段没有单词的内容。如果碰巧没有被覆盖，结果就是对的。上一篇的命令行测试 `cli.counts` 发现了输出不对，但只能告诉你「输出不匹配」。

ASan 构建：

```
==23388==ERROR: AddressSanitizer: heap-use-after-free on address 0x60c000000280 ...
READ of size 1 at 0x60c000000280 thread T1
    #0 in void wordfreq::forEachWord<...>(std::string_view, std::string&, ...) tokenizer.h:24
    #1 in wordfreq::Counter::add(std::string_view) counter.cpp:12
    #2 in wordfreq::countParallel(std::string_view, unsigned int)::$_0::operator()() const counter.cpp:64
    ...
0x60c000000280 is located 0 bytes inside of 120-byte region [0x60c000000280,0x60c0000002f8)
freed by thread T0 here:
    #0 in operator delete(void*)
    #1 in std::string::~basic_string()
    #2 in main main.cpp:63

previously allocated by thread T0 here:
    #0 in operator new(unsigned long)
    #1 in std::string::__grow_by_and_replace(...)
    #2 in std::string::append(char const*, unsigned long)
    ...
```

`heap-use-after-free`：使用了已经释放的堆内存。报告中有**三个**调用栈，各自回答一个问题：

- 在哪里使用的：线程 T1（`countParallel` 创建的工作线程）中的 `forEachWord`；
- 在哪里释放的：主线程 T0，`main.cpp:63`，一个 `std::string` 的析构函数，就是 `stripBom(loadInput(files))` 这一行；
- 当初在哪里分配的：`std::string::append`，也就是 `loadInput` 中拼接文件内容的时候。

修改的方法是让字符串活得足够长：

```cpp
std::string input = loadInput(files);
std::string_view text = stripBom(input);
```

`std::string_view`、迭代器、指针、引用都有这个问题：**它们不拥有所指的对象，对象的生命周期要由别人保证**。函数返回 `string_view` 时，最好在注释中写明它指向哪里；Clang 还支持 `[[clang::lifetimebound]]` 属性，标在参数上，让编译器也能检查 4.2 节这种情况。

## 5. 例三：数据竞争

### 5.1 偶尔失败的测试

`countParallel` 把文本切成几段，每个线程统计一段。有一个版本是这样合并结果的：

```cpp
Counter result;
std::vector<std::thread> workers;
for (std::string_view part : parts) {
    workers.emplace_back([&result, part] {
        Counter local;
        local.add(part);
        result.merge(local);                        // 每个线程统计完，直接合并到结果中
    });
}
for (auto& w : workers) w.join();
return result;
```

看起来很合理：每个线程先在自己的 `local` 中统计，最后才碰 `result`。但是几个线程可能**同时**执行 `result.merge`，同时往同一个 `unordered_map` 中插入元素。

上一篇的参数化测试 `ParallelTest` 正好测的是这个函数。连续运行 20 次，失败了 3 次，其中一次的输出：

```
[ RUN      ] Threads/ParallelTest.SameAsSingleThread/1
tests/counter_test.cpp:75: Failure
Expected equality of these values:
  parallel.top(10)
    Which is: { 32-byte object <67-61 6D-6D 61-00 ...>, 32-byte object <61-6C 70-68 61-00 ...>, 32-byte object <62-65 74-61 00-00 ...>, 32-byte object <62-65 74-61 00-00 ...>, ... }
  single.top(10)
    Which is: { 32-byte object <62-65 74-61 00-00 ...>, 32-byte object <67-61 6D-6D 61-00 ...>, ... }
```

把字节翻译成字符：`67 61 6D 6D 61` 是 `gamma`，`62 65 74 61` 是 `beta`。多线程的结果中 `beta` 出现了**两次**：哈希表内部的结构被同时修改，已经损坏了。

这种偶尔失败的测试最让人头疼：重新运行一次可能就通过了，很容易被当成「不稳定」而忽略。

### 5.2 ThreadSanitizer 的报告

用 `tsan` 预设构建，运行一次就够了：

```
WARNING: ThreadSanitizer: data race (pid=23692)
  Read of size 8 at 0x00016aeb1980 by thread T2:
    ...
    #4 wordfreq::Counter::addWord(std::string_view, long long) counter.cpp:17
    #5 wordfreq::Counter::merge(wordfreq::Counter const&) counter.cpp:25
    #6 wordfreq::countParallel(std::string_view, unsigned int)::$_0::operator()() const counter.cpp:67

  Previous write of size 8 at 0x00016aeb1980 by thread T1:
    ...
    #8 wordfreq::Counter::addWord(std::string_view, long long) counter.cpp:18
    #9 wordfreq::Counter::merge(wordfreq::Counter const&) counter.cpp:25
    #10 wordfreq::countParallel(std::string_view, unsigned int)::$_0::operator()() const counter.cpp:67

  Location is stack of main thread.

  Thread T2 (tid=5693077, running) created by main thread at:
    ...
    #10 wordfreq::countParallel(std::string_view, unsigned int) counter.cpp:64
    #11 ParallelTest_SameAsSingleThread_Test::TestBody() counter_test.cpp:73
```

数据竞争是指：两个线程访问同一块内存，至少一个是写，并且它们之间没有同步（锁、原子操作、`join` 等）。TSan 的报告给出了竞争的两方：

- 线程 T2 在 `counter.cpp:17`（`counts_.find`）**读**；
- 线程 T1 之前在 `counter.cpp:18`（`counts_.emplace`）**写**了同一个地址；
- 两个都是从 `counter.cpp:67` 的 `result.merge(local)` 调过来的；
- 「Location is stack of main thread」：这块内存在主线程的栈上，就是 `countParallel` 中的局部变量 `result`。

和 ASan 不同，TSan **不需要真的出错**才能发现问题。它记录每次内存访问和每次同步操作，只要两个访问之间不存在「先后」关系，就报告竞争，即使这一次运行碰巧没有冲突。所以偶尔失败的问题，用 TSan 运行一次就能稳定地发现。

<figure class="diagram">
<svg viewBox="0 0 700 220" role="img" aria-label="数据竞争：线程 T1 和 T2 统计完各自的一段以后，同时调用 result.merge，一个在 counter.cpp:18 写哈希表，一个在 counter.cpp:17 读，两者之间没有任何同步">
  <defs><marker id="rc-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rc-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="14.0" y="40.0" text-anchor="start" style="fill:var(--fg);font-size:13px;font-weight:700">线程 T1</text><text x="14.0" y="182.0" text-anchor="start" style="fill:var(--fg);font-size:13px;font-weight:700">线程 T2</text><rect x="90" y="18" width="160" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="170.0" y="40.9" text-anchor="middle" style="fill:var(--fg);font-size:13px">local.add(第 1 段)</text><rect x="270" y="18" width="180" height="36" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="360.0" y="40.9" text-anchor="middle" style="fill:var(--fg);font-size:13px">result.merge(local)</text><rect x="90" y="160" width="200" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="190.0" y="182.9" text-anchor="middle" style="fill:var(--fg);font-size:13px">local.add(第 2 段)</text><rect x="310" y="160" width="180" height="36" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="400.0" y="182.9" text-anchor="middle" style="fill:var(--fg);font-size:13px">result.merge(local)</text><rect x="530" y="88" width="156" height="46" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="608.0" y="107.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">result.counts_</text><text x="608.0" y="122.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">主线程栈上的局部变量</text><line x1="450" y1="40" x2="560" y2="86" style="stroke:var(--danger);stroke-width:1.5" marker-end="url(#rc-d)"/><text x="470.0" y="30.0" text-anchor="start" style="fill:var(--danger);font-size:11px">写（counter.cpp:18）</text><line x1="490" y1="170" x2="560" y2="136" style="stroke:var(--danger);stroke-width:1.5" marker-end="url(#rc-d)"/><text x="500.0" y="200.0" text-anchor="start" style="fill:var(--danger);font-size:11px">读（counter.cpp:17）</text><text x="90.0" y="112.0" text-anchor="start" style="fill:var(--muted);font-size:11px">两次访问之间没有锁，也没有 join：谁先谁后不确定</text>
  </g>
</svg>
</figure>

### 5.3 修改

两种方法：给 `merge` 加锁；或者每个线程只写自己的 `Counter`，全部 `join` 以后，在主线程中合并。后者是 wordfreq 现在的写法：

```cpp
std::vector<Counter> partial(parts.size());
std::vector<std::thread> workers;
for (std::size_t i = 0; i < parts.size(); ++i) {
    workers.emplace_back([&partial, &parts, i] { partial[i].add(parts[i]); });
}
for (auto& w : workers) w.join();

Counter result;
for (const auto& c : partial) result.merge(c);
return result;
```

每个线程只写 `partial[i]`，互不重叠；`join` 保证了线程中的写入在主线程读取之前完成。不用锁，也就没有等锁的开销。**能不共享，就不共享**，这是写多线程代码最简单可靠的原则。

改完以后：

```
$ ctest --preset tsan
...
100% tests passed out of 21
```

## 6. Sanitizer 的代价和局限

用 10 MB 的文本测试单线程运行（都是 Debug 构建，没有优化）：

| 构建 | 运行时间 | 内存 |
|---|---|---|
| 普通 | 0.25 秒 | 51 MB |
| ASan + UBSan | 0.65 秒（2.6 倍） | 79 MB |
| TSan | 1.77 秒（7 倍） | 112 MB |

所以 Sanitizer 只在开发和测试时使用，发布的程序不带。慢几倍对测试来说完全可以接受。

局限：

1. **只能发现执行到的代码中的错误**。没有测试走到的分支，再严重的 bug 也发现不了。这正是下一节覆盖率要解决的问题。
2. **不是所有的错误都能发现**。比如有人把 `isWordChar` 改成查一个 256 个元素的表 `table[c]`，`c` 是 `char`，遇到非 ASCII 字节时是负数，就越界了。我试了一下，表的前面恰好是别的合法数据，ASan 和 UBSan 都没有报告。（这个错误 `-Wall` 中的 `-Wchar-subscripts` 能在编译时发现，前提是没有用强制转换把警告压下去。）
3. **ASan 和 TSan 不能同时使用**，所以要分别构建、分别运行测试。第 6 篇会在持续集成中把它们都跑一遍。
4. **ASan 在 macOS 上不检查内存泄漏**，Linux 上默认检查。也可以用 Valgrind（只支持 Linux，不需要重新编译，但慢几十倍），见《GDB 与 core dump》第 5 节。

## 7. 测试覆盖率

### 7.1 统计哪些代码被执行了

**测试覆盖率**统计测试运行时，每一行、每个分支执行了多少次。Clang 的源码级覆盖率需要两个编译选项：

```cmake
# WORDFREQ_COVERAGE：Clang 的源码级覆盖率，运行后生成 .profraw 文件
if(WORDFREQ_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "WORDFREQ_COVERAGE 需要 Clang，当前编译器是 ${CMAKE_CXX_COMPILER_ID}")
    endif()
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate)
endif()
```

预设中，用环境变量 `LLVM_PROFILE_FILE` 指定数据文件的位置：

```json
{ "name": "coverage", "inherits": "base", "configurePreset": "coverage",
  "environment": { "LLVM_PROFILE_FILE": "${sourceDir}/build/coverage/profiles/%p.profraw" } }
```

`%p` 会被替换成进程号。CTest 把每个测试作为一个单独的进程运行，不加 `%p` 的话，每个测试都写同一个文件，后面的会覆盖前面的。

运行测试，合并数据，生成报告：

```bash
cmake --preset coverage && cmake --build --preset coverage
ctest --preset coverage
llvm-profdata merge -sparse build/coverage/profiles/*.profraw -o build/coverage/wordfreq.profdata
llvm-cov report build/coverage/tests/wordfreq_tests -instr-profile=build/coverage/wordfreq.profdata src include
```

（macOS 上用 `xcrun llvm-profdata`、`xcrun llvm-cov`，它们在 Xcode 中。）`llvm-cov` 需要可执行文件，因为源代码和计数器的对应关系记录在可执行文件中。最后的 `src include` 表示只统计这两个目录，不统计测试代码和第三方库。

```
Filename                      Functions  Executed       Lines  Missed Lines     Cover    Branches  Missed Branches     Cover
---------------------------------------------------------------------------------------------------------------------------
src/tokenizer.cpp                     3   100.00%          10             0   100.00%          14                1    92.86%
src/counter.cpp                       9   100.00%          53             0   100.00%          28                2    92.86%
include/wordfreq/counter.h            3   100.00%           3             0   100.00%           4                2    50.00%
include/wordfreq/tokenizer.h          1   100.00%          18             0   100.00%          26                0   100.00%
---------------------------------------------------------------------------------------------------------------------------
TOTAL                                16   100.00%          84             0   100.00%          72                5    93.06%
```

（删去了几列。）所有的行都执行过了，但还有 5 个分支没有走到。

### 7.2 找到没有测到的分支

`llvm-cov show` 显示每一行执行的次数，加上 `-show-branches=count` 还会显示每个条件为真、为假各多少次：

```
   49|      6|{
   50|      6|    if (threads == 0) threads = 1;
  ------------------
  |  Branch (50:9): [True: 0, False: 6]
  ------------------
   ...
   54|     25|    for (unsigned t = 0; t < threads && begin < text.size(); ++t) {
  ------------------
  |  Branch (54:26): [True: 19, False: 6]
  |  Branch (54:41): [True: 19, False: 0]
  ------------------
```

两个问题：

- 第 50 行：`threads == 0` 从来没有为真过。没有测试传入 0 个线程；
- 第 54 行：`begin < text.size()` 从来没有为假过。这个条件处理的是「线程比文本还多」的情况，比如 8 个线程统计 3 个字节的文本，切到后面已经没有内容了。测试用的文本有几千个字节，这种情况从来没有发生过。

第二个尤其值得补上：边界情况正是容易出错的地方。补两个测试：

```cpp
// 覆盖率报告发现的两个没有测到的分支
TEST(CountParallel, MoreThreadsThanText)
{
    Counter c = wordfreq::countParallel("a b", 8);      // 8 个线程，文本只有 3 个字节
    EXPECT_EQ(c.total(), 2);
    EXPECT_EQ(wordfreq::countParallel("", 4).total(), 0);
}

TEST(CountParallel, ZeroThreadsMeansOne)
{
    EXPECT_EQ(wordfreq::countParallel("x y z", 0).total(), 3);
}
```

测试通过了（代码本来就是对的），`counter.cpp` 的分支覆盖率变成了 100%：

```
   54|     35|    for (unsigned t = 0; t < threads && begin < text.size(); ++t) {
  ------------------
  |  Branch (54:26): [True: 28, False: 7]
  |  Branch (54:41): [True: 26, False: 2]
```

再用 ASan 和 TSan 各运行一遍，这两个新的情况中也没有内存错误和数据竞争。覆盖率和 Sanitizer 就是这样配合的：**覆盖率保证代码被执行到，Sanitizer 保证执行到的代码没有未定义行为。**

用 `-format=html -output-dir=build/coverage/html` 可以生成 HTML 报告，在浏览器中打开 `index.html`，没有执行到的行和分支会用红色标出，比文本的报告方便得多。

GCC 用的是另一套工具：编译选项 `--coverage`，报告用 `gcov`、`lcov` 或者 `gcovr` 生成，思路完全相同。

### 7.3 覆盖率不是目标

还剩下的 3 个分支：`tokenizer.cpp` 中 `isWordChar` 的某个比较，以及 `counter.h` 中 `Entry` 的 `operator==`（`a.word == b.word && a.count == b.count`，单词相同而次数不同的情况从来没有出现过）。它们并不重要。

覆盖率能告诉你**哪些代码一定没有被测过**，但覆盖率高不等于测得好：一个只调用函数、不检查结果的测试，也能让覆盖率达到 100%。把覆盖率当成考核指标，就会出现这样的测试。正确的用法是定期看一看报告，像上面那样**找出值得补的测试**。

## 8. 常见错误

1. **只给自己的代码打开 Sanitizer**。第三方库中分配、自己的代码中释放的内存，ASan 可能检查不到；TSan 还会误报。用全局的选项。
2. **UBSan 没有加 `-fno-sanitize-recover`**。错误信息打印出来了，但测试仍然通过，没有人会去看。
3. **在 Release 构建中打开 Sanitizer 调试**。优化会让报告中的行号和调用栈不准确，变量也可能被优化掉。用 Debug 构建，或者至少加上 `-g`。
4. **TSan 报告了竞争，觉得「这个变量只是读，不要紧」**。只要有一个线程在写，其他线程的读就是竞争。数据竞争是未定义行为，不存在「无害的竞争」。
5. **`LLVM_PROFILE_FILE` 没有加 `%p`**。每个测试的数据互相覆盖，覆盖率低得奇怪。
6. **追求 100% 的覆盖率**。为了覆盖错误处理中「内存分配失败」这样的分支写大量的模拟代码，不如把精力放在真正的边界情况上。

## 9. 小结

- Sanitizer 用一个 CMake 选项打开，作用于所有代码；ASan + UBSan 一个预设，TSan 一个预设；
- ASan 发现越界、使用已经释放的内存，报告中有使用、释放、分配三个位置；TSan 发现数据竞争，报告中有竞争的两方和线程是在哪里创建的；
- 普通构建中，这些错误可能崩溃、可能偶尔失败、可能悄悄出错；Sanitizer 让它们一次就稳定地暴露出来；
- Sanitizer 只能检查执行到的代码，用覆盖率找出测试没有走到的分支，补上测试。

## 10. 练习

1. 用 `asan` 预设运行 `wordfreq --help`，会报告内存泄漏吗？在 Linux 上呢？
2. 4.2 节的 `stripBom` 加上 `[[clang::lifetimebound]]`：`std::string_view stripBom(std::string_view s [[clang::lifetimebound]])`，重新编译那个有 bug 的版本，看看会怎样。
3. 第 5 节的数据竞争，如果改成给 `result.merge(local)` 加锁，会不会比现在的写法慢？在什么情况下会慢很多？

<details>
<summary>点击查看答案</summary>

1. 在 macOS 上，ASan 不支持泄漏检查，不会报告。在 Linux 上，LeakSanitizer 默认打开，程序退出时检查。`wordfreq --help` 没有泄漏，所以也不会报告。

2. Clang 会在编译时报告和 4.1 节相同的 `-Wdangling-gsl` 警告：`lifetimebound` 告诉编译器「返回值指向这个参数的内容」，于是编译器知道 `stripBom(loadInput(files))` 的结果在这一行结束后就悬空了。标准库中很多返回引用的函数，在 libc++ 中都标了这个属性。

3. 加锁以后，几个线程合并时要排队，合并本身变成了串行的。如果每个线程的局部结果很大（比如不同的单词非常多），合并的时间不能忽略，排队就会让总时间明显变长。现在的写法也是串行合并，但在主线程中进行，没有锁的开销；两者在单词不多时差别很小。更快的方法是并行合并（比如按单词的哈希值分片，每个线程只合并自己负责的那一片），代价是代码更复杂。

</details>

下一篇：[性能分析与优化](/posts/cppeng-profiling/)。程序是对的了，那它快吗？慢在哪里？
