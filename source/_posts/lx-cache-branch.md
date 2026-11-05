---
title: CPU 缓存与分支预测
date: 2026-11-10 10:30:00
updated: 2026-11-10 10:30:00
categories:
  - Linux基础
  - 性能分析
tags:
  - Linux
  - 性能
  - CPU
order: 4
description: 同样是把 8192×8192 个整数加起来，按行加要 15 毫秒，按列加要 207 毫秒。本文先测出这台机器访问内存的延迟：128 KB 以内不到 1 纳秒，超过 16 MB 就涨到几十纳秒，最后到 106 纳秒。虚拟机里 perf stat 读不到硬件计数器，就用 valgrind 的 cachegrind 模拟：按列访问每一次都缺失，按行访问每 16 次才缺失一次。分支预测也一样：统计大于 128 的字节之和，强制保留分支时，乱序的数组比排好序的慢 7 倍；而 GCC 默认生成的条件选择指令根本没有分支，排不排序都一样。最后看两个线程写相邻的变量，为什么会慢 4 倍。
top_group_list: true
disableNunjucks: true
viz: [lx-perf]
---

> **本文目标**：理解 CPU 缓存的层级和缓存行，知道访问各级缓存和内存的延迟是什么量级；理解按行、按列访问的差别；会用 `perf stat`（物理机）或 cachegrind（虚拟机）统计缓存缺失和分支预测失败；理解分支预测失败的代价，以及编译器用条件选择指令消除分支；理解伪共享，会用填充避免它。
>
> **前置知识**：C 语言的数组和指针；《数据结构》专栏的链表和数组（为什么数组遍历更快）；本章第 1、3 篇。
>
> **实验环境**：同第 1 篇。宿主机 Apple M6，macOS 报告性能核的 L1 数据缓存 128 KB、L2 缓存 20 MB；虚拟机中 `/sys/devices/system/cpu/cpu0/cache` 不提供大小，缓存行 64 字节。valgrind 3.22。

## 1. 为什么要有缓存

CPU 执行一条加法指令不到 1 纳秒，从内存读一个数却要 100 纳秒左右。如果每次都直接访问内存，CPU 大部分时间都在等数据。所以 CPU 和内存之间有几级**缓存**：

- **L1**：每个核心自己的，几十到一百多 KB，几个时钟周期就能读到；
- **L2**：几 MB 到几十 MB，有的 CPU 每个核心一份，有的几个核心共享（Apple 的 CPU 就是共享的）；
- **L3**：很多 x86 服务器还有一级几十 MB 的共享缓存。

缓存和内存之间交换数据的单位是**缓存行**，通常 64 字节。读一个 `int`（4 字节）时，它所在的整个 64 字节都会被读进缓存；接下来读相邻的 15 个 `int` 就不用再去内存了。所以**访问的顺序**很重要：顺序访问时，一次缺失换来 15 次命中，CPU 的预取器还会提前把后面的缓存行读进来；随机访问时，几乎每次都缺失。

## 2. 测出各级缓存的延迟

怎样测「读一次内存要多久」？如果只是顺序地读一个数组，CPU 会同时发出很多个读请求，还会预取，测出来的是带宽而不是延迟。办法是**指针追逐**：让每次读到的值就是下一次要读的地址，下一次读必须等这一次读完，CPU 无法提前：

```c
// 每个节点占一个缓存行（64 字节），next 指向随机的另一个节点，所有节点连成一个环
struct Node {
    struct Node* next;
    char pad[56];
};

// ……把 n 个节点按随机的顺序连成环……
for (long i = 0; i < steps; i++) p = p->next;
```

环的大小从 4 KB 逐渐加大到 512 MB，每次走 2000 万步：

