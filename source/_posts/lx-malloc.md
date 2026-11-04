---
title: malloc 是怎样分配内存的
date: 2026-11-09 10:10:00
updated: 2026-11-09 10:10:00
categories:
  - Linux基础
  - 内存管理
tags:
  - Linux
  - 内存
  - C
order: 2
description: malloc 不是系统调用，它是 glibc 中的一个内存分配器：小块从堆中切，大块直接 mmap。本文用 strace 看清 128 KB 的分界线；实测 malloc(1) 实际占 32 字节；分配 100 万个小块再释放掉 99.9%，RSS 一点没降，全部释放了也没降，调用 malloc_trim 才还给系统；8 个线程反复分配释放，glibc 建了 9 个 arena，结束时还占着 307 MB，限制成 1 个 arena 以后慢了 2.6 倍，换成 jemalloc 只要 0.42 秒、结束时 44 MB。
top_group_list: true
disableNunjucks: true
viz: [lx-mem]
---

> **本文目标**：理解 malloc 和系统调用 `brk`、`mmap` 的关系；知道小块和大块分配的区别；理解每个块的额外开销；理解为什么 `free` 以后 RSS 不降，会用 `malloc_trim`；理解多线程下的 arena，知道什么时候换用 jemalloc 这类分配器。
>
> **前置知识**：上一篇；C 语言专栏的动态内存。
>
> **实验环境**：同上一篇，glibc 2.39。

## 1. malloc 不是系统调用

向内核要内存只有两种方式：

- **`brk`/`sbrk`**：移动堆的末尾（program break），让 `[heap]` 区域变长或变短；
- **`mmap`**：在地址空间中另找一块地方，映射一段匿名内存。

两者都以页为单位，而且是系统调用，每次都要进入内核。程序却经常需要几十个字节的小块，每次 `malloc(24)` 都调用一次 `mmap` 显然不行。所以 **`malloc` 是 C 库中的一个内存分配器**：它向内核批发大块内存，再零售给程序；`free` 回来的块放进空闲链表，下次直接复用。

<figure class="diagram">
<svg viewBox="0 0 700 200" role="img" aria-label="程序、malloc 和内核：程序调用 malloc 和 free；malloc 是 C 库中的分配器，向内核用 brk 扩展堆、用 mmap 分配大块，再切成小块给程序，free 的块放进空闲链表复用">
  <defs><marker id="mly-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="mly-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="20" width="200" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="120.0" y="36.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">程序</text><text x="120.0" y="51.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">malloc(24) / free(p)</text><rect x="20" y="90" width="200" height="50" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="120.0" y="111.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">glibc malloc</text><text x="120.0" y="126.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">空闲链表、tcache、arena</text><rect x="20" y="160" width="200" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="120.0" y="181.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">内核</text><line x1="120" y1="60" x2="120" y2="88" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#mly-a)"/><line x1="80" y1="140" x2="80" y2="158" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#mly-a)"/><text x="60.0" y="154.0" text-anchor="middle" style="fill:var(--link);font-size:11px">brk</text><line x1="160" y1="140" x2="160" y2="158" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#mly-a)"/><text x="190.0" y="154.0" text-anchor="middle" style="fill:var(--link);font-size:11px">mmap</text><text x="260.0" y="42.0" text-anchor="start" style="fill:var(--fg);font-size:12px">每次分配都在用户态完成，大多不进入内核</text><text x="260.0" y="104.0" text-anchor="start" style="fill:var(--fg);font-size:12px">小块（&lt; 128 KB）：从堆中切，用 brk 一次扩展一大段</text><text x="260.0" y="126.0" text-anchor="start" style="fill:var(--fg);font-size:12px">大块（≥ 128 KB）：单独 mmap，free 时 munmap</text><text x="260.0" y="180.0" text-anchor="start" style="fill:var(--muted);font-size:11px">向内核「批发」，再「零售」给程序</text>
  </g>
