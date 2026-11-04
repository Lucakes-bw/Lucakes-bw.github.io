---
title: 虚拟内存与缺页
date: 2026-11-09 10:00:00
updated: 2026-11-09 10:00:00
categories:
  - Linux基础
  - 内存管理
tags:
  - Linux
  - 内存
order: 1
description: 每个进程都以为自己独占了一大片连续的内存，其实看到的是虚拟地址，由页表翻译成物理地址。本文打印一个进程的地址空间，看代码、全局变量、堆、共享库、栈各在哪里；申请 1 GB 内存，VSZ 涨了 1 GB，RSS 一点没变；读 100 MB 发生了 25603 次缺页，RSS 却只涨了 0.1 MB；写 100 MB 才真正分配了物理页。再用透明大页把 65536 次缺页变成 128 次，并发现内存碎片会让大页分配失败。
top_group_list: true
disableNunjucks: true
viz: [lx-mem]
---

> **本文目标**：理解虚拟地址、物理地址和页表；能读懂 `/proc/<pid>/maps`；理解 VSZ 和 RSS 的区别；理解按需分配和缺页（次缺页、主缺页）；知道零页和透明大页。
>
> **前置知识**：C 语言专栏的指针和动态内存；本专栏的《进程：fork、exec、wait》和《内存映射 mmap》。本章的完整代码在 [projects/lx-memory](https://github.com/Lucakes-bw/Lucakes-bw.github.io/tree/main/projects/lx-memory)。
>
> **实验环境**：Ubuntu 24.04 容器（aarch64），内核 6.8，GCC 13.3；虚拟机 2 核、2 GB 内存，没有交换分区；页大小 4096 字节，透明大页为 `madvise` 模式。容器以 `--privileged` 运行，以便清空页缓存、修改内核参数。

## 1. 每个进程都有自己的地址空间

C 程序中的指针是一个地址，比如 `0xab3b8e84a2a0`。两个进程中的指针可能是同一个数值，指向的却是不同的数据；一个进程也不可能用指针去读另一个进程的内存。因为程序中的地址都是**虚拟地址**：

- 每个进程有自己独立的**虚拟地址空间**（64 位系统上，用户态可以使用上百 TB 的地址）；
- 内存被划分成固定大小的**页**（通常 4 KB）。每个进程有一张**页表**，记录「虚拟页 → 物理页」的对应关系；
- CPU 访问内存时，由硬件（MMU）查页表，把虚拟地址翻译成物理地址。查过的结果缓存在 **TLB** 中。

<figure class="diagram">
<svg viewBox="0 0 700 230" role="img" aria-label="虚拟地址到物理地址：每个进程有自己的页表，把虚拟页映射到物理页。两个进程的虚拟页可以映射到同一个物理页（比如共享库），没有映射的虚拟页不占物理内存">
  <defs><marker id="vpt-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="vpt-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="20.0" y="22.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">进程 A 的虚拟地址</text><rect x="20" y="34" width="160" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="100.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">虚拟页 0</text><rect x="20" y="78" width="160" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="100.0" y="99.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">虚拟页 1</text><rect x="20" y="122" width="160" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="100.0" y="143.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">虚拟页 2</text><rect x="20" y="166" width="160" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="100.0" y="187.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">虚拟页 3</text><text x="520.0" y="22.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">进程 B 的虚拟地址</text><rect x="520" y="34" width="160" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="600.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">虚拟页 0</text><rect x="520" y="78" width="160" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="600.0" y="99.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">虚拟页 1</text><rect x="520" y="122" width="160" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="600.0" y="143.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">虚拟页 2</text><rect x="520" y="166" width="160" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="600.0" y="187.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">虚拟页 3</text><text x="270.0" y="22.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">物理内存</text><rect x="270" y="34" width="160" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="350.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">物理页 7</text><rect x="270" y="78" width="160" height="34" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="350.0" y="99.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">物理页 2（libc）</text><rect x="270" y="122" width="160" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="350.0" y="143.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">物理页 9</text><rect x="270" y="166" width="160" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="350.0" y="187.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">物理页 4</text><line x1="180" y1="51" x2="268" y2="51" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#vpt-a)"/><line x1="180" y1="95" x2="268" y2="95" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#vpt-a)"/><line x1="180" y1="139" x2="268" y2="139" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#vpt-a)"/><line x1="520" y1="51" x2="432" y2="183" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#vpt-a)"/><line x1="520" y1="95" x2="432" y2="95" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#vpt-a)"/><text x="20.0" y="214.0" text-anchor="start" style="fill:var(--muted);font-size:11px">虚拟页 3 没有映射：不占物理内存，访问时缺页</text><text x="520.0" y="214.0" text-anchor="start" style="fill:var(--accent);font-size:11px">两个进程共享 libc 的物理页</text>
  </g>
</svg>
</figure>

这样做的好处：进程之间互相隔离；每个进程都能使用连续的地址，物理内存却可以零散分布；多个进程可以把同一个物理页映射进自己的地址空间（共享库、写时复制）；虚拟地址可以先分配、用到时再分配物理内存。

## 2. 地址空间里有什么

`/proc/self/maps` 列出了当前进程地址空间中的每一个区域。程序打印几个典型的地址，并找出它们所在的区域：

```c
int g_inited = 42;          // 已初始化的全局变量：.data
int g_zero;                 // 未初始化的全局变量：.bss

int main(void)
{
    int local = 0;
    static const char rodata[] = "hello";
    char* small = malloc(100);
    char* big = malloc(64 * MB);
    whereIs("main（代码）", (void*)main);
    /* …… */
}
```

```
main（代码）     0xab3b6d690cc0  在  ab3b6d690000-ab3b6d692000 r-xp 00000000 00:30 1453108   /root/lx-memory/build/vm
字符串常量       0xab3b6d691c68  在  ab3b6d690000-ab3b6d692000 r-xp 00000000 00:30 1453108   /root/lx-memory/build/vm
g_inited         0xab3b6d6b0010  在  ab3b6d6b0000-ab3b6d6b1000 rw-p 00010000 00:30 1453108   /root/lx-memory/build/vm
g_zero           0xab3b6d6b0018  在  ab3b6d6b0000-ab3b6d6b1000 rw-p 00010000 00:30 1453108   /root/lx-memory/build/vm
malloc(100)      0xab3b8e84a2a0  在  ab3b8e84a000-ab3b8e86b000 rw-p 00000000 00:00 0         [heap]
malloc(64 MB)    0xfff0e9edf010  在  fff0e9edf000-fff0edee0000 rw-p 00000000 00:00 0
printf（libc）   0xfff0edf30590  在  fff0edee0000-fff0ee07a000 r-xp 00000000 00:30 1052956   /usr/lib/aarch64-linux-gnu/libc.so.6
局部变量         0xffffc7274bd4  在  ffffc7255000-ffffc7276000 rw-p 00000000 00:00 0         [stack]
```

每一行依次是：地址范围、权限（`r` 读、`w` 写、`x` 执行、`p` 私有 / `s` 共享）、在文件中的偏移、设备号、inode、映射的文件。

<figure class="diagram">
<svg viewBox="0 0 700 300" role="img" aria-label="一个进程的地址空间（低地址在下）：可执行文件的代码和数据、堆（向上增长）、mmap 区域（大块内存、共享库）、栈（向下增长）。实测中的 malloc(100) 在堆中，malloc(64 MB) 在 mmap 区域">
  <defs><marker id="vly-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="vly-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="140" y="14" width="300" height="30" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="290.0" y="33.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">[stack]  局部变量</text><text x="130.0" y="34.0" text-anchor="end" style="fill:var(--muted);font-size:10px">0xffffc72…</text><text x="260.0" y="67.0" text-anchor="start" style="fill:var(--muted);font-size:10px">栈向下增长</text><rect x="140" y="82" width="300" height="30" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="290.0" y="101.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">mmap 区域：libc.so.6、malloc(64 MB)</text><text x="130.0" y="102.0" text-anchor="end" style="fill:var(--muted);font-size:10px">0xfff0e9e…</text><rect x="140" y="120" width="300" height="40" rx="8" style="fill:none;stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:5 4"/><text x="290.0" y="145.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">（大片没有映射的地址）</text><rect x="140" y="168" width="300" height="30" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="290.0" y="187.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">[heap]  malloc(100)  ↑</text><text x="130.0" y="188.0" text-anchor="end" style="fill:var(--muted);font-size:10px">0xab3b8e8…</text><rect x="140" y="206" width="300" height="30" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="290.0" y="225.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">.data / .bss  全局变量</text><text x="130.0" y="226.0" text-anchor="end" style="fill:var(--muted);font-size:10px">0xab3b6d6b…</text><rect x="140" y="244" width="300" height="30" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="290.0" y="263.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">代码、只读常量（r-xp）</text><text x="130.0" y="264.0" text-anchor="end" style="fill:var(--muted);font-size:10px">0xab3b6d69…</text><text x="460.0" y="34.0" text-anchor="start" style="fill:var(--muted);font-size:11px">高地址</text><text x="460.0" y="268.0" text-anchor="start" style="fill:var(--muted);font-size:11px">低地址</text><text x="460.0" y="140.0" text-anchor="start" style="fill:var(--muted);font-size:11px">堆用 brk 向上扩展</text><text x="460.0" y="158.0" text-anchor="start" style="fill:var(--muted);font-size:11px">大块内存、共享库用 mmap</text>
  </g>
</svg>
</figure>

- **代码和常量**在可执行文件映射出的只读、可执行区域（`r-xp`）。写这里会 Segmentation fault；
- **全局变量**在可读写的数据区（`rw-p`），`.data` 的初始值来自文件，`.bss` 由内核清零；
- **`malloc(100)`** 在 `[heap]` 中：堆紧挨着程序的数据区，用 `brk` 系统调用向上扩展；
- **`malloc(64 MB)`** 不在堆中，而是一个没有文件的匿名区域：大块内存由 `mmap` 单独分配（下一篇）；
- **共享库**映射在高地址；`printf` 的代码在 `libc.so.6` 的映射中，所有进程共享同一份物理内存；
- **栈**在最高处，向低地址增长；默认最大 8 MB（`ulimit -s`）。

## 3. 申请内存时只分配了地址

两个衡量进程内存的指标：

- **VSZ**（虚拟内存大小，`/proc/self/status` 中的 `VmSize`）：地址空间中已经分配出去的地址有多少；
- **RSS**（常驻内存，`VmRSS`）：真正映射了物理页的部分有多大。

用 `mmap` 申请 1 GB：

```c
char* p = mmap(NULL, 1024 * MB, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
```

```
申请前：VSZ     2.2 MB，RSS    1.4 MB
申请后：VSZ  1026.2 MB，RSS    1.4 MB
```

**VSZ 涨了 1 GB，RSS 一点没变**。内核只是在地址空间中记下「这 1 GB 的地址归你了」，页表中还没有任何对应的物理页。

## 4. 缺页

### 4.1 第一次访问时才分配

访问一个还没有物理页的虚拟地址时，MMU 在页表中找不到对应项，触发一个**缺页异常**（page fault）。内核处理它：确认这个地址确实属于进程的某个区域，分配一个物理页（清零），填进页表，然后回到程序，重新执行那条访问内存的指令。程序完全感觉不到，只是这一次访问慢了一些。

<div class="dsviz" data-viz="lx-fault"></div>

逐页访问前 200 MB，用 `getrusage` 统计缺页次数：

```c
long f0 = minorFaults();
double r0 = rssMB(), t0 = nowSeconds();
for (long i = 0; i < bytes; i += step) p[i] = 1;
```

```
读前 100 MB                      缺页  25603 次，RSS    +0.1 MB，用时    8.5 毫秒
写前 100 MB                      缺页  25600 次，RSS  +100.0 MB，用时   22.7 毫秒（每次缺页 0.89 微秒）
再写一遍这 100 MB                缺页      0 次，RSS    +0.0 MB，用时    0.3 毫秒
写第 100～200 MB，每 1 KB 写一次 缺页  25600 次，RSS  +100.0 MB，用时   11.0 毫秒（每次缺页 0.43 微秒）
现在：VSZ  1026.2 MB，RSS  201.4 MB
```

- **写 100 MB：25600 次缺页**（100 MB / 4 KB = 25600），RSS 增加了 100 MB。每次缺页零点几到一微秒；
- **再写一遍：0 次缺页，0.3 毫秒**，快了 70 多倍。物理页已经在页表中了，这才是内存访问本来的速度；
- **每 1 KB 写一次**：访问次数是 4 倍，缺页次数不变，因为缺页是**按页**发生的；
- **只读不写：也有 25603 次缺页，RSS 却只涨了 0.1 MB**。

### 4.2 零页

最后一点是怎么回事？读一个从没写过的匿名页，内容一定全是 0。内核不必为它分配新的物理页，而是把它映射到一个全局共享的、内容全为 0 的只读页（**零页**）。所以读了 100 MB，RSS 几乎不变。等到写的时候，再触发一次缺页，分配真正的物理页（写时复制）。

所以「读了一遍」不能保证内存已经分配好了。需要提前分配（比如避免运行时的缺页抖动）时，要逐页写，或者用 `mmap` 的 `MAP_POPULATE`、`mlock`。

### 4.3 次缺页和主缺页

- **次缺页**（minor fault）：不需要读磁盘就能处理。比如第一次访问匿名内存、访问已经在页缓存中的文件页、写时复制；
- **主缺页**（major fault）：要从磁盘读数据，比如 `mmap` 的文件页不在页缓存中，或者页被换出到了交换分区。一次主缺页可能要几毫秒。

`ps -o min_flt,maj_flt -p <pid>` 或者 `/usr/bin/time -v` 可以看到一个进程的缺页次数。主缺页多，往往说明内存不够、在频繁地读磁盘。

## 5. 透明大页

### 5.1 一次缺页映射 2 MB

页越小，页表越大，TLB 能覆盖的内存越少，缺页次数也越多。x86-64 和 ARM64 都支持 **2 MB 的大页**：一个页表项直接映射 2 MB。Linux 的**透明大页**（THP）让普通的匿名内存也能使用大页，程序不用改。本文的环境是 `madvise` 模式，只对用 `madvise(MADV_HUGEPAGE)` 标记过的区域使用：

```c
char* aligned = (char*)(((unsigned long)q + 2 * MB - 1) & ~(2 * MB - 1));   // 大页要 2 MB 对齐
madvise(aligned, 256 * MB, MADV_HUGEPAGE);
```

```
写 256 MB，MADV_HUGEPAGE         缺页    128 次，RSS  +256.0 MB，用时    4.5 毫秒（每次缺页 34.87 微秒）
写 256 MB，普通的 4 KB 页        缺页  65536 次，RSS  +256.0 MB，用时   35.9 毫秒（每次缺页 0.55 微秒）
/proc/self/smaps_rollup 中的 AnonHugePages：256 MB
```

**65536 次缺页变成了 128 次**（256 MB / 2 MB），总时间从 35.9 毫秒降到 4.5 毫秒。每次大页缺页要清零 2 MB，所以单次慢得多（35 微秒），但次数少了 512 倍。之后的访问中，TLB 的命中率也更高。

### 5.2 大页不一定分配得到

第一次运行这个实验时，结果是这样的：

```
写 256 MB，MADV_HUGEPAGE         缺页  24656 次，RSS  +256.0 MB，用时   52.0 毫秒
/proc/self/smaps_rollup 中的 AnonHugePages：160 MB
```

只有 160 MB 用上了大页。`/proc/vmstat` 中的统计：

```
thp_fault_alloc 82
thp_fault_fallback 48
```

**48 次大页分配失败，退回到了 4 KB 的小页**，正好是 (256 − 160) / 2。大页要求 2 MB 连续、对齐的物理内存，当时虚拟机的内存大部分被页缓存占着、碎片化了，找不到足够的连续空间。清空页缓存（`echo 3 > /proc/sys/vm/drop_caches`）以后再运行，256 MB 就全部是大页了。

透明大页的代价：分配时可能需要整理内存（compaction）；只用了 2 MB 中的一小部分，也占 2 MB；所以有的数据库建议关闭它，或者只在 `madvise` 模式下让需要的程序自己开启（本章《fork 与写时复制》会实测它和写时复制的关系）。

## 6. 归还

```
munmap 以后：VSZ     2.2 MB，RSS    1.4 MB
```

`munmap` 同时释放了地址和物理页。`free` 却不一定会把内存还给操作系统，这是下一篇的内容。

## 7. 常见错误

1. **把 VSZ 当成内存占用**。VSZ 几十 GB 很常见（比如预留了大量地址的 Java、Go 程序），真正占用物理内存的是 RSS。
2. **以为 `malloc` 之后内存就分配好了**。物理页在第一次写入时才分配，第一次访问会慢；内存不够时，也是在这时才出问题（第 5 篇）。
3. **以为「读一遍」就能预先分配内存**。读未写过的匿名内存映射的是零页，RSS 不变。
4. **RSS 简单相加**。共享库、共享内存、写时复制的页在多个进程的 RSS 中都会被算上；要看「这个进程独占的内存」，用 `/proc/<pid>/smaps_rollup` 中的 `Private_*`，或者 `Pss`（按共享的进程数平分）。
5. **以为开了透明大页就一定用上了大页**。内存碎片化时会退回小页，用 `AnonHugePages` 和 `thp_fault_fallback` 确认。

## 8. 小结

- 进程使用的是虚拟地址，由页表翻译成物理地址；`/proc/<pid>/maps` 列出了地址空间中的代码、数据、堆、匿名映射、共享库和栈；
- 申请内存只分配地址（VSZ），第一次写入时才通过缺页分配物理页（RSS）：申请 1 GB，RSS 不变；写 100 MB，25600 次缺页；
- 读从没写过的内存映射到零页，25603 次缺页，RSS 只涨 0.1 MB；
- 透明大页一次映射 2 MB：缺页从 65536 次变成 128 次；物理内存碎片化时会退回小页。

## 9. 练习

1. 一个进程的 VSZ 是 20 GB，RSS 是 300 MB，机器只有 8 GB 内存。这正常吗？
2. 一个服务启动后的前几秒响应很慢，之后恢复正常，`perf` 显示大量时间花在 `do_page_fault` 上。可能是什么原因？怎么改进？
3. 两个进程都运行同一个 100 MB 的程序（代码段 100 MB），`ps` 中各自的 RSS 都包括这 100 MB 吗？机器上实际用了多少内存？

<details>
<summary>点击查看答案</summary>

1. 正常。VSZ 只是分配出去的虚拟地址，可能包括预留但没有使用的区域（线程栈、内存池、`mmap` 的大文件），这些地址没有对应的物理页，不占内存。真正占用物理内存的是 RSS 的 300 MB。

2. 服务启动后第一次访问它申请的内存（缓存、内存池、各种数据结构）时，每一页都要缺页、分配、清零。大量的缺页集中在启动后的前几秒，请求处理就变慢了。改进：启动时预先写一遍要用的内存（或者用 `MAP_POPULATE`、`mlock`），把缺页放在开始接受请求之前；对大块内存使用透明大页，减少缺页的次数。

3. 代码段是从可执行文件映射的、只读的，两个进程映射的是页缓存中同一份物理页，所以两个进程的 RSS 中都包括它（只包括实际访问过的部分），但物理内存只用了一份。这就是 RSS 相加会高估内存使用的原因；用 PSS（`/proc/<pid>/smaps_rollup` 中的 `Pss`），每个进程只算一半。

</details>