```
$ ./build/cache latency 512
     4 KB     1.3 ns
     8 KB     1.1 ns
    16 KB     0.9 ns
    32 KB     0.7 ns
    64 KB     0.6 ns
   128 KB     0.6 ns
   256 KB     2.2 ns
   512 KB     2.5 ns
     1 MB     3.3 ns
     2 MB     6.0 ns
     4 MB     6.7 ns
     8 MB     6.9 ns
    16 MB    10.6 ns
    32 MB    36.9 ns
    64 MB    73.0 ns
   128 MB    86.9 ns
   256 MB    97.9 ns
   512 MB   105.6 ns
```

<figure class="diagram">
<svg viewBox="0 0 700 290" role="img" aria-label="指针追逐测出的访问延迟：128 KB 以内在 L1 中，不到 1 纳秒；256 KB 到 16 MB 在 L2 中，2 到 10 纳秒；超过 20 MB 的 L2 以后，延迟涨到 100 纳秒左右">
  <defs><marker id="lat-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="lat-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <line x1="60" y1="220.0" x2="654" y2="220.0" style="stroke:var(--border);stroke-width:1"/><text x="52.0" y="224.0" text-anchor="end" style="fill:var(--muted);font-size:10px">0</text><line x1="60" y1="176.8" x2="654" y2="176.8" style="stroke:var(--border);stroke-width:1"/><text x="52.0" y="180.8" text-anchor="end" style="fill:var(--muted);font-size:10px">25</text><line x1="60" y1="133.6" x2="654" y2="133.6" style="stroke:var(--border);stroke-width:1"/><text x="52.0" y="137.6" text-anchor="end" style="fill:var(--muted);font-size:10px">50</text><line x1="60" y1="90.5" x2="654" y2="90.5" style="stroke:var(--border);stroke-width:1"/><text x="52.0" y="94.5" text-anchor="end" style="fill:var(--muted);font-size:10px">75</text><line x1="60" y1="47.3" x2="654" y2="47.3" style="stroke:var(--border);stroke-width:1"/><text x="52.0" y="51.3" text-anchor="end" style="fill:var(--muted);font-size:10px">100</text><text x="52.0" y="18.0" text-anchor="start" style="fill:var(--muted);font-size:10px">纳秒</text><rect x="64.0" y="217.8" width="25.0" height="2.2" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="76.5" y="212.8" text-anchor="middle" style="fill:var(--muted);font-size:9px">1.3</text><text x="76.5" y="234.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">4K</text><rect x="97.0" y="218.1" width="25.0" height="1.9" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="109.5" y="213.1" text-anchor="middle" style="fill:var(--muted);font-size:9px">1.1</text><text x="109.5" y="234.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">8K</text><rect x="130.0" y="218.4" width="25.0" height="1.6" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="142.5" y="213.4" text-anchor="middle" style="fill:var(--muted);font-size:9px">0.9</text><text x="142.5" y="234.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">16K</text><rect x="163.0" y="218.8" width="25.0" height="1.2" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="175.5" y="213.8" text-anchor="middle" style="fill:var(--muted);font-size:9px">0.7</text><text x="175.5" y="234.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">32K</text><rect x="196.0" y="219.0" width="25.0" height="1.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="208.5" y="214.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">0.6</text><text x="208.5" y="234.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">64K</text><rect x="229.0" y="219.0" width="25.0" height="1.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="241.5" y="214.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">0.6</text><text x="241.5" y="234.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">128K</text><rect x="262.0" y="216.2" width="25.0" height="3.8" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="274.5" y="211.2" text-anchor="middle" style="fill:var(--muted);font-size:9px">2.2</text><text x="274.5" y="234.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">256K</text><rect x="295.0" y="215.7" width="25.0" height="4.3" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="307.5" y="210.7" text-anchor="middle" style="fill:var(--muted);font-size:9px">2.5</text><text x="307.5" y="234.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">512K</text><rect x="328.0" y="214.3" width="25.0" height="5.7" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="340.5" y="209.3" text-anchor="middle" style="fill:var(--muted);font-size:9px">3.3</text><text x="340.5" y="234.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">1M</text><rect x="361.0" y="209.6" width="25.0" height="10.4" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="373.5" y="204.6" text-anchor="middle" style="fill:var(--muted);font-size:9px">6</text><text x="373.5" y="234.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">2M</text><rect x="394.0" y="208.4" width="25.0" height="11.6" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="406.5" y="203.4" text-anchor="middle" style="fill:var(--muted);font-size:9px">6.7</text><text x="406.5" y="234.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">4M</text><rect x="427.0" y="208.1" width="25.0" height="11.9" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="439.5" y="203.1" text-anchor="middle" style="fill:var(--muted);font-size:9px">6.9</text><text x="439.5" y="234.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">8M</text><rect x="460.0" y="201.7" width="25.0" height="18.3" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="472.5" y="196.7" text-anchor="middle" style="fill:var(--muted);font-size:9px">10.6</text><text x="472.5" y="234.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">16M</text><rect x="493.0" y="156.3" width="25.0" height="63.7" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="505.5" y="151.3" text-anchor="middle" style="fill:var(--muted);font-size:9px">36.9</text><text x="505.5" y="234.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">32M</text><rect x="526.0" y="93.9" width="25.0" height="126.1" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="538.5" y="88.9" text-anchor="middle" style="fill:var(--muted);font-size:9px">73</text><text x="538.5" y="234.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">64M</text><rect x="559.0" y="69.9" width="25.0" height="150.1" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="571.5" y="64.9" text-anchor="middle" style="fill:var(--muted);font-size:9px">86.9</text><text x="571.5" y="234.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">128M</text><rect x="592.0" y="50.9" width="25.0" height="169.1" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="604.5" y="45.9" text-anchor="middle" style="fill:var(--muted);font-size:9px">97.9</text><text x="604.5" y="234.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">256M</text><rect x="625.0" y="37.6" width="25.0" height="182.4" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="637.5" y="32.6" text-anchor="middle" style="fill:var(--muted);font-size:9px">105.6</text><text x="637.5" y="234.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">512M</text><path d="M62,244 L62,250 L256,250 L256,244" style="fill:none;stroke:var(--accent);stroke-width:1.5"/><text x="159.0" y="266.0" text-anchor="middle" style="fill:var(--accent);font-size:11px">L1（128 KB）</text><path d="M260,244 L260,250 L487,250 L487,244" style="fill:none;stroke:var(--warn);stroke-width:1.5"/><text x="373.5" y="266.0" text-anchor="middle" style="fill:var(--warn);font-size:11px">L2（20 MB）</text><path d="M491,244 L491,250 L652,250 L652,244" style="fill:none;stroke:var(--danger);stroke-width:1.5"/><text x="571.5" y="266.0" text-anchor="middle" style="fill:var(--danger);font-size:11px">内存</text>
  </g>