</svg>
</figure>

## 2. 小块从堆中切，大块直接 mmap

分配几种大小，看指针落在堆中还是 mmap 区域：

```c
static const char* where(void* p)
{
    void* brk = sbrk(0);
    return p >= g_heapStart && p < brk ? "堆（brk）" : "mmap";
}
```

```
malloc(    100)：堆（brk） 0xaaadbda4d2a0
malloc( 102400)：堆（brk） 0xaaadbda4e320
malloc( 130048)：堆（brk） 0xaaadbda67330
malloc( 132096)：mmap       0xfff345a7f010
malloc(1048576)：mmap       0xfff34597e010
malloc(1 MB)：  mmap      ，free 它
再 malloc(1 MB)：堆（brk）（阈值已经调高）
```

用 `strace` 看对应的系统调用：

```
brk(0xab4ea2a89000)                     = 0xab4ea2a89000
mmap(NULL, 135168, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0xff7cd2bef000
mmap(NULL, 1052672, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0xff7cd2aee000
mmap(NULL, 1052672, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0xff7cd29ed000
munmap(0xff7cd29ed000, 1052672)         = 0
brk(0xab4ea2b89000)                     = 0xab4ea2b89000
```

- **127 KB 以内**的三次分配都从堆中切，只需要一次 `brk` 扩展堆（之后在已有的堆中切，不再进入内核）；
- **129 KB** 和 **1 MB** 各调用了一次 `mmap`（多出来的几 KB 是块头和页对齐）。分界线是 `M_MMAP_THRESHOLD`，默认 128 KB；
- 大块用 `mmap` 的好处：`free` 时直接 `munmap`，内存立即还给系统，不会在堆中留下一个大洞；
- **动态阈值**：`free` 一个 mmap 来的 1 MB 块（`munmap`）以后，glibc 认为程序会反复分配这么大的块，把阈值调高到 1 MB。再 `malloc(1 MB)`，就改为扩展堆（第二个 `brk` 正好向上扩展了 1 MB），避免反复 `mmap`/`munmap`。

## 3. 每个块的额外开销

```
malloc(   1)：可用   24 字节，相邻两块的地址相差   32 字节
malloc(   8)：可用   24 字节，相邻两块的地址相差   32 字节
malloc(  24)：可用   24 字节，相邻两块的地址相差   32 字节
malloc(  25)：可用   40 字节，相邻两块的地址相差   48 字节
malloc( 100)：可用  104 字节，相邻两块的地址相差  112 字节
malloc(1000)：可用 1000 字节，相邻两块的地址相差 1008 字节
分配 100 万个 1 字节的块：RSS +30.5 MB
```

<figure class="diagram">
<svg viewBox="0 0 700 160" role="img" aria-label="glibc 中一个块的结构：8 字节的头记录大小和标志，后面是用户数据；块的大小按 16 字节对齐，最小 32 字节。malloc(1) 实际占 32 字节，malloc(25) 占 48 字节">
  <defs><marker id="mck-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="mck-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="30" width="60" height="36" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="50.0" y="52.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">头 8</text><rect x="80" y="30" width="220" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="190.0" y="52.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">用户数据 24（malloc(1) 只用 1）</text><text x="310.0" y="52.0" text-anchor="start" style="fill:var(--fg);font-size:12px">= 32 字节（最小的块）</text><rect x="20" y="90" width="60" height="36" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="50.0" y="112.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">头 8</text><rect x="80" y="90" width="300" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="230.0" y="112.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">用户数据 40（malloc(25)）</text><text x="390.0" y="112.0" text-anchor="start" style="fill:var(--fg);font-size:12px">= 48 字节</text><text x="20.0" y="150.0" text-anchor="start" style="fill:var(--muted);font-size:11px">实测：100 万个 malloc(1)，RSS 增加 30.5 MB</text>
  </g>
</svg>
</figure>

