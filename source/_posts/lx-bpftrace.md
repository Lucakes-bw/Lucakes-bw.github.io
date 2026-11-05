---
title: bpftrace：不改代码看清程序在做什么
date: 2026-11-10 10:50:00
updated: 2026-11-10 10:50:00
categories:
  - Linux基础
  - 性能分析
tags:
  - Linux
  - 性能
  - bpftrace
order: 6
description: bpftrace 把一小段脚本编译成 eBPF 程序放进内核，挂在系统调用、内核函数、甚至用户程序的任意函数上，在内核中计数、做直方图，不用修改和重启程序。本文先统计 python3 启动时打开了 124 个文件、失败 9 次，画出它 malloc 的大小分布；再跟踪一个运行中的服务：每种请求的次数、大小和耗时分布，千分之一的慢请求是哪几个、慢在哪一行，失败的请求有什么共同点。最后实测开销：uprobe 让每次函数调用多出约 136 纳秒，比 ltrace 小几百倍，但对每秒调用千万次的函数仍然不能忽视。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 eBPF 和 bpftrace 的基本原理；会写 bpftrace 的探针、过滤条件和动作，会用 `count`、`sum`、`hist` 聚合；会用 tracepoint 和 kprobe 观察内核，用 uprobe、uretprobe 观察用户程序的函数参数、返回值和耗时；会用 `profile` 采样；知道各种探针的开销和限制。
>
> **前置知识**：本章第 2、5 篇。第 5 篇已经用过一个 bpftrace 脚本，本文系统地讲它的写法。
>
> **实验环境**：同第 1 篇；bpftrace 0.20.2。要在容器中使用，见第 5 篇第 5 节（挂载 tracefs、`--pid=host`）。

## 1. eBPF 和 bpftrace

前面几篇的工具各有局限：strace 只能看系统调用，而且很慢；perf 采样能看到时间花在哪里，但不知道函数的参数、调用了多少次；要知道「参数大于 1 KB 的请求有多少个」「哪些请求超过了 10 毫秒」，通常只能改代码、加日志、重新部署。

**eBPF** 是 Linux 内核中的一个小型虚拟机。用户可以把一段程序（字节码）加载到内核中，挂在某个事件上，事件发生时在内核中执行。加载前内核会**验证**这段程序：不能有无限循环，不能访问任意内存，不会让内核崩溃。程序可以把数据存在内核中的 **map**（哈希表、数组等）里，用户态再读出来。因为统计在内核中完成，不需要每个事件都切换到一个跟踪进程，开销比 strace、ltrace 小得多。

**bpftrace** 是 eBPF 的一种高级语言，有点像 awk：一行脚本就能完成一个统计，它负责编译成 eBPF 字节码、加载、挂载，结束时打印 map 中的结果。

