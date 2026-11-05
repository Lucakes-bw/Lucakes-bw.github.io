---
title: off-CPU 分析：线程在等什么
date: 2026-11-10 10:40:00
updated: 2026-11-10 10:40:00
categories:
  - Linux基础
  - 性能分析
tags:
  - Linux
  - 性能
  - bpftrace
order: 5
description: 一个模拟的服务每个请求要解析、持锁查表、读磁盘。perf 的报告说时间都花在计算上，解析和查表各占一半；可是把解析优化快一倍，吞吐量一点没变。因为 4 个线程在 1.9 秒中只用了 0.3 秒 CPU，其余时间都不在 CPU 上，perf 采样根本看不到。本文用 bpftrace 在线程切换时记录等待的时间和调用栈，画出 off-CPU 火焰图：96.8% 的时间在等磁盘。去掉磁盘读以后再看，91.5% 的等待变成了等锁，去掉锁，吞吐量又涨了 47%。
top_group_list: true
disableNunjucks: true
viz: [lx-perf]
---

> **本文目标**：理解为什么 on-CPU 采样看不到等待的时间；理解 off-CPU 分析的原理：在线程让出 CPU 和重新运行时记录时间和调用栈；会用 bpftrace 统计 off-CPU 时间并画成火焰图；能把 on-CPU 和 off-CPU 的结果放在一起，找出真正的瓶颈；知道在容器中使用 bpftrace 要注意进程号命名空间。
>
> **前置知识**：本章第 1、3 篇；本专栏的《线程同步》（互斥锁）；《页缓存与写回》（`O_DIRECT`）。bpftrace 的语法在第 6 篇系统介绍，本文用到的部分会在用到时解释。
>
> **实验环境**：同第 1 篇。数据盘限速 100 MB/s，读的文件是 1 GB。

## 1. 一个「计算很多」的服务

`offcpu.c` 模拟一个服务，4 个线程处理请求，每个请求做三件事：

```c
__attribute__((noinline)) void parse(uint64_t id) { sink = burn(id, parseIters); }  // 解析请求，约 0.1 ms 的计算

__attribute__((noinline)) void lookup(uint64_t id)        // 持有全局锁，在共享的表中查找，约 0.1 ms
{
    pthread_mutex_lock(&tableLock);
    sink = burn(id, 60000);
    pthread_mutex_unlock(&tableLock);
}

__attribute__((noinline)) void load(uint64_t id, void* buf)  // 从磁盘读 256 KB（O_DIRECT）
{
    off_t off = ...;   // 由 id 算出一个位置
    CHECK(pread(fd, buf, READ_SIZE, off) == READ_SIZE);
}

for (long i = 0; i < perThread; i++) {     // 每个线程
    parse(id);
    lookup(id);
    load(id, buf);
}
```

4 个线程处理 800 个请求：

```
4 个线程处理 800 个请求：1.98 秒，每秒 403 个；CPU 用户态 0.26 秒、内核态 0.03 秒；主动切换 1113 次，被抢占 47 次
```

用 perf 采样（第 3 篇的方法）：

```
$ perf record -F 999 -g ./build/offcpu /data/big.bin 4 800
[ perf record: Captured and wrote 0.043 MB /tmp/on.data (292 samples) ]
$ perf report --no-children --stdio
    84.59%  offcpu   offcpu             [.] burn
            |
            ---burn
               |--44.18%--lookup
               |          worker
               |          ...
                --40.41%--parse
                          worker
                          ...
```

时间几乎都在 `burn` 上，查表和解析各占一半。看起来该优化计算。试试把解析的计算量减半（选项 `f`）：

```
4 个线程处理 800 个请求：1.99 秒，每秒 402 个；CPU 用户态 0.21 秒、内核态 0.04 秒；主动切换 1419 次，被抢占 10 次
```

CPU 时间少了，吞吐量**一点没变**。

perf 的报告没有错，只是不完整。看采样数：292 个，999 Hz，对应约 0.29 秒的 CPU 时间，正好是 user + sys。而 4 个线程运行了 1.98 秒，一共 7.9 秒，**perf 只看到了其中的 4%**。剩下 96% 的时间，线程不在 CPU 上，定时器中断打断的是别的进程（或者空闲的 CPU），这些时间不会出现在采样中。

## 2. off-CPU 分析的原理