glibc 中每个块（chunk）前面有一个 8 字节的头，记录块的大小和几个标志位；块的大小按 16 字节对齐，最小 32 字节。所以：

- `malloc(1)` 实际占 **32 字节**，100 万个 1 字节的块用了 30.5 MB；
- `malloc(24)` 正好用满最小的块，`malloc(25)` 就要 48 字节；
- 空闲时，块中的用户数据区被用来存放链表指针，这也是最小 32 字节的原因。

大量很小的对象，逐个 `malloc` 很浪费。可以一次分配一个数组，或者自己写一个对象池（《数据结构》专栏 LRU 一文中的节点也可以这样分配）。

## 4. free 以后内存还给系统了吗

### 4.1 碎片

分配 100 万个 100 字节的块（填满一段约 110 MB 的堆），然后释放其中的 99.9%，每 1000 个只留下最后一个：

```c
for (int i = 0; i < n; ++i)
    if (i % 1000 != 999) {   // 每 1000 个只留下最后一个
        free(ptrs[i]);
        ptrs[i] = NULL;
    }
```

```
分配 100 万个 100 字节的块：RSS +114.5 MB
释放 99.9%，只留下 1000 个：    RSS +114.7 MB
malloc_trim(0) 以后：         RSS +11.9 MB
全部释放：                    RSS +11.9 MB
```

**释放了 99.9%，RSS 一点没降。** 堆只能从末尾收缩（`brk` 只能移动堆顶），而留下的 1000 个块均匀分布在整个堆中，最后一个就在堆顶附近，堆一点也缩不回去。空闲的内存在 glibc 的空闲链表中，可以给以后的 `malloc` 用，但操作系统看来，它们仍然被这个进程占着。

<div class="dsviz" data-viz="lx-heap"></div>

**`malloc_trim(0)`** 遍历堆中所有的空闲块，把其中完整的空闲页用 `madvise(MADV_DONTNEED)` 还给内核（地址仍然保留）。RSS 从 114.7 MB 降到了 11.9 MB：剩下的 1000 个块每个都占着一个页（1000 × 4 KB ≈ 4 MB），加上分配器自己的数据结构。

### 4.2 一个都不留，也不一定还

如果全部释放呢？分别用 100 字节和 200 字节的块：

```
对照：100 万个 100 字节的块：分配后 RSS +110.4 MB，全部释放后 +110.4 MB，malloc_trim 后 -4.0 MB
对照：100 万个 200 字节的块：分配后 RSS +206.0 MB，全部释放后 +206.0 MB，malloc_trim 后 +0.0 MB
```

**全部释放了，RSS 也没降**，直到调用 `malloc_trim`。原因是 glibc 为了让 `free` 和下一次 `malloc` 更快，把刚释放的小块先放进各种缓存（每个线程的 tcache、fastbin），这些块不会立即和相邻的空闲块合并，堆顶也就不会收缩。用 `GLIBC_TUNABLES=glibc.malloc.tcache_count=0` 关掉 tcache 再运行，200 字节的块全部释放后降到了 +115.2 MB（还有一部分没有合并），100 字节的块（属于 fastbin）仍然是 +110.4 MB。

这些细节随 glibc 的版本变化。结论是：**`free` 只是还给了 malloc，不一定还给操作系统**。一个程序先处理了一大批数据、之后长期空闲，RSS 会一直停在高峰时的水平。需要时调用 `malloc_trim(0)`；或者把大块数据放进 `mmap` 分配的内存中，用完 `munmap`。

## 5. 多线程：arena

### 5.1 每个线程一个 arena

如果所有线程共用一个堆，每次 `malloc`/`free` 都要加锁，线程多时锁竞争很严重。glibc 的做法是建多个 **arena**（分配区），每个 arena 有自己的锁和空闲链表，线程尽量使用不同的 arena。默认最多 `8 × CPU 核数` 个。

实验：每个线程反复执行 20 轮「分配 50 万个 64 字节的块（约 40 MB），再全部释放」：

