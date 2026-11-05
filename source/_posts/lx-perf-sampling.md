---
title: perf 采样的原理与陷阱
date: 2026-11-10 10:20:00
updated: 2026-11-10 10:20:00
categories:
  - Linux基础
  - 性能分析
tags:
  - Linux
  - 性能
  - perf
order: 3
description: perf record 每秒打断程序 999 次，记下当时的调用栈。本文用一个调用关系清楚的小程序，看 perf 报告在哪些情况下会骗人：handleUpload 和 handleDownload 在调用栈中消失了，原因是尾调用优化；明确关掉帧指针后，main 也不见了；改用 DWARF 回溯，调用栈完整了，连被内联的函数都看得到，数据却从 0.36 MB 涨到 25.9 MB；去掉符号表后只剩下一串地址，而 perf 的 build-id 缓存会把这个问题藏起来。最后在没有硬件计数器的虚拟机里采样内核，看采样点为什么会堆在一个函数上。
top_group_list: true
disableNunjucks: true
viz: [lx-perf]
---

> **本文目标**：理解 perf 采样的原理，会用采样数检查结果是否可信；理解调用栈是怎样回溯出来的（帧指针、DWARF），知道尾调用、省略帧指针、内联、去掉符号表各会让报告变成什么样，以及怎么解决；会对运行中的进程采样；知道在虚拟机中采样的局限。
>
> **前置知识**：《C++工程实践》中的《性能分析与优化：基准测试、perf 与火焰图》（`perf record`、`perf report`、火焰图的基本读法，本文不再重复）；《C 语言基础》中函数调用与栈帧的知识；本章第 1 篇。
>
> **实验环境**：同第 1 篇。这台虚拟机没有把 CPU 的硬件性能计数器（PMU）提供给虚拟机，`perf stat -e cycles` 显示 `<not supported>`，perf 采样用的是软件事件 `task-clock`（定时器）。

## 1. 采样的原理

`perf record -F 999` 让内核每秒打断程序 999 次（每个 CPU 上按程序运行的时间计算）。每次打断时，记下：

- 程序计数器（PC）：正在执行哪条指令，对应哪个函数；
- 加上 `-g` 时，还有**调用栈**：这个函数是被谁调用的，一直到 `main`。

某个函数在采样中出现的次数占总次数的比例，就是它占用 CPU 时间的比例。采样的开销很小，也不需要修改程序。

<div class="dsviz" data-viz="lx-sample"></div>

演示中有一个细节：采样的间隔如果正好和程序的循环节奏同步，每次都会落在同一个地方，结果完全错误。所以采样频率常用 99、999 这样的数，而不是 100、1000，以免和程序中以 10 毫秒、1 毫秒为周期的定时器同步。

拿到报告，先检查采样数是否合理：**采样数 ≈ 频率 × 程序用掉的 CPU 时间**。下面的程序运行了 3.18 秒，999 Hz，预期约 3177 个采样，实际是 3175 个。如果采样数远少于预期，可能是程序大部分时间在等（off-CPU，第 5 篇），或者采样丢失了（报告头部的 `Total Lost Samples`）。

## 2. 实验程序

`sample.c` 的调用关系很清楚：

```c
static inline uint64_t mix(uint64_t h, uint8_t b)   // 很短，会被内联
{
    h ^= b;
    h *= 0x100000001b3ULL;
    return h ^ (h >> 29);
}

__attribute__((noinline)) uint64_t checksum(const uint8_t* p, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < n; i++) h = mix(h, p[i]);
    return h;
}

__attribute__((noinline)) uint64_t handleUpload(void) { return checksum(data, 3 * BLOCK); }
__attribute__((noinline)) uint64_t handleDownload(void) { return checksum(data, BLOCK); }

__attribute__((noinline)) void sortRecords(void)
{
    for (int i = 0; i < NREC; i++) recs[i].key = rand();
    qsort(recs, NREC, sizeof recs[0], cmpRecord);
}

__attribute__((noinline)) uint64_t serveRound(void)
{
    uint64_t sum = handleUpload();
    sum += handleDownload();
    sortRecords();
    return sum;
}
```

热点函数 `checksum` 有两个调用者：`handleUpload` 处理 3 MB，`handleDownload` 处理 1 MB，时间应该是 3 : 1。这正是需要调用栈的场景：只知道 `checksum` 占 80%，不知道该优化哪一条路径。

## 3. 陷阱一：尾调用让函数消失

用最普通的 `-O2` 编译，`perf record -F 999 -g ./build/sample 500`，再 `perf report --no-children -G`：