线程不在 CPU 上，一定是在某个时刻让出了 CPU：调用 `pread` 等磁盘、`pthread_mutex_lock` 拿不到锁进入 futex 等待、时间片用完被抢占。每次让出 CPU，内核都要执行调度函数，切换到别的线程。所以只要在两个时刻插桩：

1. **线程被换下 CPU 时**：记下时刻；
2. **线程重新回到 CPU 时**：用现在的时刻减去记下的时刻，就是它这一次等了多久；此时它的调用栈正是它被换下时的调用栈（它从哪里睡下去，就从哪里醒过来），记下这个调用栈。

按调用栈累计等待的时间，就知道「在哪里、等了多久」。

<div class="dsviz" data-viz="lx-offcpu"></div>

这需要在内核的调度路径上运行一小段代码，正是 eBPF 擅长的：内核提供了 `sched:sched_switch` 跟踪点（每次切换时触发，此时还在被换下的线程的上下文中），线程回到 CPU 后会执行内核函数 `finish_task_switch`（此时已经是新线程的上下文）。用 bpftrace 写出来：

```c
// offcpu.bt：off-CPU 时间，按调用栈汇总（单位微秒）
// 用法：bpftrace offcpu.bt 程序名 秒数
tracepoint:sched:sched_switch
/comm == str($1)/                      // 被换下的是我们的程序
{
    @start[tid] = nsecs;               // 记下这个线程被换下的时刻
}

kprobe:finish_task_switch.isra.0
/@start[tid]/                          // 回到 CPU 的线程，之前被记录过
{
    @us[ustack, kstack] = sum((nsecs - @start[tid]) / 1000);   // 按用户态 + 内核态的调用栈累计
    delete(@start[tid]);
}

interval:s:$2
{
    clear(@start);
    exit();                            // 指定的秒数后退出，打印 @us
}
```

几点说明：

- `@start`、`@us` 是 bpftrace 的 map（内核中的哈希表），`tid` 是当前线程的编号，`nsecs` 是当前时刻（纳秒）；
- `ustack`、`kstack` 是当前线程的用户态、内核态调用栈，可以直接作为 map 的键；
- 内核函数 `finish_task_switch` 在编译时被 GCC 改了名字（`.isra.0` 是一种过程间优化的后缀），先用 `bpftrace -l 'kprobe:finish_task_switch*'` 查出准确的名字；
- 要在程序**还在运行时**打印结果，bpftrace 才能读 `/proc/<pid>/maps` 把用户态的地址解析成函数名，所以用 `interval` 控制时间，而不是等程序结束。

BCC 工具集中有现成的 `offcputime`（Ubuntu 上叫 `offcputime-bpfcc`），原理相同，选项更多。这里自己写一遍，是为了看清它做了什么。

## 3. off-CPU 火焰图

运行 2400 个请求（约 6 秒），bpftrace 记录其中 5 秒：

```bash
(sleep 1; ./build/offcpu /data/big.bin 4 2400) &
bpftrace offcpu.bt offcpu 5 > off.txt
```

输出是一个个调用栈和累计的微秒数。用一个小脚本（`fold.py`，见代码目录）把它们折叠成火焰图脚本需要的格式（一行一个调用栈，从外到内用分号连接），再用 FlameGraph 生成火焰图。按用户态的函数汇总：

| 在哪里等 | 时间 | 比例 |
|---|---|---|
| `load` → `pread`：等磁盘 | 14970 ms | 96.8% |
| `lookup` → `pthread_mutex_lock`：等锁 | 424 ms | 2.7% |
| `burn`：被抢占 | 71 ms | 0.5% |

一共 15.5 秒（4 个线程 × 5 秒中的大部分）。等磁盘那一支的内核调用栈：

```
__arm64_sys_pread64 → ksys_pread64 → vfs_read → ext4_file_read_iter
  → iomap_dio_rw → __iomap_dio_rw → blk_io_schedule → io_schedule_timeout → schedule
```

`iomap_dio_rw` 是 ext4 的直接 IO（`O_DIRECT`），`blk_io_schedule` 是在等块设备完成请求。把 on-CPU 和 off-CPU 的火焰图放在一起：

<figure>
<a href="/img/lx-perf/on.svg"><img src="/img/lx-perf/on.svg" alt="on-CPU 火焰图：最宽的是 worker，上面分成 parse 和 lookup 两块，顶上都是 burn，看起来全是计算" width="1000"></a>
<figcaption>on-CPU 火焰图：941 个采样，约 0.9 秒的 CPU 时间（点击打开交互版本）</figcaption>
</figure>