</svg>
</figure>

能看到两个明显的台阶，正好对应宿主机的缓存大小：

- **128 KB 以内**：都在 L1 中，不到 1 纳秒；
- **256 KB 到 16 MB**：放不进 L1，在 L2 中，2 到 10 纳秒；
- **32 MB 以上**：超过了 20 MB 的 L2，要去内存，延迟迅速涨到 100 纳秒左右。

几点说明：

- 128 KB 以内测出的 0.6 纳秒比 L1 的标称延迟（3 个周期左右）还要低。环很小时，几十个节点的跳转顺序是固定重复的，新的 CPU 有预测加载地址甚至加载结果的机制，可能「猜中」了下一个地址。这一段只看量级；
- 超过 L2 以后延迟不是一下子跳到 100 纳秒，而是逐渐上升：数据越大，命中 L2 的比例越低；同时，跳转的范围越大，**TLB**（页表的缓存，见《虚拟内存与缺页》）缺失也越多，每次还要多查一次页表；
- 1 纳秒和 100 纳秒差了 100 倍。一个链表很长、节点分散在内存中的程序，每访问一个节点就要等 100 纳秒。

## 3. 按行与按列

C 语言的二维数组按行存放：`m[0][0]`、`m[0][1]`、……、`m[0][n-1]`、`m[1][0]`……。两个函数做的计算完全一样，只是循环的顺序不同：

