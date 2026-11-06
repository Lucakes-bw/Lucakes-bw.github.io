---
title: cgroup：限制容器的资源
date: 2026-11-12 10:20:00
updated: 2026-11-12 10:20:00
categories:
  - Linux基础
  - 容器原理
tags:
  - Linux
  - 容器
  - cgroup
order: 3
description: 命名空间决定容器看到什么，cgroup 决定容器能用多少：CPU、内存、进程数、IO。本文直接读写 /sys/fs/cgroup 下的文件来限制一组进程。CPU 配额有一个反直觉的现象：限制为 1 个 CPU，每秒处理的请求正好减半，中位延迟却没变，p99 延迟从 13.5 毫秒变成了 65 毫秒，因为 4 个线程在每 100 毫秒的周期里只用 50 毫秒就花光了配额，剩下的时间全部被暂停；同样的配额只用 1 个线程，p99 只有 5 毫秒。最后用 pids.max 挡住 fork 炸弹：第 99 次 fork 失败。
top_group_list: true
disableNunjucks: true
viz: [lx-ctr]
---

> **本文目标**：理解 cgroup v2 的层级结构和控制文件；会创建 cgroup、把进程放进去、设置 `cpu.max`、`memory.max`、`pids.max`；理解 CPU 配额（CFS bandwidth）按周期限流的机制，以及它为什么会增加尾延迟；会用 `cpu.stat` 中的 `nr_throttled`、`throttled_usec` 诊断限流；知道容器的线程数要和 CPU 配额匹配。
>
> **前置知识**：本章第 1 篇；本专栏《内存管理》一章的《内存不足：overcommit 与 OOM killer》（已经实测过 `memory.max` 和容器中的 OOM）；《性能分析》一章的第 1 篇（`%wait`、排队等 CPU）。
>
> **实验环境**：同第 1 篇，虚拟机 2 核。容器以 `--privileged --cgroupns=private` 运行，在容器中看到的 `/sys/fs/cgroup` 是它自己的 cgroup 子树，可以在里面创建子 cgroup。

## 1. cgroup v2 的文件接口

cgroup（control group）把进程分组，按组限制和统计资源。cgroup v2 的全部接口是一个文件系统，挂载在 `/sys/fs/cgroup`：

- 每个目录是一个 cgroup，创建目录就是创建 cgroup，子目录是子 cgroup；
- `cgroup.procs`：组中的进程，写入进程号就把它移进来；
- `cgroup.subtree_control`：给子 cgroup 启用哪些控制器（`+cpu +memory +pids +io`）；
- 各控制器的文件：`cpu.max`、`memory.max`、`pids.max`……写入就是设置，读出就是查看。

<figure class="diagram">
<svg viewBox="0 0 700 230" role="img" aria-label="cgroup v2 的目录结构：容器的 cgroup 根目录下，现有进程移到 init 子组，根上启用控制器，实验用的 app 子组带有 cpu.max、memory.max、pids.max 等控制文件">
  <defs><marker id="cgt-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="cgt-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="20" width="220" height="50" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="130.0" y="41.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">/sys/fs/cgroup（容器的根）</text><text x="130.0" y="56.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">subtree_control: cpu memory pids io</text><rect x="300" y="20" width="180" height="40" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="390.0" y="36.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">init/</text><text x="390.0" y="51.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">cgroup.procs：原有进程</text><rect x="300" y="90" width="380" height="120" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="312.0" y="110.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">app/（实验用的子组）</text><line x1="242" y1="40" x2="298" y2="40" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#cgt-a)"/><line x1="242" y1="55" x2="298" y2="120" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#cgt-a)"/><text x="320.0" y="128.0" text-anchor="start" style="fill:var(--fg);font-size:11px">cgroup.procs</text><text x="440.0" y="128.0" text-anchor="start" style="fill:var(--muted);font-size:11px">放进来的进程</text><text x="320.0" y="144.0" text-anchor="start" style="fill:var(--fg);font-size:11px">cpu.max</text><text x="440.0" y="144.0" text-anchor="start" style="fill:var(--muted);font-size:11px">100000 100000 = 1 个 CPU</text><text x="320.0" y="160.0" text-anchor="start" style="fill:var(--fg);font-size:11px">memory.max</text><text x="440.0" y="160.0" text-anchor="start" style="fill:var(--muted);font-size:11px">内存上限（字节）</text><text x="320.0" y="176.0" text-anchor="start" style="fill:var(--fg);font-size:11px">pids.max</text><text x="440.0" y="176.0" text-anchor="start" style="fill:var(--muted);font-size:11px">最多 100 个进程</text><text x="320.0" y="192.0" text-anchor="start" style="fill:var(--fg);font-size:11px">cpu.stat</text><text x="440.0" y="192.0" text-anchor="start" style="fill:var(--muted);font-size:11px">nr_throttled、throttled_usec</text><text x="20.0" y="110.0" text-anchor="start" style="fill:var(--fg);font-size:11px">规则：启用了控制器的 cgroup</text><text x="20.0" y="128.0" text-anchor="start" style="fill:var(--fg);font-size:11px">不能直接放进程（根除外）</text><text x="20.0" y="160.0" text-anchor="start" style="fill:var(--muted);font-size:11px">Docker 的 --cpus、--memory、</text><text x="20.0" y="178.0" text-anchor="start" style="fill:var(--muted);font-size:11px">--pids-limit 写的就是这些文件</text>
  </g>