<figure>
<a href="/img/lx-perf/off.svg"><img src="/img/lx-perf/off.svg" alt="off-CPU 火焰图：几乎整个宽度都是 load、pread 和下面一长串内核函数，直到 io_schedule；lookup 和 pthread_mutex_lock 只是很窄的一条" width="1000"></a>
<figcaption>off-CPU 火焰图：同样的程序，15.5 秒的等待时间（点击打开交互版本）</figcaption>
</figure>

两张图的「宽度」单位不同：on-CPU 图的总宽度是不到 1 秒的 CPU 时间，off-CPU 图是 15.5 秒的等待时间。**真正的瓶颈是磁盘**：800 个请求要读 200 MB，磁盘 100 MB/s，至少需要 2 秒，和实测的 1.98 秒对得上。优化计算没有用，要么减少读的数据量，要么缓存热点数据，要么换更快的盘。

## 4. 去掉磁盘以后：锁

假设热点数据都缓存到了内存中，不再读磁盘（选项 `n`）。40000 个请求：

```
4 个线程处理 40000 个请求：3.02 秒，每秒 13265 个；CPU 用户态 4.14 秒、内核态 0.05 秒；主动切换 27967 次，被抢占 4560 次
```

吞吐量从 403 涨到了 13265。现在 CPU 用了 4.14 秒，2 个核 3.02 秒最多 6.04 秒，还有余量；主动切换将近 28000 次，说明线程还在频繁地等什么。on-CPU 的报告：

```
    98.82%  offcpu   offcpu             [.] burn
               |--49.63%--parse
                --49.19%--lookup
```

和之前一样，一半一半，看不出锁有什么问题：on-CPU 采样只知道 `lookup` 在计算，不知道其他线程在门外排队。off-CPU：

| 在哪里等 | 时间 | 比例 |
|---|---|---|
| `lookup` → `pthread_mutex_lock`：等锁 | 6302 ms | 91.5% |
| `pthread_mutex_unlock` → `__lll_lock_wake`：解锁时唤醒别的线程 | 496 ms | 7.2% |
| `burn`：被抢占 | 90 ms | 1.3% |

<figure>
<a href="/img/lx-perf/off2.svg"><img src="/img/lx-perf/off2.svg" alt="去掉磁盘读以后的 off-CPU 火焰图：最宽的一块是 lookup、pthread_mutex_lock、__lll_lock_wait 和 futex 等待" width="1000"></a>
<figcaption>去掉磁盘读以后的 off-CPU 火焰图（点击打开交互版本）</figcaption>
</figure>

瓶颈变成了锁。`lookup` 中 0.1 毫秒的计算是在持锁时做的，4 个线程要排队，同一时刻只有一个能执行。如果查表其实不需要互斥（比如表是只读的，或者可以分片），去掉锁（选项 `nl`）：

```
4 个线程处理 40000 个请求：2.05 秒，每秒 19478 个；CPU 用户态 4.10 秒、内核态 0.00 秒；主动切换 3 次，被抢占 2108 次
```

又快了 47%，主动切换从 27967 次降到 3 次。现在 CPU 用了 4.10 秒，2 个核 2.05 秒，正好用满，这时瓶颈才是计算，on-CPU 的报告才说了实话，再去优化 `parse` 才有效果。

| 版本 | 每秒请求 | 瓶颈 | 怎样看出来 |
|---|---|---|---|
| 原始 | 403 | 磁盘 | off-CPU：96.8% 在 `pread` |
| 解析快一倍 | 402 | 磁盘 | 没变，优化错了地方 |
| 不读磁盘 | 13265 | 锁 | off-CPU：91.5% 在 `pthread_mutex_lock` |
| 不读磁盘、不加锁 | 19478 | CPU | CPU 用满，on-CPU 的报告有效 |

## 5. 在容器中使用 bpftrace

这个实验中踩了两个坑：