```
1 个线程，每个 20 轮×50 万次 malloc/free：0.66 秒；arena 2 个；结束时 RSS 39.7 MB（峰值 43.0 MB）
8 个线程，每个 20 轮×50 万次 malloc/free：3.61 秒；arena 9 个；结束时 RSS 306.8 MB（峰值 336.9 MB）
--- MALLOC_ARENA_MAX=1
8 个线程，每个 20 轮×50 万次 malloc/free：9.41 秒；arena 1 个；结束时 RSS 251.8 MB（峰值 282.0 MB）
--- jemalloc
8 个线程，每个 20 轮×50 万次 malloc/free：0.42 秒；arena 1 个；结束时 RSS 43.6 MB（峰值 283.9 MB）
```

<figure class="diagram">
<svg viewBox="0 0 700 210" role="img" aria-label="8 个线程反复分配释放小块：glibc 默认用时 3.61 秒、结束时 RSS 306.8 MB；限制为 1 个 arena 用时 9.41 秒、RSS 251.8 MB；jemalloc 用时 0.42 秒、RSS 43.6 MB">
  <defs><marker id="mar-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="mar-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="20.0" y="22.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">用时（秒）</text><text x="180.0" y="44.0" text-anchor="end" style="fill:var(--fg);font-size:12px">glibc 默认（9 个 arena）</text><rect x="190" y="32" width="122.7" height="16" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="320.7" y="44.0" text-anchor="start" style="fill:var(--muted);font-size:11px">3.61 秒</text><text x="180.0" y="68.0" text-anchor="end" style="fill:var(--fg);font-size:12px">MALLOC_ARENA_MAX=1</text><rect x="190" y="56" width="319.9" height="16" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="517.9" y="68.0" text-anchor="start" style="fill:var(--muted);font-size:11px">9.41 秒</text><text x="180.0" y="92.0" text-anchor="end" style="fill:var(--fg);font-size:12px">jemalloc</text><rect x="190" y="80" width="14.3" height="16" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="212.3" y="92.0" text-anchor="start" style="fill:var(--muted);font-size:11px">0.42 秒</text><text x="20.0" y="128.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">结束时的 RSS（MB）</text><text x="180.0" y="150.0" text-anchor="end" style="fill:var(--fg);font-size:12px">glibc 默认（9 个 arena）</text><rect x="190" y="138" width="306.8" height="16" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="504.8" y="150.0" text-anchor="start" style="fill:var(--muted);font-size:11px">306.8 MB</text><text x="180.0" y="174.0" text-anchor="end" style="fill:var(--fg);font-size:12px">MALLOC_ARENA_MAX=1</text><rect x="190" y="162" width="251.8" height="16" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="449.8" y="174.0" text-anchor="start" style="fill:var(--muted);font-size:11px">251.8 MB</text><text x="180.0" y="198.0" text-anchor="end" style="fill:var(--fg);font-size:12px">jemalloc</text><rect x="190" y="186" width="43.6" height="16" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="241.6" y="198.0" text-anchor="start" style="fill:var(--muted);font-size:11px">43.6 MB</text>
  </g>
</svg>
</figure>

- **8 个线程，glibc 建了 9 个 arena**（主 arena 加上每个线程一个）。所有线程都已经结束、所有的块都已经释放，**RSS 仍然有 307 MB**：每个 arena 都保留着自己用过的内存；
- **`MALLOC_ARENA_MAX=1`** 只用一个 arena：RSS 少了一些，但所有线程抢同一把锁，**用时从 3.61 秒变成 9.41 秒**；
- **jemalloc**（用 `LD_PRELOAD` 替换 malloc，程序不用改）：**0.42 秒，结束时 RSS 43.6 MB**。它为每个线程维护小块的缓存，空闲的内存会按时间衰减、主动还给系统。（「arena 1 个」是 glibc 的统计，jemalloc 不使用 glibc 的 arena，这一项对它没有意义。）