```
    82.90%  sample   sample         [.] checksum
            |
            ---_start
               __libc_start_main@@GLIBC_2.34
               __libc_start_call_main
               main
               serveRound
               checksum
     9.86%  sample   libc.so.6      [.] msort_with_tmp.part.0
            |
            ---_start
               ...
               main
               serveRound
               __GI___qsort_r
```

调用栈看起来完整，但 `handleUpload` 和 `handleDownload` 不见了，`serveRound` 直接调用了 `checksum`；排序那一支中 `sortRecords` 也不见了。看反汇编：

```
0000000000000d20 <handleUpload>:
 d20:	adrp	x0, bc000 <recs+0x9bfe0>
 d24:	mov	x1, #0x300000              	// #3145728
 d28:	add	x0, x0, #0x420
 d2c:	b	cc0 <checksum>
```

最后一条是 `b`（跳转），不是 `bl`（调用）。`return checksum(...)` 是函数的最后一步，`handleUpload` 不需要等 `checksum` 返回以后再做什么，编译器就直接跳过去，让 `checksum` 用完以后直接返回到 `serveRound`。这叫**尾调用优化**（`-foptimize-sibling-calls`，`-O2` 默认打开），省了一次调用和返回，但 `handleUpload` 的栈帧从来没有存在过，任何回溯方法都找不到它。

加上 `-fno-optimize-sibling-calls` 重新编译（`build/sample-nosib`），两条路径分开了：

```
    83.06%  sample-nosib  sample-nosib   [.] checksum
            |
            ---_start
               ...
               main
               serveRound
               |
               |--62.37%--handleUpload
               |          checksum
               |
                --20.69%--handleDownload
                          checksum
```

62.37% : 20.69%，正好是 3 : 1。这个选项一般只在分析时打开。

## 4. 陷阱二：没有帧指针，调用栈就断了

perf 默认的回溯方法是**帧指针**：每个函数开头把调用者的帧指针和返回地址存在栈上，再让帧指针寄存器（aarch64 是 `x29`，x86-64 是 `rbp`）指向这里，所有的栈帧就连成了一条链。perf 采样时顺着链往上走，每一步读两次内存，很快。

<div class="dsviz" data-viz="lx-unwind"></div>

为了多一个可用的寄存器，编译器开了优化以后可以不维护帧指针（`-fomit-frame-pointer`），这时链就断了。不同平台的默认行为不同：

- **x86-64**：GCC 的 `-O2` 默认省略帧指针，这是 perf 调用栈不完整最常见的原因。《C++工程实践》一章中用 perf 时要加 `-fno-omit-frame-pointer`，就是这个原因；
- **aarch64**：`gcc -Q -O2 --help=optimizers` 显示 `-fomit-frame-pointer [enabled]`，但 aarch64 的 GCC 仍然会为非叶子函数保存帧记录，所以上一节的调用栈是完整的。

在 aarch64 上明确加上 `-fomit-frame-pointer`（`build/sample-omit`），`main` 的开头从

```
 940:	stp	x29, x30, [sp, #-64]!
 944:	mov	x29, sp
```

变成了

```
 940:	stp	x30, x19, [sp, #-48]!
```

不再保存 `x29`。采样的结果：

```
    83.07%  sample-omit  sample-omit    [.] checksum
            |
            ---_start
               __libc_start_main@@GLIBC_2.34
               serveRound
               checksum
```

`main` 和 `__libc_start_call_main` 都不见了，`serveRound` 好像是被 `__libc_start_main` 直接调用的。这个调用栈是**错的**，而 perf 不会给出任何提示。层数越多的程序，断得越厉害。

还要注意 `checksum` 的调用者 `serveRound` 是对的。`checksum` 是**叶子函数**（不再调用别的函数），不保存帧记录，它的返回地址只在 `x30`（LR）寄存器中。从结果看，perf 在 arm64 上还利用了 LR，补上了叶子函数的调用者；只沿帧指针链回溯的工具会漏掉这一层（第 6 篇的 bpftrace 就是这样）。

解决办法有三种：

1. **重新编译，加 `-fno-omit-frame-pointer`**。开销通常只有百分之一二。Ubuntu 24.04、Fedora 38 起，发行版自己的软件包都默认这样编译，所以上面的报告中 libc 内部（`__libc_start_call_main`、`__GI___qsort_r`）的调用栈都是完整的；
2. **`--call-graph dwarf`**：不依赖帧指针，下一节；
3. **`--call-graph lbr`**：用 Intel CPU 的硬件记录最近的跳转，只在 Intel 的物理机上可用。

## 5. DWARF 回溯：完整，但很大