<figure class="diagram">
<svg viewBox="0 0 700 330" role="img" aria-label="bpftrace 的探针挂在哪里：用户程序和共享库的函数（uprobe）；系统调用的入口和返回（tracepoint）；内核函数（kprobe）；调度器的跟踪点；定时器（profile）。事件发生时在内核中执行 eBPF 程序，结果存进 map，bpftrace 再读出来打印">
  <defs><marker id="prb-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="prb-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="20.0" y="24.0" text-anchor="start" style="fill:var(--muted);font-size:11px">用户态</text><rect x="20" y="34" width="200" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="120.0" y="50.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">程序：handleRequest</text><text x="120.0" y="65.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">uprobe / uretprobe</text><rect x="240" y="34" width="200" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="340.0" y="50.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">libc：malloc、pthread</text><text x="340.0" y="65.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">uprobe</text><line x1="20" y1="96" x2="460" y2="96" style="stroke:var(--border-2);stroke-width:1.5;stroke-dasharray:6 4"/><text x="240.0" y="112.0" text-anchor="middle" style="fill:var(--link);font-size:11px">系统调用：tracepoint:syscalls:sys_enter_* / sys_exit_*</text><text x="20.0" y="140.0" text-anchor="start" style="fill:var(--muted);font-size:11px">内核</text><rect x="20" y="150" width="200" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="120.0" y="166.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">VFS、ext4、TCP……</text><text x="120.0" y="181.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">kprobe / kretprobe</text><rect x="240" y="150" width="200" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="340.0" y="166.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">调度器</text><text x="340.0" y="181.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">tracepoint:sched:*</text><rect x="20" y="210" width="200" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="120.0" y="226.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">定时器中断</text><text x="120.0" y="241.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">profile:hz:99</text><rect x="240" y="210" width="200" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="340.0" y="226.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">块设备、页缓存……</text><text x="340.0" y="241.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">tracepoint:block:* 等</text><rect x="500" y="120" width="180" height="50" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="590.0" y="141.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">eBPF 程序</text><text x="590.0" y="156.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">经过验证，在内核中运行</text><rect x="500" y="200" width="180" height="40" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="590.0" y="224.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">map：计数、直方图</text><rect x="500" y="34" width="180" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="590.0" y="50.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">bpftrace（用户态）</text><text x="590.0" y="65.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">编译、加载、打印</text><line x1="442" y1="54" x2="498" y2="140" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#prb-a)"/><line x1="442" y1="170" x2="498" y2="145" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#prb-a)"/><line x1="442" y1="230" x2="498" y2="145" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#prb-a)"/><line x1="590" y1="170" x2="590" y2="198" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#prb-a)"/><path d="M680,220 C700,160 700,90 682,60" style="fill:none;stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#prb-a)"/><text x="590.0" y="272.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">统计在内核中完成，</text><text x="590.0" y="290.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">不必每个事件都切换进程</text><text x="240.0" y="290.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">事件发生时触发</text>
  </g>
</svg>
</figure>

常用的探针类型：

| 探针 | 挂在哪里 | 例子 |
|---|---|---|
| `tracepoint` | 内核中预先定义好的跟踪点，接口稳定 | `tracepoint:syscalls:sys_enter_openat` |
| `kprobe` / `kretprobe` | 任意内核函数的入口 / 返回 | `kprobe:vfs_read` |
| `uprobe` / `uretprobe` | 任意用户程序或共享库中函数的入口 / 返回 | `uprobe:/lib/aarch64-linux-gnu/libc.so.6:malloc` |
| `profile` | 按固定频率在每个 CPU 上触发，用来采样 | `profile:hz:99` |
| `interval` | 按固定时间间隔在一个 CPU 上触发，用来定时打印、退出 | `interval:s:3` |
| `BEGIN` / `END` | 脚本开始 / 结束时 | |

`bpftrace -l 'tracepoint:syscalls:*open*'` 列出匹配的探针；`bpftrace -lv tracepoint:syscalls:sys_enter_openat` 显示它的参数。

## 2. 语法

一个 bpftrace 程序由若干个**探针 /过滤条件/ { 动作 }** 组成：

```
tracepoint:syscalls:sys_enter_openat /comm == "python3"/ { @files = count(); }
```

- `comm`、`pid`、`tid`、`nsecs` 是内置变量：当前进程的名字、进程号、线程号、当前时刻（纳秒）；
- `args` 是 tracepoint 的参数（`args.filename`、`args.ret`）；kprobe、uprobe 的参数是 `arg0`、`arg1`……（按调用约定取寄存器），返回探针中的返回值是 `retval`；
- `@` 开头的是 map，`@files` 是没有键的，`@calls[arg0]` 以第一个参数为键；
- 聚合函数：`count()` 计数，`sum(x)` 求和，`avg(x)`、`min(x)`、`max(x)`，`hist(x)` 按 2 的幂分组的直方图，`lhist(x, 最小, 最大, 步长)` 线性直方图；
- `ustack`、`kstack` 是当前的用户态、内核态调用栈；`printf` 打印，`str(p)` 读出指针指向的字符串；
- 程序结束（Ctrl-C 或 `exit()`）时，所有的 map 都会被打印出来。