</svg>
</figure>

v2 有一条规则：**一个 cgroup 如果给子 cgroup 启用了控制器，它自己就不能有进程**（根除外）。所以容器的 cgroup 中要先建一个子 cgroup，把现有的进程都移进去，才能在根上启用控制器：

```bash
mkdir /sys/fs/cgroup/init
for p in $(cat /sys/fs/cgroup/cgroup.procs); do echo $p > /sys/fs/cgroup/init/cgroup.procs; done
echo "+cpu +memory +pids +io" > /sys/fs/cgroup/cgroup.subtree_control
mkdir /sys/fs/cgroup/app     # 实验用的 cgroup，自动带有 cpu.max、memory.max、pids.max 等文件
```

一个进程属于哪个 cgroup，看 `/proc/<pid>/cgroup`：`0::/app`。子进程继承父进程的 cgroup，所以把一个 shell 放进去，它启动的所有程序都在里面：

```bash
sh -c 'echo $$ > /sys/fs/cgroup/app/cgroup.procs; exec ./build/cgcpu 4 5 3'
```

Docker 的 `--cpus`、`--memory`、`--pids-limit` 最终就是写这几个文件；Kubernetes 的 `resources.limits` 也一样。

## 2. CPU 配额：总量够了，延迟却变长了

`cpu.max` 的格式是「配额 周期」，单位微秒：每个周期内，这个 cgroup 中的所有进程加起来最多使用「配额」微秒的 CPU，用完了就**暂停**（throttle），直到下一个周期开始。`100000 100000` 表示每 100 毫秒最多用 100 毫秒，相当于 1 个 CPU；`50000 100000` 是半个 CPU。

`cgcpu` 模拟一个处理请求的服务：每个线程不停地处理请求，每个请求要消耗 5 毫秒的 CPU，统计每个请求从开始到完成的时间：

```c
double t0 = nowSeconds(), c0 = threadCpu();
while (threadCpu() - c0 < reqMs / 1e3)      // 消耗 reqMs 毫秒的 CPU（用线程的 CPU 时钟计时）
    for (int i = 0; i < 1000; i++) x = x * 6364136223846793005UL + 1;
lat[id][cnt[id]++] = (nowSeconds() - t0) * 1e3;
```

4 个线程，运行 3 秒：

| `cpu.max` | 线程 | 每秒请求 | 中位延迟 | p99 延迟 | 最大延迟 | 被限流的周期 |
|---|---|---|---|---|---|---|
| 不限制（2 个 CPU） | 4 | 400 | 10.6 ms | 13.5 ms | 17.6 ms | — |
| `100000 100000`（1 个 CPU） | 4 | 200 | 10.6 ms | **65.0 ms** | 65.5 ms | 30 / 31 |
| `100000 100000`（1 个 CPU） | 1 | 200 | 5.0 ms | 5.0 ms | 8.0 ms | 3 / 30 |
| `10000 10000`（1 个 CPU，周期 10 ms） | 4 | 196 | 20.1 ms | 38.4 ms | 82.0 ms | 294 / 295 |