`-g` 编译时，调试信息中有一份「在每条指令处，怎样找到返回地址和调用者的寄存器」的表（DWARF 的 CFI，`.eh_frame` 中也有一份，C++ 的异常处理就靠它）。`--call-graph dwarf` 让 perf 在每次采样时**复制一段栈**（默认 8 KB）和寄存器，事后用这张表慢慢回溯：

```bash
perf record -F 999 --call-graph dwarf ./build/sample-omit 500
```

```
    83.08%  sample-omit  sample-omit        [.] checksum
            |
            ---_start
               __libc_start_main_impl (inlined)
               __libc_start_call_main
               main
               serveRound
               checksum
               mix (inlined)
```

同一个没有帧指针的程序，调用栈完整了，还多了两层**被内联的函数**：`mix` 被内联进了 `checksum`，`__libc_start_main_impl` 被内联进了 `__libc_start_main`。调试信息记录了内联关系，帧指针回溯看不到这些。

代价是数据量：

| 方式 | 采样数 | perf.data |
|---|---|---|
| `-g`（帧指针） | 3178 | 0.36 MB |
| `--call-graph dwarf` | 3179 | 25.9 MB |

大了 72 倍，每个采样要存 8 KB 的栈。采样时间长、频率高时，DWARF 会产生几 GB 的数据，写盘本身也会干扰程序，`perf report` 也慢得多。栈很深时（比如递归很深，或者大的局部数组），8 KB 不够用，最外面的几层仍然会丢，可以用 `--call-graph dwarf,32768` 加大。

## 6. 陷阱三：内联和符号

**内联**。帧指针回溯的报告中没有 `mix`，它的时间都算在 `checksum` 上。这通常没问题，因为 `mix` 确实是 `checksum` 的一部分；需要看清是哪一行代码热时，用 `perf annotate checksum` 看每条指令的采样比例，或者用 DWARF 回溯加 `--inline`。

**符号表被去掉**。发布的程序常常用 `strip` 去掉符号表减小体积。复制一份 `build/sample` 去掉符号表再采样：

```
$ perf report --stdio
    72.23%  sample-s  sample-s          [.] 0x0000000000000ce8
    10.70%  sample-s  libc.so.6         [.] msort_with_tmp.part.0
     5.90%  sample-s  sample-s          [.] 0x0000000000000cec
     4.96%  sample-s  sample-s          [.] 0x0000000000000cf4
     4.01%  sample-s  libc.so.6         [.] __memcpy_generic
     1.18%  sample-s  sample-s          [.] 0x0000000000000c24
```

只剩下地址，而且同一个函数被拆成了很多行（每个地址一行）。`checksum` 的地址范围是 `0xcc0` 开始，所以前三行都是它，要靠人去对 `nm` 或者 `objdump` 的输出。

有意思的是，第一次做这个实验时，去掉符号表的程序在报告中**仍然显示函数名**。原因是 perf 的 **build-id 缓存**：每次 `perf record` 时，perf 把用到的可执行文件按 build-id（链接器写进文件的一个唯一编号，`readelf -n` 可以看到）复制一份到 `~/.debug`。复制出来的 `sample-s` 和 `sample` 的 build-id 完全相同，`perf report` 就从缓存中找到了带符号的那一份。删掉 `~/.debug` 再试，才看到上面的地址。

这个机制正好可以利用：发布去掉符号表的程序，同时保留带符号的版本（或者用 `objcopy --only-keep-debug` 分离出调试信息），分析时用 `perf buildid-cache -a` 把它加进缓存，或者放进 `/usr/lib/debug/.build-id/` 下对应的位置。

## 7. 对运行中的进程采样

不必从 perf 启动程序，`-p` 附着到运行中的进程，`--` 后面的命令决定采样多久：

```bash
./build/load tiny 100000000 &
perf record -F 999 -g -p $! -- sleep 2
```

2 秒、999 Hz，得到 2005 个采样，和预期一致。按模块统计（`--sort dso`），72% 在内核中（`[kernel.kallsyms]`），和第 1 篇中 `tiny` 的 sys 占 74% 对得上。按函数看：

```
    35.61%  load     [kernel.kallsyms]  [k] el0_svc
    27.23%  load     libc.so.6          [.] __GI___libc_write
     9.88%  load     [kernel.kallsyms]  [k] __fget_light
     7.78%  load     [kernel.kallsyms]  [k] invoke_syscall
     5.39%  load     [kernel.kallsyms]  [k] get_random_u16
     1.80%  load     [kernel.kallsyms]  [k] vfs_write
```

两点值得一说：