## 3. 观察系统中的程序

**python3 启动时打开了多少个文件？** 一个探针在进入 `openat` 时计数，另一个在返回时统计失败的次数：

```bash
bpftrace -e '
tracepoint:syscalls:sys_enter_openat /comm == "python3"/ { @files = count(); }
tracepoint:syscalls:sys_exit_openat /comm == "python3" && args.ret < 0/ { @failed = count(); }'
```

另一个终端运行 `python3 -c "import json, decimal, asyncio"`，结果：

```
@failed: 9
@files: 124
```

一条 `import` 打开了 124 个文件，9 个不存在（Python 按搜索路径一个个试）。把 `count()` 换成 `printf("%s\n", str(args.filename))` 就能看到具体的文件名。

**python3 malloc 的大小分布**。uprobe 可以挂在共享库的函数上，`arg0` 就是 `malloc` 的参数：

```bash
bpftrace -e 'uprobe:/lib/aarch64-linux-gnu/libc.so.6:malloc /comm == "python3"/ { @bytes = hist(arg0); }'
```

```
@bytes:
[1]                   92 |@                                                   |
[2, 4)                 7 |                                                    |
[4, 8)                56 |                                                    |
[8, 16)              172 |@@                                                  |
[16, 32)            1947 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@                    |
[32, 64)            3081 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[64, 128)            162 |@@                                                  |
[128, 256)            32 |                                                    |
[256, 512)            15 |                                                    |
[512, 1K)           2500 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@          |
[1K, 2K)             509 |@@@@@@@@                                            |
...
[128K, 256K)           3 |                                                    |
```

大部分在 16 到 64 字节，512 字节到 1 KB 之间也有一大堆。想知道是谁申请的，把 `hist(arg0)` 换成 `@[ustack] = count()`，按调用栈统计。这种分布在选择分配器、调 malloc 的参数（《malloc 是怎样分配内存的》）时很有用。

## 4. 跟踪一个运行中的服务

`trace serve` 模拟一个服务：不停地处理三种请求（查询 70%、写入 20%、统计 10%），大小从 64 到 2048 字节；千分之一的请求会等 20 毫秒；有些请求会失败。这些规律在代码中，但假设我们不知道：

```c
// type：0 查询，1 写入，2 统计；size：请求的大小（字节）
__attribute__((noinline)) int handleRequest(int type, int size)
{
    long iters = 2000L + size * (type == 2 ? 40L : 4L);
    if (rand() % 1000 == 0) usleep(20000);  // 千分之一的请求要等 20 ms
    sink = burn((uint64_t)size, iters);
    return type == 1 && size >= 1024 ? -1 : 0;  // 大的写入请求会失败
}
```

（`noinline` 是为了让这个小程序中的函数不被内联，实际的程序中，处理请求的函数一般都足够大，不会被内联。被内联的函数没有自己的入口，uprobe 挂不上去。）

服务已经在运行，不需要重启。下面的脚本中 `$B` 是可执行文件的路径（`/root/lx-perf/build/trace`），每个都运行 3 秒（`interval:s:3 { exit(); }`）。

**每种请求有多少？**

```
uprobe:$B:handleRequest { @calls[arg0] = count(); }
```

```
@calls[2]: 6735
@calls[1]: 13806
@calls[0]: 48186
```

70%、20%、10%。

**请求的大小分布？**

```
uprobe:$B:handleRequest { @size = hist(arg1); }
```

```
@size:
[64, 128)          11802 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ |
[128, 256)         11874 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ |
[256, 512)         11748 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@  |
[512, 1K)          11830 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ |
[1K, 2K)           11998 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[2K, 4K)           11836 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ |
```