<figure class="diagram">
<svg viewBox="0 0 700 230" role="img" aria-label="每个请求消耗 5 ms CPU 时的延迟：限制为 1 个 CPU、4 个线程时，中位数不变（10.6 ms），p99 从 13.5 ms 变成 65 ms；同样的配额只用 1 个线程，p99 只有 5 ms">
  <defs><marker id="qta-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="qta-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="200.0" y="46.0" text-anchor="end" style="fill:var(--fg);font-size:12px">不限制，4 线程</text><rect x="210.0" y="24.0" width="81.0" height="16.0" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><rect x="210.0" y="42.0" width="63.6" height="16.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="297.0" y="37.0" text-anchor="start" style="fill:var(--muted);font-size:10px">p99 13.5 ms</text><text x="279.6" y="55.0" text-anchor="start" style="fill:var(--muted);font-size:10px">中位数 10.6 ms</text><text x="200.0" y="90.0" text-anchor="end" style="fill:var(--fg);font-size:12px">1 CPU，4 线程</text><rect x="210.0" y="68.0" width="390.0" height="16.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><rect x="210.0" y="86.0" width="63.6" height="16.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="606.0" y="81.0" text-anchor="start" style="fill:var(--muted);font-size:10px">p99 65.0 ms</text><text x="279.6" y="99.0" text-anchor="start" style="fill:var(--muted);font-size:10px">中位数 10.6 ms</text><text x="200.0" y="134.0" text-anchor="end" style="fill:var(--fg);font-size:12px">1 CPU，1 线程</text><rect x="210.0" y="112.0" width="30.0" height="16.0" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><rect x="210.0" y="130.0" width="30.0" height="16.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="246.0" y="125.0" text-anchor="start" style="fill:var(--muted);font-size:10px">p99 5.0 ms</text><text x="246.0" y="143.0" text-anchor="start" style="fill:var(--muted);font-size:10px">中位数 5.0 ms</text><text x="200.0" y="178.0" text-anchor="end" style="fill:var(--fg);font-size:12px">1 CPU（周期 10 ms），4 线程</text><rect x="210.0" y="156.0" width="230.4" height="16.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><rect x="210.0" y="174.0" width="120.6" height="16.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="446.4" y="169.0" text-anchor="start" style="fill:var(--muted);font-size:10px">p99 38.4 ms</text><text x="336.6" y="187.0" text-anchor="start" style="fill:var(--muted);font-size:10px">中位数 20.1 ms</text><text x="20.0" y="216.0" text-anchor="start" style="fill:var(--muted);font-size:11px">吞吐量：不限制 400 个/秒，三种 1 CPU 的设置都是约 200 个/秒</text>
  </g>
</svg>
</figure>

第一行：2 个 CPU、4 个线程，每个线程只能分到半个 CPU，5 毫秒的计算要 10 毫秒左右才能完成。

第二行：限制为 1 个 CPU，吞吐量正好减半，**中位延迟没变**，p99 却从 13.5 毫秒变成了 65 毫秒。`cpu.stat` 显示 31 个周期中有 30 个被限流，`throttled_usec` 一共 3.0 秒，正好是 30 个周期 × 每个周期暂停 50 毫秒 × 2 个 CPU（这个值按 CPU 累加）。

原因在于配额是按周期计算的。4 个线程在 2 个 CPU 上同时运行，100 毫秒的配额在周期开始后的 **50 毫秒**内就用完了（2 个 CPU × 50 毫秒），接下来的 50 毫秒，所有线程一起被暂停，什么也做不了。

<div class="dsviz" data-viz="lx-cpumax"></div>

一个正在处理中的请求如果碰上暂停，就要多等 50 毫秒：10.6 + 50 ≈ 60 多毫秒，正是 p99 的值。大部分请求在运行的 50 毫秒内完成，中位数不受影响；而每个周期都有请求被暂停打断，所以 p99 和最大值都被拉高了。平均 CPU 使用率看起来「刚好 1 个 CPU、没有超」，监控上什么也看不出来。

第三行：同样的配额，只用 1 个线程。一个线程最多只能用 1 个 CPU，永远不会在周期结束前用完配额，几乎不被限流，每个请求就是 5 毫秒。**吞吐量一样，延迟好了一个数量级。**

第四行：把周期缩短到 10 毫秒。每次暂停只有 5 毫秒，最大延迟本应变小，实测 p99 确实降到了 38 毫秒，但中位数翻倍到了 20 毫秒（5 毫秒的计算几乎每次都会被一次暂停打断），最大值反而是 82 毫秒。短周期让暂停更频繁、更零碎，并不是简单的改善。

**实践中的结论**：

- 容器中的线程池大小、GOMAXPROCS、JVM 的 GC 线程数，要按**配额**而不是宿主机的核数来设。JVM、Go 的新版本会读取 cgroup 的配额自动调整，老版本看到的是宿主机的 64 个核；
- 关心延迟的服务，用 `cpu.stat` 的 `nr_throttled` 监控限流，而不只看 CPU 使用率；
- 可以考虑用 `cpuset.cpus` 把容器绑定在几个固定的核上，代替配额；或者用 `cpu.max.burst` 允许偶尔超出一点；Kubernetes 中也有人选择只设 CPU 的 requests、不设 limits；
- 和配额不同，`cpu.weight` 是按比例分配：CPU 空闲时可以用满，只在竞争时按权重分。

## 3. 内存