注意这台机器只有 2 个 CPU 核，8 个线程并不能同时运行，所以 8 个线程的总时间比 1 个线程长是正常的。

### 5.2 实际影响

多线程的服务（比如 MySQL、各种 Java 之外的服务端程序）经常遇到「RSS 只涨不跌」「内存比预期大很多」的问题，很多时候就是 glibc 的 arena 和碎片造成的。常见的对策：

- 设置 `MALLOC_ARENA_MAX=2` 或 `4`，在内存和速度之间折中；
- 换用 jemalloc 或 tcmalloc（Redis 默认就用 jemalloc：《数据库》专栏实验用的 Redis 8，`redis-server --version` 显示 `malloc=jemalloc-5.3.0`）；
- 定期调用 `malloc_trim(0)`。

## 6. 常见错误

1. **以为 `free` 以后 RSS 会下降**。内存还给了 malloc，不一定还给操作系统。
2. **用 RSS 的增长判断内存泄漏**。RSS 不降可能只是分配器的缓存和碎片；用 Valgrind、ASan 的泄漏检查（《C++ 工程实践》），或者 `mallinfo2()` 看分配器内部的统计。
3. **大量的 1～8 字节的小分配**，每个实际占 32 字节。
4. **在多线程程序中只看单线程的测试结果**。arena 让多线程程序的内存可能是单线程的好几倍。
5. **混用分配器**：`LD_PRELOAD` 了 jemalloc，却把它分配的内存交给另一个用 glibc 分配器的库去 `free`（静态链接了自己的 malloc 的库最容易出这种问题）。

## 7. 小结

- `malloc` 是 C 库中的分配器，用 `brk` 扩展堆、用 `mmap` 分配大块；默认 128 KB 以上用 `mmap`，阈值会动态调整；
- 每个块有 8 字节的头、按 16 字节对齐、最小 32 字节：`malloc(1)` 占 32 字节；
- 堆只能从末尾收缩，释放了 99.9% 的块，RSS 一点没降；全部释放后，由于 tcache 等缓存，也没有降；`malloc_trim(0)` 把空闲页还给系统；
- 多线程时 glibc 每个线程一个 arena：8 个线程结束后 RSS 仍有 307 MB；限制成 1 个 arena 慢了 2.6 倍；jemalloc 0.42 秒、44 MB。

## 8. 练习

1. 一个程序启动时读入一个 2 GB 的配置文件，解析成几千万个小对象，解析完就全部释放了，之后长期运行。为什么 RSS 一直是 2 GB 多？怎么改？
2. 为什么 glibc 不总是用 `mmap` 分配，那样 `free` 时内存就能立即还给系统了？
3. `malloc(0)` 返回什么？它占内存吗？

<details>
<summary>点击查看答案</summary>

1. 几千万个小对象都从堆（和各个 arena）中分配，释放以后进入 malloc 的空闲链表和缓存，堆顶可能还有没释放的对象挡着，内存没有还给操作系统，RSS 停在峰值。改法：解析完调用 `malloc_trim(0)`；或者把解析用的临时对象放在一块单独 `mmap` 的内存池中（自己按顺序分配），用完整体 `munmap`；或者换用会主动归还空闲内存的分配器（jemalloc）。

2. `mmap` 是系统调用，最小单位是一页（4 KB），每次都要进入内核、修改页表，`munmap` 还要刷新 TLB；第一次访问还要缺页。几十字节的分配如果每次都 `mmap`，既慢又浪费（一个 24 字节的对象占 4 KB）。所以小块从堆中切分、在用户态复用，只有大块才单独 `mmap`。

3. glibc 的 `malloc(0)` 返回一个有效的、唯一的指针（可以传给 `free`），它占用一个最小的块（32 字节）；C 标准也允许返回 `NULL`。不能通过这个指针读写任何字节。

</details>