6 种大小各占六分之一。

**每种请求的耗时分布？** 入口记下时刻和类型，返回时算出耗时，按类型分别做直方图：

```
uprobe:$B:handleRequest { @start[tid] = nsecs; @type[tid] = arg0; }

uretprobe:$B:handleRequest /@start[tid]/
{
    @us[@type[tid]] = hist((nsecs - @start[tid]) / 1000);
    delete(@start[tid]);
    delete(@type[tid]);
}
```

返回探针中拿不到参数（寄存器早就被覆盖了），所以要在入口时按线程号存起来。结果（省略了中间为 0 的行）：

```
@us[0]:
[2, 4)              3278 |@@@@@@                                              |
[4, 8)             25657 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[8, 16)            16678 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@                   |
[16, 32)            6512 |@@@@@@@@@@@@@                                       |
[32, 64)             700 |@                                                   |
[64, 128)             14 |                                                    |
[16K, 32K)            44 |                                                    |

@us[2]:
[4, 8)               404 |@@@@@@@@@@                                          |
[8, 16)             1959 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
...
[128, 256)           882 |@@@@@@@@@@@@@@@@@@@@@@@                             |
[256, 512)            97 |@@                                                  |
[16K, 32K)             9 |                                                    |
```

查询大多在 4 到 16 微秒，统计请求慢得多，最长到几百微秒。每种请求都有一小撮落在 16 到 32 **毫秒**，和主体相差上千倍。平均值会把它们淹没，直方图一眼就能看出来。

**慢请求是哪些？** 只打印超过 10 毫秒的：

```
uprobe:$B:handleRequest { @start[tid] = nsecs; @a[tid] = arg0; @b[tid] = arg1; }

uretprobe:$B:handleRequest /@start[tid] && nsecs - @start[tid] > 10000000/
{
    printf("慢请求：type=%d size=%d 用了 %d ms\n", @a[tid], @b[tid], (nsecs - @start[tid]) / 1000000);
}
```

```
慢请求：type=0 size=256 用了 30 ms
慢请求：type=1 size=64 用了 24 ms
慢请求：type=2 size=2048 用了 30 ms
慢请求：type=0 size=512 用了 22 ms
慢请求：type=2 size=64 用了 30 ms
...
```

类型、大小都没有规律，不是某种请求的问题。慢在哪里？它们在 CPU 上算不了 20 毫秒，多半在等。看哪段代码在睡眠：

```
tracepoint:syscalls:sys_enter_clock_nanosleep /comm == "trace"/ { @[ustack] = count(); }
```

```
@[
    clock_nanosleep+128
    nanosleep+28
    usleep+88
    handleRequest+172
    main+584
    __libc_start_call_main+116
    __libc_start_main+152
    _start+48
]: 81
```

`handleRequest` 中调用了 `usleep`。`handleRequest+172` 是调用点在函数中的偏移，`addr2line -e build/trace` 加上地址可以换算成源代码的行号（需要 `-g` 编译）。实际的程序中，这个位置可能是一次同步的 DNS 查询、一个忘了加超时的网络请求，或者一次意外的磁盘读。`usleep(20000)` 实际睡了 22 到 30 毫秒，多出来的是定时器的精度和调度的延迟。

**失败的请求有什么共同点？**

```
uretprobe:$B:handleRequest { @ret[(int32)retval] = count(); }
```

```
@ret[-1]: 4745
@ret[0]: 65240
```

约 7% 失败。按参数分组：

```
uprobe:$B:handleRequest { @a[tid] = arg0; @b[tid] = arg1; }
uretprobe:$B:handleRequest /(int32)retval < 0/ { @fail[@a[tid], @b[tid]] = count(); }
```

```
@fail[1, 1024]: 2419
@fail[1, 2048]: 2459
```

