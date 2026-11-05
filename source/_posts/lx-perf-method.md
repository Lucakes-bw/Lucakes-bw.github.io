---
title: 性能分析从哪里入手：time、负载与等待
date: 2026-11-10 10:00:00
updated: 2026-11-10 10:00:00
categories:
  - Linux基础
  - 性能分析
tags:
  - Linux
  - 性能
order: 1
description: 程序慢，先别急着优化代码：它可能根本没在用 CPU。本文用几个故意写慢的小程序，看 time 输出的 real、user、sys 怎么读；4 个进程等磁盘时 CPU 99% 空闲，平均负载却涨到 3.89，因为 Linux 把等磁盘的进程也算进了负载；4 个计算进程挤在 2 个核上，pidstat 显示每个进程一半的时间在排队。最后把这些现象整理成一张查找瓶颈的路线图，对应本章后面的几篇。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会读 `time` 的 real、user、sys，知道三者的关系说明了什么；理解 Linux 的平均负载包含哪些进程，为什么负载高不等于 CPU 忙；会用 `vmstat`、`pidstat` 和 `/proc/<pid>/schedstat` 区分「在计算」「等 CPU」「等 IO」；建立「先判断时间花在哪一类，再选工具」的习惯。
>
> **前置知识**：本专栏的《进程与系统监控》（`top`、`/proc` 的基本用法）；《内存管理》一章的《页缓存与写回》（`O_DIRECT`）。本章的完整代码在 [projects/lx-perf](https://github.com/Lucakes-bw/Lucakes-bw.github.io/tree/main/projects/lx-perf)。
>
> **实验环境**：Ubuntu 24.04 容器（aarch64），内核 6.8，GCC 13.3，perf 6.8，bpftrace 0.20.2，sysstat 12.6；虚拟机 2 核、2 GB 内存，宿主机是 Apple M6。容器以 `--privileged --pid=host` 运行，数据盘用 `--device-read-bps`、`--device-write-bps` 限速到 100 MB/s，模拟普通的云盘。

## 1. 时间花在了哪里

一个程序运行了 10 秒，这 10 秒中它可能在：

- **在 CPU 上运行**：执行自己的代码（用户态），或者在系统调用中执行内核的代码（内核态）；
- **不在 CPU 上**：等磁盘、等网络、等锁、睡眠，或者已经准备好了，但 CPU 被别人占着，在**运行队列**中排队。

前者叫 **on-CPU**，后者叫 **off-CPU**。两种情况的分析方法完全不同：on-CPU 要找出是哪些代码在消耗 CPU，用采样（第 3 篇）；off-CPU 要找出在等什么、等了多久（第 5 篇）。如果程序其实在等磁盘，花一周时间优化计算的代码，一点用也没有。所以性能分析的第一步，是判断时间属于哪一类。

<figure class="diagram">
<svg viewBox="0 0 700 170" role="img" aria-label="一个线程的 10 秒：在 CPU 上运行的时间（用户态、内核态）叫 on-CPU，perf 采样只看得到这一部分；等磁盘、等锁、睡眠、排队等 CPU 的时间叫 off-CPU，要用别的方法分析">
  <defs><marker id="spl-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="spl-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="60" width="140" height="40" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="90.0" y="84.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">用户态</text><rect x="160" y="60" width="60" height="40" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="190.0" y="84.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">内核态</text><rect x="220" y="60" width="170" height="40" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="305.0" y="84.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">等磁盘</text><rect x="390" y="60" width="90" height="40" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="435.0" y="84.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">等锁</text><rect x="480" y="60" width="80" height="40" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="520.0" y="84.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">排队等 CPU</text><rect x="560" y="60" width="120" height="40" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="620.0" y="84.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">睡眠</text><path d="M20,50 L20,42 L220,42 L220,50" style="fill:none;stroke:var(--accent);stroke-width:1.5"/><text x="120.0" y="34.0" text-anchor="middle" style="fill:var(--accent);font-size:12px">on-CPU：在 CPU 上运行</text><path d="M220,50 L220,42 L680,42 L680,50" style="fill:none;stroke:var(--fg-2);stroke-width:1.5"/><text x="450.0" y="34.0" text-anchor="middle" style="fill:var(--fg);font-size:12px">off-CPU：不在 CPU 上</text><text x="120.0" y="124.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">perf 采样（第 3 篇）</text><text x="120.0" y="142.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">user + sys</text><text x="450.0" y="124.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">off-CPU 分析（第 5 篇）</text><text x="450.0" y="142.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">real −（user + sys），多线程时还要算排队</text><text x="20.0" y="164.0" text-anchor="start" style="fill:var(--muted);font-size:11px">一共 10 秒 = real</text>
  </g>
</svg>
</figure>

## 2. time：real、user、sys

`time` 是最简单的工具，它报告三个时间：

- **real**：从开始到结束的实际时间（墙上时钟）；
- **user**：在用户态执行用了多少 CPU 时间；
- **sys**：在内核态执行用了多少 CPU 时间。

`load` 程序有几种模式，各自代表一种「慢」：

```c
// spin：一直计算，直到指定的秒数
static void spinFor(double sec)
{
    double end = nowSeconds() + sec;
    unsigned long x = 1;
    while (nowSeconds() < end)
        for (int i = 0; i < 100000; i++) x = x * 6364136223846793005UL + 1;
    sink = x;
}

// tiny：每次 write 1 字节，一共 n 次
for (long i = 0; i < n; i++) CHECK(write(fd, "x", 1) == 1);

// sleep：什么都不做
usleep((useconds_t)(atof(argv[2]) * 1e6));
```

用 Bash 自带的 `time` 运行（2 核的机器）：

| 命令 | real | user | sys | 说明 |
|---|---|---|---|---|
| `load spin 2` | 2.001 | 2.001 | 0.000 | 一直在计算 |
| `load tiny 16000000` | 1.954 | 0.530 | 1.424 | 大部分时间在内核中 |
| `load sleep 2` | 2.006 | 0.000 | 0.001 | 一直在等 |
| `load threads 2 2` | 2.002 | 4.000 | 0.001 | 2 个线程同时计算 |
| `load threads 4 2` | 2.002 | 4.003 | 0.001 | 4 个线程，但只有 2 个核 |

<figure class="diagram">
<svg viewBox="0 0 700 250" role="img" aria-label="time 的结果：虚线框是 real，实心条是 user（蓝）加 sys（黄）。spin 的 CPU 时间等于 real；tiny 大部分在内核态；sleep 几乎没有 CPU 时间；多线程时 CPU 时间超过 real；4 个线程在 2 核上只拿到 4 秒 CPU">
  <defs><marker id="tim-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="tim-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="140.0" y="47.0" text-anchor="end" style="fill:var(--fg);font-size:12px">spin 2</text><rect x="150.0" y="34.0" width="180.1" height="18.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="150" y="30" width="180.1" height="26" rx="3" style="fill:none;stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:5 4"/><text x="338.1" y="47.0" text-anchor="start" style="fill:var(--muted);font-size:10px">real 2.0  user 2.0  sys 0.0</text><text x="140.0" y="87.0" text-anchor="end" style="fill:var(--fg);font-size:12px">tiny 16000000</text><rect x="150.0" y="74.0" width="47.7" height="18.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="197.7" y="74.0" width="128.2" height="18.0" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><rect x="150" y="70" width="175.9" height="26" rx="3" style="fill:none;stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:5 4"/><text x="333.9" y="87.0" text-anchor="start" style="fill:var(--muted);font-size:10px">real 2.0  user 0.5  sys 1.4</text><text x="140.0" y="127.0" text-anchor="end" style="fill:var(--fg);font-size:12px">sleep 2</text><rect x="150.0" y="114.0" width="1.5" height="18.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="150" y="110" width="180.5" height="26" rx="3" style="fill:none;stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:5 4"/><text x="338.5" y="127.0" text-anchor="start" style="fill:var(--muted);font-size:10px">real 2.0  user 0.0  sys 0.0</text><text x="140.0" y="167.0" text-anchor="end" style="fill:var(--fg);font-size:12px">threads 2 2</text><rect x="150.0" y="154.0" width="360.0" height="18.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="150" y="150" width="180.2" height="26" rx="3" style="fill:none;stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:5 4"/><text x="518.1" y="167.0" text-anchor="start" style="fill:var(--muted);font-size:10px">real 2.0  user 4.0  sys 0.0</text><text x="140.0" y="207.0" text-anchor="end" style="fill:var(--fg);font-size:12px">threads 4 2</text><rect x="150.0" y="194.0" width="360.3" height="18.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="150" y="190" width="180.2" height="26" rx="3" style="fill:none;stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:5 4"/><text x="518.4" y="207.0" text-anchor="start" style="fill:var(--muted);font-size:10px">real 2.0  user 4.0  sys 0.0</text><line x1="150" y1="228" x2="150" y2="232" style="stroke:var(--border-2)"/><text x="150.0" y="245.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">0 秒</text><line x1="240" y1="228" x2="240" y2="232" style="stroke:var(--border-2)"/><text x="240.0" y="245.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1 秒</text><line x1="330" y1="228" x2="330" y2="232" style="stroke:var(--border-2)"/><text x="330.0" y="245.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">2 秒</text><line x1="420" y1="228" x2="420" y2="232" style="stroke:var(--border-2)"/><text x="420.0" y="245.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">3 秒</text><line x1="510" y1="228" x2="510" y2="232" style="stroke:var(--border-2)"/><text x="510.0" y="245.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">4 秒</text><line x1="150" y1="228" x2="510" y2="228" style="stroke:var(--border-2)"/>
  </g>
</svg>
</figure>

读法：

- **user + sys ≈ real**：单线程程序一直在 CPU 上，瓶颈是计算。再看 user 和 sys 哪个大：user 大，优化自己的代码；sys 大，说明系统调用太多或者太慢，`tiny` 每次只写 1 字节，1600 万次系统调用，sys 占了 73%，应该改成攒够一大块再写（《文件描述符与系统调用》中实测过，快了 100 多倍）。
- **user + sys 远小于 real**：大部分时间在等，CPU 的代码再快也没用，要找出在等什么。`sleep` 是极端的例子。
- **user + sys > real**：多个线程同时在多个核上运行。2 个线程跑 2 秒，用了 4 秒 CPU。

最后一行值得注意：4 个线程各自计算 2 秒，如果每个线程都能一直运行，应该用掉 8 秒 CPU，实际只有 4 秒。因为只有 2 个核，任何时刻只有 2 个线程在运行，另外 2 个在排队，每个线程只运行了一半的时间。**排队等 CPU 也是 off-CPU**，`time` 看不出来，后面用 `pidstat` 看。

## 3. 平均负载：不只是 CPU

`uptime` 和 `top` 第一行显示的**平均负载**（load average）常被理解成「CPU 有多忙」。《进程与系统监控》中说它是等待运行的进程数，这其实只说了一半。做个实验：4 个进程用 `O_DIRECT` 不停地读磁盘（`load dread`，绕过页缓存，每次都要等磁盘），150 秒后，换成 4 个进程计算（`load spin`），每 15 秒记录一次 `/proc/loadavg`：

```bash
for i in 1 2 3 4; do ./build/load dread /data/big.bin & done
```

读磁盘的进程状态全是 `D`（不可中断的睡眠，在等 IO），几乎不用 CPU：

```
    PID STAT %CPU COMMAND
    432 D     0.0 load
    433 D     0.0 load
    434 D     0.0 load
    435 D     0.1 load
```

两个阶段的 1 分钟平均负载：

| 时间（秒） | 0 | 15 | 30 | 60 | 90 | 120 | 150 |
|---|---|---|---|---|---|---|---|
| 4 个进程等磁盘 | 0.18 | 1.48 | 2.04 | 2.81 | 3.28 | 3.82 | 3.89 |
| 4 个进程计算 | 0.02 | 1.38 | 1.96 | 2.77 | 3.25 | 3.55 | 3.73 |

<figure class="diagram">
<svg viewBox="0 0 700 270" role="img" aria-label="1 分钟平均负载：4 个进程等磁盘（实线）和 4 个进程计算（虚线），两条曲线几乎重合，都向 4 靠近；可是前者 CPU 99% 空闲，后者 CPU 100% 在用户态">
  <defs><marker id="lav-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="lav-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <line x1="70" y1="210.0" x2="630" y2="210.0" style="stroke:var(--border);stroke-width:1"/><text x="62.0" y="214.0" text-anchor="end" style="fill:var(--muted);font-size:10px">0</text><line x1="70" y1="165.0" x2="630" y2="165.0" style="stroke:var(--border);stroke-width:1"/><text x="62.0" y="169.0" text-anchor="end" style="fill:var(--muted);font-size:10px">1</text><line x1="70" y1="120.0" x2="630" y2="120.0" style="stroke:var(--border);stroke-width:1"/><text x="62.0" y="124.0" text-anchor="end" style="fill:var(--muted);font-size:10px">2</text><line x1="70" y1="75.0" x2="630" y2="75.0" style="stroke:var(--border);stroke-width:1"/><text x="62.0" y="79.0" text-anchor="end" style="fill:var(--muted);font-size:10px">3</text><line x1="70" y1="30.0" x2="630" y2="30.0" style="stroke:var(--border);stroke-width:1"/><text x="62.0" y="34.0" text-anchor="end" style="fill:var(--muted);font-size:10px">4</text><text x="70.0" y="228.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">0 秒</text><text x="182.0" y="228.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">30 秒</text><text x="294.0" y="228.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">60 秒</text><text x="406.0" y="228.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">90 秒</text><text x="518.0" y="228.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">120 秒</text><text x="630.0" y="228.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">150 秒</text><path d="M70.0,201.9 L126.0,143.4 L182.0,118.2 L238.0,98.8 L294.0,83.5 L350.0,71.4 L406.0,62.4 L462.0,48.5 L518.0,38.1 L574.0,36.3 L630.0,34.9" style="fill:none;stroke:var(--accent);stroke-width:2"/><path d="M70.0,209.1 L126.0,147.9 L182.0,121.8 L238.0,101.5 L294.0,85.3 L350.0,73.2 L406.0,63.8 L462.0,56.1 L518.0,50.2 L574.0,45.8 L630.0,42.2" style="fill:none;stroke:var(--warn);stroke-width:2;stroke-dasharray:6 4"/><circle cx="70.0" cy="201.9" r="3" style="fill:var(--accent)"/><circle cx="126.0" cy="143.4" r="3" style="fill:var(--accent)"/><circle cx="182.0" cy="118.2" r="3" style="fill:var(--accent)"/><circle cx="238.0" cy="98.8" r="3" style="fill:var(--accent)"/><circle cx="294.0" cy="83.5" r="3" style="fill:var(--accent)"/><circle cx="350.0" cy="71.4" r="3" style="fill:var(--accent)"/><circle cx="406.0" cy="62.4" r="3" style="fill:var(--accent)"/><circle cx="462.0" cy="48.5" r="3" style="fill:var(--accent)"/><circle cx="518.0" cy="38.1" r="3" style="fill:var(--accent)"/><circle cx="574.0" cy="36.3" r="3" style="fill:var(--accent)"/><circle cx="630.0" cy="34.9" r="3" style="fill:var(--accent)"/><circle cx="70.0" cy="209.1" r="3" style="fill:var(--warn)"/><circle cx="126.0" cy="147.9" r="3" style="fill:var(--warn)"/><circle cx="182.0" cy="121.8" r="3" style="fill:var(--warn)"/><circle cx="238.0" cy="101.5" r="3" style="fill:var(--warn)"/><circle cx="294.0" cy="85.3" r="3" style="fill:var(--warn)"/><circle cx="350.0" cy="73.2" r="3" style="fill:var(--warn)"/><circle cx="406.0" cy="63.8" r="3" style="fill:var(--warn)"/><circle cx="462.0" cy="56.1" r="3" style="fill:var(--warn)"/><circle cx="518.0" cy="50.2" r="3" style="fill:var(--warn)"/><circle cx="574.0" cy="45.8" r="3" style="fill:var(--warn)"/><circle cx="630.0" cy="42.2" r="3" style="fill:var(--warn)"/><text x="626.0" y="24.9" text-anchor="end" style="fill:var(--accent);font-size:11px">等磁盘 3.89</text><text x="626.0" y="60.2" text-anchor="end" style="fill:var(--warn);font-size:11px">计算 3.73</text><text x="74.0" y="20.0" text-anchor="start" style="fill:var(--muted);font-size:11px">平均负载</text><text x="70.0" y="252.0" text-anchor="start" style="fill:var(--fg);font-size:11px">等磁盘时 vmstat：b = 4，wa = 100%（CPU 空闲）；计算时：r = 4，us = 100%</text>
  </g>
</svg>
</figure>

两条曲线几乎重合，都朝 4 涨上去。可是 CPU 的情况完全不同，用 `vmstat 1` 看：

```
等磁盘时：
 r  b   swpd   free   buff  cache   si   so    bi    bo   in   cs us sy id wa st gu
 0  4      0 313084  18072 1458596    0    0 102400     0   89  141  0  1  0 100  0  0
计算时：
 r  b   swpd   free   buff  cache   si   so    bi    bo   in   cs us sy id wa st gu
 4  0      0 326640  18360 1460032    0    0     0     0 4044 1139 100  0  0  0  0  0
```

- `r`：可运行的进程数（正在运行的和在排队的）；`b`：处于 `D` 状态的进程数；
- `bi`：每秒从块设备读入的 KB 数，102400 KB 正好是限速的 100 MB/s；
- `us`、`sy`、`id`、`wa`：CPU 时间中用户态、内核态、空闲、**IO 等待**的百分比。

等磁盘时 `b` 是 4、`wa` 是 100%，CPU 其实完全空闲（`wa` 是「空闲，而且有进程在等 IO」，CPU 本身随时可以干别的）；计算时 `r` 是 4、`us` 是 100%。

**Linux 的平均负载 = 可运行的进程数（R）+ 不可中断睡眠的进程数（D）的指数平均。** 这是 Linux 特有的算法（大多数 Unix 只算 R），本意是把等磁盘的进程也算作「系统的压力」。所以：

- 负载高，不一定是 CPU 忙，可能是磁盘慢，或者网络文件系统（NFS）卡住了；
- 先用 `vmstat` 看是 `r` 高还是 `b` 高，再决定往哪个方向查；
- 「1 分钟平均」不是过去 60 秒的算术平均，而是指数衰减的平均：负载突然从 0 变成 4，1 分钟后只显示 2.8 左右，要两三分钟才接近 4。看到负载在涨，问题可能已经持续了一会儿。

## 4. 等 CPU：pidstat 和 schedstat

回到第 2 节的问题：4 个计算进程挤在 2 个核上，每个进程等了多久？`pidstat` 是 sysstat 包中的工具，按进程统计。`-u` 看 CPU，`-w` 看上下文切换，`5 1` 表示统计 5 秒、输出 1 次：

```
$ pidstat -u -w -p 967,968,969,970 5 1
      UID       PID    %usr %system  %guest   %wait    %CPU   CPU  Command
        0       967   50.60    0.00    0.00   49.40   50.60     0  load
        0       968   49.80    0.00    0.00   50.20   49.80     1  load
        0       969   49.80    0.00    0.00   50.40   49.80     1  load
        0       970   49.80    0.00    0.00   50.20   49.80     0  load

      UID       PID   cswch/s nvcswch/s  Command
        0       967      0.00    256.00  load
        0       968      0.00    256.20  load
        ...
```

- `%wait`：**准备好了但在排队等 CPU** 的时间比例。每个进程一半时间在运行（`%usr` 50%），一半时间在等（`%wait` 50%）。`top` 中每个进程显示 50% 的 CPU，看不出它其实想要 100%；
- `cswch/s`：每秒**主动**让出 CPU 的次数（等 IO、等锁、睡眠）；
- `nvcswch/s`：每秒**被迫**让出 CPU 的次数（时间片用完，被调度器换下）。

这 4 个进程从不主动让出，每秒被抢占 256 次。反过来，如果一个程序 `cswch/s` 很高，说明它经常在等东西，就该做 off-CPU 分析了。

`pidstat` 的数据来自 `/proc/<pid>/schedstat`，它只有三个数：在 CPU 上运行的总时间（纳秒）、在运行队列中等待的总时间（纳秒）、运行过的时间片数。隔 5 秒读两次：

```
pid 967 schedstat0 509923413 510452750 272
pid 967 schedstat1 3049617136 2988821508 1561
```

5 秒中运行了 2.54 秒，排队 2.48 秒，对得上。

`tiny` 的 `pidstat` 则是另一种样子：`%usr` 25.74、`%system` 74.26、`%wait` 0，CPU 一直是它的，但四分之三的时间在内核中。

## 5. 一张路线图

把上面的现象整理一下，就是查找瓶颈的第一步：

<figure class="diagram">
<svg viewBox="0 0 700 330" role="img" aria-label="查找瓶颈的路线图：先用 time 比较 CPU 时间和墙上时间。CPU 时间接近 real 是计算慢，再看 user 和 sys；远小于 real 是在等，用 vmstat 和 pidstat 区分等 IO、等锁和排队等 CPU">
  <defs><marker id="rte-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rte-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="250" y="14" width="200" height="36" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="350.0" y="36.9" text-anchor="middle" style="fill:var(--fg);font-size:13px">程序慢</text><rect x="220" y="76" width="260" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="350.0" y="92.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">time：user + sys 和 real</text><text x="350.0" y="107.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">第 1 篇</text><line x1="350" y1="50" x2="350" y2="74" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rte-a)"/><rect x="20" y="150" width="200" height="40" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="120.0" y="166.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">user + sys ≈ real</text><text x="120.0" y="181.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">在计算</text><rect x="250" y="150" width="200" height="40" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="350.0" y="166.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">user + sys ≪ real</text><text x="350.0" y="181.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">在等</text><rect x="480" y="150" width="200" height="40" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="580.0" y="166.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">pidstat %wait 高</text><text x="580.0" y="181.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">vmstat r &gt; 核数</text><line x1="300" y1="116" x2="130" y2="148" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rte-a)"/><line x1="350" y1="116" x2="350" y2="148" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rte-a)"/><line x1="400" y1="116" x2="570" y2="148" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rte-a)"/><rect x="20" y="230" width="95" height="54" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="67.5" y="246.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">user 高</text><text x="67.5" y="261.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">perf 采样 3</text><text x="67.5" y="276.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">缓存分支 4</text><rect x="125" y="230" width="95" height="54" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="172.5" y="246.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">sys 高</text><text x="172.5" y="261.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">strace -c 2</text><text x="172.5" y="276.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">perf 看内核</text><line x1="90" y1="190" x2="68" y2="228" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rte-a)"/><line x1="150" y1="190" x2="172" y2="228" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rte-a)"/><rect x="250" y="230" width="95" height="54" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="297.5" y="246.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">b、wa 高</text><text x="297.5" y="261.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">等 IO</text><text x="297.5" y="276.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">strace -T 2</text><rect x="355" y="230" width="95" height="54" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="402.5" y="246.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">cswch/s 高</text><text x="402.5" y="261.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">等锁、网络</text><text x="402.5" y="276.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">off-CPU 5</text><line x1="320" y1="190" x2="298" y2="228" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rte-a)"/><line x1="380" y1="190" x2="402" y2="228" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rte-a)"/><rect x="480" y="230" width="200" height="54" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="580.0" y="246.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">CPU 不够</text><text x="580.0" y="261.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">减少线程、优化计算</text><text x="580.0" y="276.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">或者加 CPU</text><line x1="580" y1="190" x2="580" y2="228" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rte-a)"/><text x="350.0" y="312.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">任何一步都可以用 bpftrace（第 6 篇）统计函数的次数、参数和耗时</text>
  </g>