```c
long sumRows(const int* m, int n)   // 按行：顺序访问
{
    long s = 0;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) s += m[(size_t)i * n + j];
    return s;
}

long sumCols(const int* m, int n)   // 按列：每次跳过一整行
{
    long s = 0;
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) s += m[(size_t)i * n + j];
    return s;
}
```

<div class="dsviz" data-viz="lx-cache"></div>

实测：

| 矩阵 | 大小 | 按行 | 按列 | 慢了 |
|---|---|---|---|---|
| 1024×1024 | 4 MB | 0.3 ms | 1.1 ms | 4.3 倍 |
| 4096×4096 | 64 MB | 3.9 ms | 49.0 ms | 12.7 倍 |
| 8192×8192 | 256 MB | 15.1 ms | 206.5 ms | 13.7 倍 |

4 MB 的矩阵能放进 L2，按列也只慢 4 倍；超过 L2 以后，按列访问每次都要去内存，慢了 13 倍左右。

## 4. 数缓存缺失：perf stat 与 cachegrind

慢了是因为缓存缺失，这只是推断。在物理机上，CPU 的**硬件性能计数器**（PMU）可以直接数出缓存缺失、分支预测失败的次数：

```bash
perf stat -e cycles,instructions,cache-references,cache-misses,branches,branch-misses ./build/cache rowcol 4096
```

可惜这台虚拟机没有提供 PMU：

```
   <not supported>      instructions
```

很多云服务器、几乎所有的 Mac 虚拟机都是这样。这时可以用 valgrind 的 **cachegrind**：它在一个模拟的 CPU 上逐条执行程序，按设定的缓存参数模拟每一次访问，统计命中和缺失。结果不受虚拟机影响、可以重复，但程序会慢几十倍，而且模拟的缓存只是近似（只有两级、没有预取等）。按宿主机的参数设置（L1 数据缓存 128 KB、8 路组相联、64 字节的缓存行；最后一级缓存设成 16 MB，因为 cachegrind 要求组数是 2 的幂）：

```bash
valgrind --tool=cachegrind --cache-sim=yes \
    --I1=65536,4,64 --D1=131072,8,64 --LL=16777216,16,64 \
    ./build/cache rowcol 4096
cg_annotate --show=Dr,D1mr,DLmr cachegrind.out.<pid>
```

`Dr` 是读数据的次数，`D1mr` 是 L1 读缺失，`DLmr` 是最后一级缓存读缺失：

```
          Dr               D1mr              DLmr
  16,777,216 (50.0%)  16,777,216 (94.1%)  16,777,216 (94.1%)  sumCols
  16,777,216 (50.0%)   1,048,577  (5.9%)   1,048,577  (5.9%)  sumRows
```

两个函数各读了 16777216 次（4096 × 4096）：

- **按行**：缺失 1048577 次，正好是 1/16。每个缓存行 64 字节放 16 个 `int`，读第一个时缺失，后面 15 个都命中；
- **按列**：**每一次都缺失**，而且不只是 L1，最后一级缓存也全部缺失，次次都要去内存。

在 cachegrind 中，按行 85.9 毫秒，按列 321.4 毫秒，只慢了 3.7 倍，比真实的 12.7 倍小：cachegrind 只模拟缺失的次数，不模拟内存访问真正的延迟。**用它数次数，不要用它测时间。**

## 5. 分支预测