只有类型 1（写入）、大小 1024 和 2048 的请求失败。几行脚本就从运行中的服务里找到了规律，没有加一行日志。`(int32)` 是因为 `retval` 是 64 位的寄存器值，`int` 的 -1 要转换一下才能正确比较。

## 5. 用 profile 采样

bpftrace 也能像 perf 那样采样，`profile:hz:99` 在每个 CPU 上每秒触发 99 次，`ustack(3)` 只取最内层的 3 帧：

```
profile:hz:99 /comm == "trace"/ { @[ustack(3)] = count(); }
```

```
@[
    burn+48
    main+584
    __libc_start_call_main+116
]: 99
@[
    burn+56
    main+584
    __libc_start_call_main+116
]: 13
```

注意 `handleRequest` 不见了，`burn` 好像是 `main` 直接调用的。`burn` 是**叶子函数**，在 aarch64 上不保存帧记录，它的返回地址（回到 `handleRequest`）只在 LR 寄存器中；bpftrace 只沿着帧指针链回溯，于是漏掉了一层。perf 在 arm64 上会利用 LR 补上这一层（第 3 篇中 `checksum` 的调用者是对的）。另外，`ustack` 不会合并同一个函数中的不同地址（`burn+48` 和 `burn+56` 是两行）。做 CPU 的采样分析，perf 更方便；bpftrace 的优势是能和其他探针组合，比如只在某种请求处理期间采样。

## 6. 开销

eBPF 程序在内核中运行，开销小，但不是零。用一个空函数测 uprobe 的开销：

```c
__attribute__((noinline)) void empty(int i) { __asm__ volatile("" ::"r"(i)); }

for (long i = 0; i < n; i++) empty((int)i);
```

```
直接运行：    调用 10000000 次：0.016 秒，每次 1.6 ns
挂上 uprobe： 调用 10000000 次：1.374 秒，每次 137.4 ns
直接运行：    调用 10000000 次：0.008 秒，每次 0.8 ns
```

bpftrace 准确地数到了 `@n: 10000000`。每次调用多出约 **136 纳秒**：uprobe 是把函数入口的指令换成断点，每次调用都要陷入内核、执行 eBPF 程序、再返回用户态。和其他方式比较：

| 方式 | 每次多出的时间 | 来源 |
|---|---|---|
| tracepoint 计数（系统调用） | 约 60 ns | 第 2 篇 |
| uprobe 计数（用户函数） | 约 136 ns | 本节 |
| uprobe + uretprobe | 更多（返回时还要陷入一次） | |
| strace（系统调用） | 约 25 µs | 第 2 篇 |
| ltrace（库函数） | 约 50 µs | 第 2 篇 |

比 ltrace 小几百倍，但对于每次只要几纳秒、每秒调用上千万次的函数（比如 `malloc`），136 纳秒意味着慢几十倍。第 3 节对 python3 的 `malloc` 挂 uprobe 没有问题，因为它只调用了 9000 次左右；对一个每秒 malloc 上千万次的服务就要小心了。跟踪 `trace serve` 时，`handleRequest` 每次要几微秒到几十微秒，多出的 0.1 到 0.3 微秒（入口和返回各一次）影响不大：实测不跟踪时 5 秒处理了 138634 个请求，挂着入口和返回两个探针、做耗时直方图时处理了 135512 个，只少了 2% 左右。

经验：

- **优先用 tracepoint**，它最便宜，而且接口在内核版本之间是稳定的；kprobe 挂的是内核内部的函数，换一个内核版本，函数名和参数都可能变（比如本章的 `finish_task_switch.isra.0`）；
- **在内核中聚合**：用 `count`、`hist`，不要每个事件都 `printf`，否则输出本身就是瓶颈；
- **先用过滤条件缩小范围**：`/comm == "..."/`、`/pid == 1234/`，让无关的事件尽早返回；
- **uprobe 挂在调用频率适中的函数上**：请求的入口、数据库的查询函数，而不是每秒千万次的小函数。

## 7. 常见错误