</svg>
</figure>

| 现象 | 说明 | 下一步 |
|---|---|---|
| user 高，user + sys ≈ real | 计算慢 | 用 perf 采样找热点函数（第 3 篇），看缓存和分支（第 4 篇） |
| sys 高 | 系统调用多或慢 | `strace -c` 统计系统调用（第 2 篇），perf 看内核中的热点 |
| user + sys 远小于 real，`vmstat` 中 `b`、`wa` 高 | 等 IO | 看等的是哪个文件、哪个调用（第 2、5 篇） |
| user + sys 远小于 real，`cswch/s` 高 | 等锁、等网络、等别的进程 | off-CPU 分析（第 5 篇） |
| `%wait` 高，`vmstat` 中 `r` 大于核数 | CPU 不够 | 减少线程、优化计算，或者加 CPU |

最后一篇的 bpftrace 可以在任何一步中使用：它能统计任意函数被调用的次数、参数和耗时，而不需要修改和重启程序。

还有两点要注意：

- 在虚拟机中，`vmstat` 的 `st`（steal）列表示虚拟机想运行、但宿主机把 CPU 给了别人的时间。云服务器上 `st` 高，说明宿主机太挤，程序本身没问题；
- **先看整体，再看局部**。先用 `time`、`vmstat` 判断大方向，再用专门的工具深入。直接打开 perf，很容易在一个占 2% 的函数上花掉一下午。