1. **跟踪点不存在**：`ERROR: tracepoint not found: syscalls:sys_enter_openat`。容器中没有挂载 tracefs，执行 `mount -t tracefs nodev /sys/kernel/tracing` 后就好了；
2. **用户态的函数名都是地址**，而且按 `pid == 进程号` 过滤时什么也抓不到。eBPF 程序在内核中运行，看到的是宿主（虚拟机）的进程号；容器有自己的进程号命名空间，`$!`、`ps` 看到的是另一套号码，`/proc` 中也找不到内核报告的那个进程，bpftrace 就无法解析符号。解决办法是用 `--pid=host` 运行容器，让容器和宿主共用进程号；或者像本文的脚本那样按程序名（`comm`）过滤。

还要 `--privileged`（或者 `CAP_BPF`、`CAP_PERFMON` 等权限），以及内核提供 BTF（`/sys/kernel/btf/vmlinux`，Ubuntu 的内核都有）。

## 6. 常见错误

1. **只看 on-CPU 的报告就开始优化**。先用第 1 篇的方法比较 CPU 时间和墙上时间，CPU 时间远小于墙上时间时，on-CPU 的报告只覆盖了很小一部分。
2. **在 on-CPU 的报告中找锁的问题**。持锁的线程在计算，排队的线程不在 CPU 上，采样只能看到前者。
3. **把 off-CPU 时间加起来和墙上时间比较**。它是所有线程的等待时间之和，4 个线程 5 秒，最多可以有 20 秒。
4. **off-CPU 的结果中包括了本来就该等的时间**。比如线程池中的空闲线程在等任务，网络服务器在 `epoll_wait` 中等连接，它们的等待时间可能最长，却不是问题。要关注的是处理请求的路径上的等待。
5. **在容器中直接用 bpftrace，发现符号解析不出来**。注意进程号命名空间。

## 7. 小结

- on-CPU 采样只能看到线程在 CPU 上的时间；本文的服务 96% 的时间不在 CPU 上，perf 的报告误导人去优化计算；
- off-CPU 分析在线程被换下（`sched_switch`）和重新运行（`finish_task_switch`）时记录时刻，按调用栈累计等待的时间；
- 实测：等磁盘 96.8%；去掉磁盘读以后，等锁 91.5%；去掉锁，吞吐量从 13265 涨到 19478，CPU 才成为瓶颈；
- 瓶颈会转移：解决了一个，下一个才会显现，每一步都要重新测量；
- 在容器中使用 bpftrace，要挂载 tracefs，注意进程号命名空间（`--pid=host`）。

## 8. 练习

1. 一个程序的 on-CPU 火焰图中，`pthread_mutex_lock` 占了 30%。这说明了什么？和本文 off-CPU 中看到的 `pthread_mutex_lock` 有什么不同？
2. 本文的 off-CPU 脚本对所有让出 CPU 的情况都记录了时间，包括被抢占。怎样只记录主动让出 CPU（等 IO、等锁、睡眠）的情况？（提示：`sched_switch` 跟踪点的参数中有 `prev_state`）
3. 原始版本中，把线程数从 4 加到 16，吞吐量会变成多少？为什么？

<details>
<summary>点击查看答案</summary>

1. on-CPU 中出现 `pthread_mutex_lock` 说明线程在 CPU 上执行加锁的代码：比如锁竞争非常激烈，大量的时间花在原子操作和 futex 系统调用本身上；或者用的是自适应锁（`PTHREAD_MUTEX_ADAPTIVE_NP`），拿不到锁时先自旋一会儿再睡眠。这是「抢锁」消耗的 CPU。off-CPU 中的 `pthread_mutex_lock` 是拿不到锁、睡眠等待的时间，不消耗 CPU。两者都说明锁竞争严重，但 off-CPU 的那部分通常大得多，而且只有 off-CPU 分析能看到。

2. 在 `sched_switch` 中判断 `args.prev_state`：被抢占时线程仍然是可运行的状态（`TASK_RUNNING`，值为 0）；主动等待时是 `TASK_INTERRUPTIBLE`（1）或 `TASK_UNINTERRUPTIBLE`（2）等。把过滤条件改成 `/comm == str($1) && args.prev_state != 0/` 即可。反过来，只记录 `prev_state == 0` 的情况，统计的就是排队等 CPU 的时间（第 1 篇中 `%wait` 的那部分）。

3. 基本不变。实测 16 个线程处理 800 个请求，每秒 417 个。瓶颈是磁盘的带宽：每个请求读 256 KB，100 MB/s 的磁盘每秒最多支持 400 个请求，再多的线程也只是在磁盘的队列中多排一会儿。off-CPU 中 `pread` 的等待时间会随线程数成比例增加。

</details>