CPU 是**流水线**式执行的：一条指令还没执行完，后面的十几条已经在取指、译码了。遇到 `if` 这样的条件分支，在条件算出来之前，CPU 不知道接下来该执行哪一边。它不会停下来等，而是**猜一边**（分支预测）先执行下去：猜对了，什么损失也没有；猜错了，已经执行的那些指令全部作废，从正确的一边重新开始，损失十几个时钟周期。

分支预测器根据这个分支过去的表现来猜。下面的函数统计一个字节数组中大于等于 128 的元素之和：

```c
long sumBig(const uint8_t* d, size_t n)
{
    long s = 0;
    for (size_t i = 0; i < n; i++)
        if (d[i] >= 128) s += d[i];
    return s;
}
```

数组中是随机的字节，`if` 的结果完全随机，预测器只能猜对一半；排好序以后，前一半全是「否」，后一半全是「是」，几乎每次都猜对。32 MB 的数组，各求和 10 次：

```
$ ./build/cache branch 33554432
乱序  ：141.7 ms
排好序：141.8 ms
```

**一样快**。看 `-O2` 编译出来的代码：

```
    14b0:	ldrb	w1, [x2], #1
    14b4:	tst	x1, #0x80
    14b8:	add	x1, x0, w1, uxtb
    14bc:	csel	x0, x1, x0, ne
```

没有条件跳转。GCC 把 `if` 变成了「先算出 `s + d[i]`，再用 `csel`（条件选择）指令根据条件选 `s + d[i]` 还是 `s`」。这叫 **if 转换**：两边都算，用一条不跳转的指令选结果。没有分支，自然没有预测失败。x86-64 上对应的指令是 `cmov`。

用 `-fno-if-conversion -fno-if-conversion2 -fno-tree-vectorize` 编译（`build/cache-branchy`），强制保留分支：

```
    14f0:	ldrb	w1, [x2]
    14f4:	add	x2, x2, #0x1
    14f8:	tbz	w1, #7, 1500 <sumBig+0x20>    // 第 7 位是 0（小于 128）就跳过加法
    14fc:	add	x0, x0, w1, uxtb
```

```
$ ./build/cache-branchy branch 33554432
乱序  ：760.1 ms
排好序：107.4 ms
```

乱序慢了 **7.1 倍**。用 cachegrind 的分支模拟（`--branch-sim=yes`）数一下，数组改成 4 MB：

| 版本 | `sumBig` 中的条件分支 | 预测失败 |
|---|---|---|
| `csel`（默认的 `-O2`） | 83,886,100 | 48 |
| 保留分支 | 167,772,180 | 20,986,797 |

保留分支的版本每个元素有两个条件分支（循环本身一个、`if` 一个），一共 4M × 10 次 × 2 遍（乱序、排序）× 2 = 1.68 亿个；预测失败 2099 万次，约等于乱序那一遍中 `if` 的一半（4194万的一半），正好是「随机，猜对一半」。排好序的那一遍几乎没有失败。`csel` 版本只有循环的分支，48 次失败只发生在每次循环结束时。

还有一个有意思的地方：排好序时，保留分支的版本（107 毫秒）反而比 `csel` 的版本（142 毫秒）快。分支能猜对时，CPU 可以跳过不需要的加法；`csel` 两边都要算，还让每次的结果依赖于上一次的选择。**if 转换不是总是更好**，编译器要猜数据的规律，猜错了也会变慢。数据规律明显（比如大部分请求都走同一个分支）时，分支往往更快；数据随机时，`csel` 更稳。

## 6. 伪共享

最后一种和缓存有关的问题出现在多线程中。两个线程各自累加自己的计数器，互不相干：

```c
static char counters[256] __attribute__((aligned(256)));
static size_t gap;   // 两个计数器相隔的字节数

static void* bump(void* arg)
{
    volatile long* c = (volatile long*)(counters + (arg ? gap : 0));
    for (long i = 0; i < 200 * 1000 * 1000; i++) (*c)++;
    return NULL;
}
```