- `get_random_u16` 占了 5%，调用栈显示它在系统调用的公共入口中被调用。这应该是内核的 `randomize_kstack_offset`：每次系统调用都随机偏移内核栈的位置，增加某些攻击的难度。系统调用越便宜，这种固定开销的比例越大；
- `el0_svc`（aarch64 上从用户态进入系统调用的入口）占了 36%，比真正写数据的 `vfs_write` 多得多。这很可能是**软件事件采样的偏差**：没有 PMU，perf 用定时器中断采样；进入和退出系统调用的路上有一段代码是关着中断的，定时器到期时中断被推迟，等中断重新打开才采样，样本就堆到了重新开中断的位置附近。在有 PMU 的物理机上，用 `cycles` 事件采样，这种偏差会小得多。

**在虚拟机或容器中采样，要知道自己用的是什么事件**：报告头部的 `Samples: 3K of event 'task-clock:ppp'` 说明是软件事件。云服务器上很多实例类型也不提供 PMU。

容器中使用 perf，还要注意：

- 需要 `--privileged`，或者至少 `CAP_PERFMON` 并调低 `/proc/sys/kernel/perf_event_paranoid`；
- 看内核函数名需要 root 或者 `/proc/sys/kernel/kptr_restrict` 为 0；
- 容器中的 perf 版本要和宿主机（虚拟机）的内核大致匹配。本文的容器中安装的是 6.8.0-147 的 perf，内核是 6.8.0-117，可以正常工作。

## 8. 常见错误

1. **不检查采样数**。采样数远少于「频率 × CPU 时间」时，报告不能代表程序的全部时间。
2. **在没有帧指针的程序上相信 `-g` 的调用栈**。断掉的调用栈不会报错，只会给出错误的调用关系。
3. **找不到某个函数，就以为它不花时间**。它可能被内联了，或者被尾调用跳过了。
4. **在生产环境长时间用 `--call-graph dwarf`**。数据量是帧指针的几十倍。
5. **只发布去掉符号表的程序，不保留调试信息**。出了性能问题时，报告中只有地址。
6. **把软件事件的采样结果当成精确的**。在虚拟机中，样本可能堆积在中断重新打开的位置。

## 9. 小结

- perf 按固定频率打断程序，记录 PC 和调用栈；采样数 ≈ 频率 × CPU 时间，用它检查报告是否可信；
- 尾调用（`return f(...)`）让中间的函数从调用栈中消失，`-fno-optimize-sibling-calls` 可以保留；
- 帧指针回溯快，但依赖 `-fno-omit-frame-pointer`；x86-64 的 `-O2` 默认省略，aarch64 的 GCC 默认保留帧记录，明确省略后 `main` 从调用栈中消失了；
- DWARF 回溯不依赖帧指针，能看到内联函数，数据量大 72 倍；
- 去掉符号表后只剩地址；perf 的 build-id 缓存可以找回符号，也可能让人误以为符号表还在；
- 虚拟机中没有 PMU 时只能用软件事件采样，内核中的样本可能有偏差。

## 10. 练习

1. 用 `perf record -F 99 -g` 对一个程序采样 10 秒，只得到 120 个采样。这个程序是单线程的。可能是什么原因？
2. 下面的函数开了 `-O2`，`perf report` 中会看到 `validate` 吗？为什么？
   ```c
   int handle(struct req* r) {
       if (!r) return -1;
       return validate(r);
   }
   ```
3. 线上的服务是去掉符号表发布的。你手上有同一次构建产生的带符号的版本。怎样让 perf 报告显示函数名，而不用重新部署？

<details>
<summary>点击查看答案</summary>

1. 预期是 99 × 10 = 990 个，只有 120 个，说明程序只用了约 1.2 秒的 CPU，其余时间不在 CPU 上：在等 IO、等锁或睡眠。这时 on-CPU 的报告只覆盖了一小部分时间，要用第 1 篇的方法确认在等什么，再做 off-CPU 分析（第 5 篇）。也要看报告头部有没有丢失采样（`Total Lost Samples`）。

2. 会看到 `validate`（它是被调用的函数，正在执行时 PC 就在它里面），但看不到 `handle`：`return validate(r)` 是尾调用，编译器用跳转代替调用，`validate` 返回时直接回到 `handle` 的调用者。调用栈中 `validate` 的上一层会显示为调用 `handle` 的函数。要看到 `handle`，用 `-fno-optimize-sibling-calls` 编译。

3. 确认两个文件的 build-id 相同（`readelf -n`），然后在分析的机器上 `perf buildid-cache -a 带符号的文件`，把它加进缓存（或者放到 `/usr/lib/debug/.build-id/xx/yyyy.debug`）。`perf report` 按 build-id 查找符号，会自动使用带符号的版本。线上的程序不需要任何改动。

</details>