## 6. 常见错误

1. **不看 real 和 user + sys 的关系就开始优化代码**。程序可能大部分时间在等。
2. **把平均负载当成 CPU 使用率**。Linux 的负载包含等磁盘的进程，负载 4 时 CPU 可能完全空闲。
3. **以为 `top` 中进程的 CPU 是 50%，就说明它只需要一半的 CPU**。可能是 CPU 不够，它另一半时间在排队，看 `pidstat` 的 `%wait`。
4. **把 `wa`（IO 等待）当成 CPU 很忙**。`wa` 是空闲时间的一种，CPU 可以去做别的事。
5. **在负载突增的那一刻看 1 分钟平均负载**。它需要一两分钟才能反映出变化，看实时的 `vmstat` 中的 `r`、`b`。

## 7. 小结

- 时间分两类：on-CPU（用户态、内核态）和 off-CPU（等 IO、等锁、睡眠、排队等 CPU），分析方法不同；
- `time` 的三个数：user + sys ≈ real 是计算慢，远小于 real 是在等，大于 real 是多线程；
- Linux 的平均负载 = R + D 状态进程数的指数平均：4 个进程等磁盘，CPU 99% 空闲，负载涨到 3.89；
- `vmstat` 的 `r`、`b`、`us`、`sy`、`wa` 区分计算和等 IO；`pidstat` 的 `%wait` 是排队等 CPU 的比例，`cswch/s` 是主动让出 CPU 的次数；
- 4 个计算进程在 2 个核上，每个进程运行一半、排队一半，`schedstat` 中 5 秒运行 2.54 秒、排队 2.48 秒。