```
间隔   8 字节：0.22 秒
间隔  64 字节：0.05 秒
间隔 128 字节：0.05 秒
```

两个计数器紧挨着时，慢了 **4.4 倍**。两个变量在同一个缓存行中，而缓存一致性是以缓存行为单位的：一个核心要写这一行，必须先让另一个核心缓存中的这一行失效；另一个核心接着写自己的变量，又要把这一行抢回来。两个线程明明没有共享任何数据，缓存行却在两个核心之间来回传递。这叫**伪共享**（false sharing）。

<figure class="diagram">
<svg viewBox="0 0 700 236" role="img" aria-label="伪共享：左边两个计数器在同一个缓存行中，核心 0 写 A、核心 1 写 B，这一行要在两个核心的缓存之间来回传递；右边隔开 64 字节，各在各的缓存行中，互不干扰">
  <defs><marker id="shr-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="shr-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="20.0" y="22.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">间隔 8 字节：0.22 秒</text><rect x="20" y="36" width="150" height="30" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="95.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">核心 0（写 A）</text><text x="20.0" y="88.0" text-anchor="start" style="fill:var(--muted);font-size:10px">L1 中的缓存行</text><rect x="20" y="94" width="70" height="28" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="55.0" y="112.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">A</text><rect x="94" y="94" width="70" height="28" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="129.0" y="112.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">B</text><line x1="95" y1="66" x2="95" y2="92" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#shr-a)"/><rect x="190" y="36" width="150" height="30" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="265.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">核心 1（写 B）</text><text x="190.0" y="88.0" text-anchor="start" style="fill:var(--muted);font-size:10px">L1 中的缓存行</text><rect x="190" y="94" width="70" height="28" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="225.0" y="112.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">A</text><rect x="264" y="94" width="70" height="28" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="299.0" y="112.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">B</text><line x1="265" y1="66" x2="265" y2="92" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#shr-a)"/><path d="M90,124 Q178,176 264,126" style="fill:none;stroke:var(--danger);stroke-width:1.5" marker-end="url(#shr-d)"/><path d="M270,124 Q178,150 96,126" style="fill:none;stroke:var(--danger);stroke-width:1.5" marker-end="url(#shr-d)"/><text x="178.0" y="196.0" text-anchor="middle" style="fill:var(--danger);font-size:11px">写之前要让对方的这一行失效</text><text x="178.0" y="214.0" text-anchor="middle" style="fill:var(--danger);font-size:11px">缓存行来回传递</text><text x="370.0" y="22.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">间隔 64 字节：0.05 秒</text><rect x="370" y="36" width="150" height="30" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="445.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">核心 0（写 A）</text><text x="370.0" y="88.0" text-anchor="start" style="fill:var(--muted);font-size:10px">L1 中的缓存行</text><rect x="370" y="94" width="144" height="28" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="442.0" y="112.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">A 所在的行</text><line x1="445" y1="66" x2="445" y2="92" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#shr-a)"/><rect x="540" y="36" width="150" height="30" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="615.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">核心 1（写 B）</text><text x="540.0" y="88.0" text-anchor="start" style="fill:var(--muted);font-size:10px">L1 中的缓存行</text><rect x="540" y="94" width="144" height="28" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="612.0" y="112.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">B 所在的行</text><line x1="615" y1="66" x2="615" y2="92" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#shr-a)"/><text x="528.0" y="196.0" text-anchor="middle" style="fill:var(--accent);font-size:11px">各写各的缓存行</text><text x="528.0" y="214.0" text-anchor="middle" style="fill:var(--accent);font-size:11px">不需要同步</text>
  </g>
</svg>
</figure>

隔开 64 字节，两个变量就在不同的缓存行中，互不干扰。所以多线程程序中，每个线程频繁写的数据（计数器、统计信息、队列的头尾指针）要放在不同的缓存行中，常见的写法：