`memory.max` 是内存的硬上限，超过以后先回收页缓存，回收不了就在这个 cgroup 内触发 OOM killer。《内存不足：overcommit 与 OOM killer》中已经实测过：200 MB 的容器中占用到 180 MB 左右被杀，退出码 137；读 1 GB 的文件不会触发 OOM，因为页缓存可以回收。第 6 篇的迷你容器中也会再看一次。

还有一个 `memory.high`：超过时不杀进程，而是让分配内存的进程变慢、强制回收，给程序一个「软」的上限。`memory.events` 中的 `oom`、`oom_kill`、`high` 计数说明发生过什么。

## 4. pids：挡住 fork 炸弹

一个不断 fork 的程序（fork 炸弹）可以占满整台机器的进程号，所有程序都无法再创建进程。`pids.max` 限制一个 cgroup 中的进程（线程也算）总数。`forkbomb` 不停地 fork，每个子进程都暂停不退出：

```bash
echo 100 > /sys/fs/cgroup/app/pids.max
sh -c 'echo $$ > /sys/fs/cgroup/app/cgroup.procs; ./build/forkbomb 10000; cat /sys/fs/cgroup/app/pids.events'
```

```
第 99 次 fork 失败：Resource temporarily unavailable
成功创建了 98 个子进程
max 1
```

98 个子进程，加上 `forkbomb` 自己和外面的 `sh`，正好 100 个。`fork` 返回 `EAGAIN`；`pids.events` 中 `max 1` 记录了达到上限的事件，宿主机的 `dmesg` 中有：

```
cgroup: fork rejected by pids controller in /docker/e66c8d76.../app
```

在 cgroup 外面运行同一个程序，300 个子进程全部创建成功。Docker 默认**不**限制进程数，`--pids-limit` 才会设置它；Kubernetes 有节点级别的 `podPidsLimit`。

## 5. 常见错误

1. **只看平均 CPU 使用率**。配额下的限流会显著增加尾延迟，平均值看不出来，要看 `nr_throttled`、`throttled_usec`。
2. **线程数按宿主机的核数设置**。容器配额 2 个 CPU，却开了 64 个线程，每个周期开头就用光配额。
3. **在启用了控制器的 cgroup 中直接放进程**。cgroup v2 不允许（根 cgroup 除外），写 `cgroup.procs` 会失败。
4. **不限制进程数**。一个容器中的 fork 炸弹或线程泄漏会影响整台机器。
5. **以为 `memory.max` 只算程序 `malloc` 的内存**。页缓存、内核为这个 cgroup 分配的内存（套接字缓冲区等）也算在内。

## 6. 小结

- cgroup v2 是一个文件系统：目录是 cgroup，`cgroup.procs` 放进程，`cpu.max`、`memory.max`、`pids.max` 设置限制；
- CPU 配额按周期计算，用完就暂停到下一个周期；4 个线程、1 个 CPU 的配额：吞吐量减半，中位延迟不变，p99 从 13.5 ms 变成 65 ms；
- 同样的配额只用 1 个线程，p99 只有 5 ms：线程数要和配额匹配；
- `pids.max` = 100 时，第 99 次 fork 失败，挡住了 fork 炸弹；
- 内存的限制和 OOM 见《内存不足：overcommit 与 OOM killer》。

## 7. 练习

1. 一个 Go 服务部署在 CPU 配额为 2 个 CPU 的容器中，宿主机有 32 个核，Go 的版本较老，不会读取 cgroup 配额。可能出现什么问题？怎么解决？
2. `cpu.max` 设为 `200000 100000` 是什么意思？在一台 2 核的机器上，和不限制有区别吗？
3. 为什么说 `pids.max` 也能防止线程泄漏？

<details>
<summary>点击查看答案</summary>

1. Go 运行时按宿主机的核数把 GOMAXPROCS 设为 32，最多 32 个线程同时运行，配额 200 毫秒（每 100 毫秒）在周期开始后约 6 毫秒就用光了，剩下的 94 毫秒全部暂停，尾延迟非常高，GC 也会受影响。解决：把 GOMAXPROCS 设为 2（环境变量，或者使用 `go.uber.org/automaxprocs`），新版本的 Go 会自动读取 cgroup 的配额。

2. 每 100 毫秒最多使用 200 毫秒的 CPU，相当于 2 个 CPU。在 2 核的机器上，这个 cgroup 最多也只能用到 2 个 CPU，配额永远不会在周期结束前用完，和不限制几乎没有区别；在核数更多的机器上它才起作用。

3. `pids.max` 限制的是 cgroup 中的任务数，Linux 中每个线程都是一个任务，也算在内。一个不断创建线程而不结束的程序，达到上限后 `pthread_create` 会返回 `EAGAIN`，不会无限地消耗系统的进程号和内存。

</details>