## 8. 练习

1. 一个程序 `time` 的结果是 real 10.0、user 0.3、sys 0.2。它慢在哪里？接下来用什么工具查？
2. 一台 8 核的服务器，平均负载是 30，`top` 显示 CPU 空闲 85%。可能是什么情况？
3. 为什么 `load threads 4 2` 的 real 仍然是 2 秒，而不是 4 秒？如果把 `spinFor` 改成「执行固定次数的计算」，real 会是多少？

<details>
<summary>点击查看答案</summary>

1. CPU 时间只有 0.5 秒，9.5 秒在等。接下来要知道它在等什么：先用 `vmstat` 看系统是否在等 IO（`b`、`wa`），用 `pidstat -w` 看它主动切换的次数；然后用 `strace -T` 看哪个系统调用耗时长（第 2 篇），或者做 off-CPU 分析，按调用栈统计等待的时间（第 5 篇）。

2. 负载 30、CPU 却空闲，说明大量进程处于 `D` 状态：比如磁盘很慢或者坏了，或者挂载的网络文件系统（NFS）卡住了，访问它的进程都在等。用 `vmstat` 看 `b` 列，`ps -eo stat,comm | grep "^D"` 找出这些进程，`cat /proc/<pid>/stack` 看它们卡在内核的什么地方。

3. `spinFor` 是按时间计算的：每个线程一直算到墙上时钟过了 2 秒就停，不管自己实际运行了多久，所以 real 总是 2 秒，每个线程只拿到 1 秒 CPU。如果改成固定的计算量（比如每个线程需要 2 秒 CPU），4 个线程一共需要 8 秒 CPU，2 个核要 4 秒才能做完，real 约为 4 秒，user 约为 8 秒。

</details>