1. **对被内联的函数挂 uprobe**。函数没有独立的入口，挂不上，或者只能看到少数没被内联的调用。用 `nm` 确认符号存在，看反汇编确认它真的被调用了。
2. **在返回探针中读参数**。`arg0` 等寄存器早已被覆盖，要在入口时按 `tid` 存起来。
3. **忘了清理按 `tid` 存的 map**。线程很多、请求有时不返回（异常、longjmp）时，map 会越来越大，要在返回时 `delete`。
4. **不做类型转换就比较返回值**。`retval` 是 64 位的，`int` 的 -1 要先 `(int32)retval`。
5. **在每秒上千万次的函数上挂 uprobe**。每次约 136 纳秒，可能让程序慢几十倍。
6. **用 bpftrace 的调用栈做精细的 CPU 分析**。它只沿帧指针回溯，叶子函数的调用者可能丢失，不合并同一函数的不同地址。

## 8. 小结

- eBPF 让一小段经过验证的程序在内核中运行，在事件发生时计数、做直方图，结果存在 map 中；bpftrace 是它的高级语言；
- 探针：tracepoint（稳定、便宜）、kprobe（任意内核函数）、uprobe/uretprobe（任意用户函数）、profile（采样）、interval（定时）；
- 不修改、不重启，统计出了一个服务每种请求的次数、大小和耗时分布，找出了千分之一的慢请求在哪里睡眠，失败的请求都是大的写入；
- 开销：tracepoint 每次约 60 纳秒，uprobe 每次约 136 纳秒，比 strace、ltrace 小两三个数量级，但对调用极频繁的函数仍然明显；
- 在容器中使用，需要特权、tracefs 和共享的进程号命名空间（第 5 篇）。

## 9. 练习

1. 写一个 bpftrace 脚本，统计每个进程每秒执行了多少次系统调用，每秒打印一次并清空。（提示：`tracepoint:raw_syscalls:sys_enter`、`interval:s:1`、`print`、`clear`）
2. 第 4 节中，怎样统计「每种请求的平均耗时」而不是直方图？为什么说在这个例子中，平均值会误导人？
3. 要知道一个程序的 `read` 系统调用每次读了多少字节、耗时多久，应该用 `sys_enter_read` 和 `sys_exit_read` 这两个 tracepoint，还是 `kprobe:vfs_read` 和 `kretprobe:vfs_read`？各有什么优缺点？

<details>
<summary>点击查看答案</summary>

1.
   ```
   tracepoint:raw_syscalls:sys_enter { @[comm] = count(); }
   interval:s:1 { print(@); clear(@); }
   ```
   `raw_syscalls:sys_enter` 在每个系统调用进入时触发，按进程名计数；每秒打印一次再清空，就是每秒的次数。

2. 把 `hist(...)` 换成 `avg(...)`：`@us[@type[tid]] = avg((nsecs - @start[tid]) / 1000);`。在这个例子中，每种请求的主体都在几微秒到几十微秒，但有千分之一的请求要 20 多毫秒，是主体的上千倍。这些少数的慢请求会把平均值拉高很多（比如查询的平均值会比大部分查询的耗时高出一截），而平均值本身又看不出「大部分很快、极少数极慢」的形状，既夸大了一般请求的耗时，又掩盖了慢请求的严重程度。直方图或者分位数（p99、p999）更能说明问题。

3. tracepoint 的接口稳定，参数有名字（`args.count`、`args.ret`），而且覆盖所有的 `read` 系统调用，包括读管道、套接字；返回探针中 `args.ret` 就是实际读到的字节数。`vfs_read` 是内核内部的函数，名字和参数可能随内核版本变化；它能看到文件对象等更多的内核信息，也会被 `read` 以外的路径（比如 `pread64`、`readv` 的部分实现）调用，统计的范围不完全一样。只关心系统调用的语义时，优先用 tracepoint。

</details>