```c
struct PerThread {
    long count;
    char pad[64 - sizeof(long)];   // 填满一个缓存行
} __attribute__((aligned(64)));
```

C11 的 `_Alignas(64)` 也可以；C++17 提供了 `std::hardware_destructive_interference_size`（通常是 64）。有的 CPU 的预取器每次成对地取两个缓存行，这时要隔开 128 字节才彻底，这台机器上 64 字节已经够了。

## 7. 常见错误

1. **以为算法的复杂度相同，速度就差不多**。同样是 O(n²)，按列访问慢了 13 倍；链表的遍历和数组一样是 O(n)，可能慢几十倍。
2. **用顺序读数组测内存延迟**。测出来的是带宽，要用指针追逐。
3. **在虚拟机中用 `perf stat` 看缓存缺失，看到 `<not supported>` 就放弃**。用 cachegrind 数次数，或者到物理机上测。
4. **用 cachegrind 的耗时判断优化的效果**。它只模拟缺失次数，不模拟延迟。
5. **测分支预测前不看反汇编**。编译器可能已经把分支变成了 `csel`/`cmov`，或者向量化了；本文最初的测试中，GCC 甚至把 10 次重复调用合并成了一次（`sumBig` 没有副作用），要加 `__attribute__((noipa))`。
6. **多线程的计数器放在一个数组里**，`long counts[NTHREADS]`，每个线程写自己的那一项，正好全在同一个缓存行中。

## 8. 小结

- 缓存以 64 字节的缓存行为单位；这台机器上 L1 不到 1 纳秒，L2 2 到 10 纳秒，内存约 100 纳秒；
- 按行访问 8192×8192 的矩阵 15 毫秒，按列 207 毫秒；cachegrind 显示按列每次读都缺失，按行每 16 次缺失一次；
- 虚拟机中没有 PMU 时，`perf stat` 读不到硬件事件，用 cachegrind 模拟统计；
- 保留分支时，乱序数组慢 7.1 倍，2099 万次预测失败；GCC 默认用 `csel` 消除了分支，排不排序都一样，但排好序时反而比分支版本慢；
- 两个线程写同一个缓存行中的不同变量，慢 4.4 倍，隔开 64 字节就好了。

## 9. 练习

1. 一个程序用链表存了 1000 万个节点，每个节点是单独 `malloc` 的。遍历一次大约需要多久？换成数组呢？（用本文测出的延迟估算）
2. 第 3 节中，1024×1024 的矩阵按列访问只慢了 4.3 倍。为什么比大矩阵慢得少？
3. 下面的代码中，哪个循环更可能被编译器变成 `csel`？
   ```c
   for (i = 0; i < n; i++) if (a[i] > 0) s += a[i];          // A
   for (i = 0; i < n; i++) if (a[i] > 0) s += expensive(a[i]); // B
   ```

<details>
<summary>点击查看答案</summary>

1. 1000 万个节点约 640 MB（按每个节点占一个 64 字节的缓存行估算，实际 `malloc` 的块大小可能不同），节点分散在内存中，几乎每访问一个节点都要去内存，按约 100 纳秒估算，一次遍历要 1 秒左右。数组是顺序访问，有预取、一次缺失换来很多次命中，主要受内存带宽限制，几十毫秒就能完成。实际中，如果节点是按顺序连续分配的，它们在内存中也大致连续，链表会快很多；但经过多次插入、删除以后，顺序就乱了。

2. 1024×1024 个 `int` 是 4 MB，能完全放进 20 MB 的 L2。按列访问在 L1 中仍然缺失，但每次缺失只需要去 L2 取，几纳秒，而不是去内存的 100 纳秒。所以慢得少。

3. A。`s += a[i]` 计算很便宜，两边都算再选一个，代价只是一次加法；B 中 `expensive` 是一次函数调用，不能「两边都算」，而且调用可能有副作用，编译器不能在条件为假时也去执行它，只能保留分支。

</details>
