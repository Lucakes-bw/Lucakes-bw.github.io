---
title: 内存不足：overcommit 与 OOM killer
date: 2026-11-09 10:40:00
updated: 2026-11-09 10:40:00
categories:
  - Linux基础
  - 内存管理
tags:
  - Linux
  - 内存
  - 容器
order: 5
description: 在只有 2 GB 内存的机器上，malloc(2 GB) 失败了，但每次 1 GB 却连续成功了 1000 次，地址空间涨到 1000 GB：这是 overcommit。真正用完内存时，内核的 OOM killer 选一个进程杀掉。本文在限制了 200 MB 内存的容器中复现 OOM，从内核日志中读懂它的记录；用 oom_score_adj 让一个只占 60 MB 的进程先被杀；读 1 GB 的文件却不会触发 OOM，因为页缓存可以回收。还复现了一个陷阱：编译器把 malloc 加 memset 整个优化掉了。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 overcommit 的三种模式，知道为什么 malloc 很少返回 NULL；理解 OOM killer 怎样选择进程，会读内核日志中的 OOM 记录；会用 `oom_score_adj` 保护重要的进程；理解容器（cgroup）的内存限制和 OOM，知道页缓存在其中的作用。
>
> **前置知识**：本章前几篇。
>
> **实验环境**：同第 1 篇。修改 `vm.overcommit_memory` 会影响整台虚拟机，实验结束后立即恢复了默认值。容器的内存限制用 `docker run --memory=200m --memory-swap=200m`（不允许使用交换分区）。

## 1. overcommit：先答应，用到再说

### 1.1 三种模式

第 1 篇看到，`malloc`/`mmap` 只分配地址，写的时候才分配物理页。那内核答应一次 `malloc` 的时候，要不要检查内存够不够？由 `vm.overcommit_memory` 决定：

| 值 | 模式 | 规则 |
|---|---|---|
| 0（默认） | 启发式 | 单次申请明显超过「内存 + 交换分区」的才拒绝，其他都答应 |
| 1 | 总是答应 | 从不拒绝 |
| 2 | 严格 | 所有进程已经答应出去的内存（`Committed_AS`）不能超过 `CommitLimit` = 交换分区 + 内存 × `overcommit_ratio`% |

### 1.2 实测

各种大小的 `malloc`（只申请，不写入），以及每次 1 GB、不释放地一直申请：

```c
long sizes[] = {1, 2, 4, 64};
for (int i = 0; i < 4; ++i) {
    void* p = malloc(sizes[i] * GB);
    printf("  malloc(%2ld GB)：%s\n", sizes[i], p ? "成功" : "失败");
    free(p);
}
int n = 0;
while (n < 1000 && malloc(1 * GB)) ++n;
```

```
--- overcommit_memory=0（默认）
  CommitLimit 979 MB，Committed_AS 716 MB，MemTotal 1959 MB
  malloc( 1 GB)：成功
  malloc( 2 GB)：失败
  malloc( 4 GB)：失败
  malloc(64 GB)：失败
  每次 1 GB、不写入、不释放：成功了 1000 次（到上限 1000 次停止），VSZ 1000 GB
--- overcommit_memory=1
  malloc( 1 GB)：成功
  malloc( 2 GB)：成功
  malloc( 4 GB)：成功
  malloc(64 GB)：成功
  每次 1 GB、不写入、不释放：成功了 1000 次（到上限 1000 次停止），VSZ 1000 GB
--- overcommit_memory=2，overcommit_ratio=100
  CommitLimit 1959 MB，Committed_AS 716 MB，MemTotal 1959 MB
  malloc( 1 GB)：成功
  malloc( 2 GB)：失败
  malloc( 4 GB)：失败
  malloc(64 GB)：失败
  每次 1 GB、不写入、不释放：成功了 1 次，VSZ 1 GB
```

- **默认模式**：一次申请 2 GB（超过了 1959 MB 的内存，又没有交换分区）被拒绝；但**每次 1 GB，连续成功了 1000 次**，这个进程的地址空间有 1000 GB，是物理内存的 500 倍。启发式只检查单次申请是否离谱，不管总量；
- **模式 1**：连 64 GB 都答应了；
- **模式 2**：`CommitLimit` 是 1959 MB，已经答应出去了 716 MB，只剩约 1.2 GB 的额度，第二个 1 GB 就失败了。

（模式 2 的默认 `overcommit_ratio` 是 50，`CommitLimit` 只有 979 MB，比已经答应出去的多不了多少，整台虚拟机上的程序都可能开始分配失败，所以实验中改成了 100，并且立即恢复。）

### 1.3 为什么默认要 overcommit

很多程序申请的内存远比实际用的多：线程栈默认 8 MB（实际只用几十 KB）、预留的内存池、fork 以后很快 exec 的子进程（按严格的算法，fork 一个 1 GB 的进程就要再答应 1 GB）……严格模式下，这些都会让机器在内存还很充裕的时候就拒绝分配。默认的启发式模式是一种乐观的折中。

代价是：**malloc 成功了，不代表内存够用**。真正用完内存的时刻是在**写入**时（缺页时找不到可用的物理页），而这时已经没有办法让 malloc 返回 NULL 了，内核只能采取更激烈的手段。

## 2. OOM killer

### 2.1 内存不足时内核做什么

缺页时没有空闲的物理页，内核先尝试**回收**：丢弃干净的页缓存、把脏页写回后丢弃、把匿名页换出到交换分区（如果有）。回收以后还是不够，就调用 **OOM killer**（Out Of Memory killer）：选一个进程，发送 `SIGKILL`，释放它的内存。

它怎么选？每个进程有一个 `oom_score`（`/proc/<pid>/oom_score`），基本上就是**这个进程占用的内存**（RSS、交换分区中的页、页表）占可用内存的比例，乘以 1000，再加上 `oom_score_adj`（-1000～1000）。分数最高的被杀。也就是说，**默认杀占内存最多的进程**，因为杀掉它释放的内存最多。

### 2.2 实测：容器中的 OOM

在内存限制为 200 MB 的容器中，每次申请并写满 20 MB：

```c
for (;;) {
    char* p = malloc(step * MB);
    if (!p) { /* malloc 返回 NULL */ }
    for (long i = 0; i < step * MB; i += 4096) ((volatile char*)p)[i] = 1;   // 写入，真正占用物理内存
    total += step;
    printf("已占用 %ld MB\n", total);
}
```

```
$ docker run --memory=200m --memory-swap=200m ... oom eat 20
已占用 140 MB
已占用 160 MB
已占用 180 MB
$ docker inspect -f '{{.State.ExitCode}} OOMKilled={{.State.OOMKilled}}' oomt
137 OOMKilled=true
```

**malloc 一次都没有返回 NULL**，进程在写下一个 20 MB 时直接被杀了。退出码 137 = 128 + 9，表示被信号 9（`SIGKILL`）杀死。进程没有任何机会处理这个情况：`SIGKILL` 不能被捕获，也来不及记录日志。

内核日志（`dmesg`）中的记录：

```
oom_kill_process+0x1f8/0x3e0
[  pid  ]   uid  tgid total_vm      rss rss_anon rss_file rss_shmem pgtables_bytes swapents oom_score_adj name
[ 433057]     0 433057    51774    51328    51008      320         0   454656        0             0 oom
oom-kill:constraint=CONSTRAINT_MEMCG,...,oom_memcg=/docker/7c0e2421db46...,task=oom,pid=433057,uid=0
Memory cgroup out of memory: Killed process 433057 (oom) total-vm:207096kB, anon-rss:204032kB, file-rss:1280kB, shmem-rss:0kB, UID:0 pgtables:444kB oom_score_adj:0
```

- 进程表列出了候选进程的内存（单位是页）：`rss` 51328 页 ≈ 200 MB；
- `constraint=CONSTRAINT_MEMCG`：是一个 **cgroup**（这里是 Docker 容器）的内存用完了，不是整台机器。cgroup 的 OOM 只在这个 cgroup 内部选择进程，不会杀掉容器外面的进程；
- 最后一行是结论：杀了哪个进程，它占了多少内存。

这台虚拟机的内核日志中还留着几天前的另一条记录：

```
Out of memory: Killed process 368372 (mysqld) ... oom_score_adj:0
```

没有 `Memory cgroup` 的前缀：那是整台虚拟机的内存用完了（全局 OOM），被杀的是占内存最多的 mysqld。

### 2.3 oom_score_adj：指定先杀谁

在 300 MB 的容器中启动三个进程：A 占 120 MB；B 只占 60 MB，但 `oom_score_adj` 设为 1000（最先被杀）；C 不断占用内存：

```bash
$O hold 120 & A=$!
(echo 1000 > /proc/self/oom_score_adj; exec $O hold 60) & B=$!
$O eat 20
```

```
A（占 120 MB，oom_score_adj 0）oom_score 708
B（占  60 MB，oom_score_adj 1000）oom_score 1353
C 开始不断占用内存：
已占用 140 MB
已占用 160 MB
A 还活着
B 被杀了
```

内核日志：

```
Memory cgroup out of memory: Killed process 433463 (oom) anon-rss:61440kB, oom_score_adj:1000
Memory cgroup out of memory: Killed process 433469 (oom) anon-rss:182656kB, oom_score_adj:0
```

<figure class="diagram">
<svg viewBox="0 0 700 200" role="img" aria-label="OOM killer 的选择：300 MB 的容器中，A 占 120 MB，B 只占 60 MB 但 oom_score_adj 为 1000，C 不断占用内存。第一次 OOM 杀了分数最高的 B；B 的内存被 C 用完后，第二次 OOM 杀了最大的 C；A 活了下来">
  <defs><marker id="osl-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="osl-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="30" width="205" height="50" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="122.5" y="51.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">A：120 MB</text><text x="122.5" y="66.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">oom_score 708</text><rect x="20" y="100" width="205" height="34" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="122.5" y="121.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">活着</text><line x1="122" y1="80" x2="122" y2="98" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#osl-a)"/><rect x="245" y="30" width="205" height="50" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="347.5" y="51.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">B：60 MB，adj 1000</text><text x="347.5" y="66.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">oom_score 1353</text><rect x="245" y="100" width="205" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="347.5" y="121.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">第一次 OOM 被杀</text><line x1="347" y1="80" x2="347" y2="98" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#osl-a)"/><rect x="470" y="30" width="205" height="50" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="572.5" y="51.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">C：不断占用</text><text x="572.5" y="66.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">到 182 MB</text><rect x="470" y="100" width="205" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="572.5" y="121.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">第二次 OOM 被杀</text><line x1="572" y1="80" x2="572" y2="98" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#osl-a)"/><text x="20.0" y="170.0" text-anchor="start" style="fill:var(--fg);font-size:12px">oom_score ≈ 占用的内存比例 × 1000 + oom_score_adj；分数最高的被杀</text><text x="20.0" y="190.0" text-anchor="start" style="fill:var(--muted);font-size:11px">oom_score_adj = -1000：永远不杀；1000：最先被杀</text>
  </g>
</svg>
</figure>

- 第一次 OOM：**只占 60 MB 的 B 被杀了**，因为它的分数加了 1000；
- B 释放的 60 MB 很快又被 C 用完，第二次 OOM：C 占了 182 MB，是剩下的进程中最大的，被杀；
- A 一直活着。

`oom_score_adj = -1000` 表示**永远不杀**。sshd、数据库这类关键进程常常设置一个负值（systemd 的 `OOMScoreAdjust=`），可以丢弃的工作进程设置正值。但如果所有进程都设成了 -1000，内存用完时就没有可以杀的进程了，系统会更糟。

## 3. 页缓存不会导致 OOM

容器的内存限制也包括它产生的页缓存。在 200 MB 的容器中读一个 1 GB 的文件（读之前清空页缓存）：

```
memory.max 200 MB，memory.peak 200 MB
anon 0 MB
file 196 MB
oom 0
oom_kill 0
```

容器的内存用到了 200 MB 的上限，其中 196 MB 是页缓存（`file`），但**没有发生 OOM**：页缓存是可以回收的，到了上限，内核回收这个容器最早读的那部分文件页，给新读的数据腾地方。

所以看容器的内存，不能只看「用了多少」：`memory.current` 接近上限可能只是页缓存多，没有任何问题；匿名内存（`anon`）接近上限才危险。`docker stats` 显示的内存已经减去了一部分不活跃的页缓存。

## 4. 一个陷阱：被优化掉的 malloc

实验中的程序最初是这样占用内存的：

```c
char* p = malloc(mb * MB);
memset(p, 1, mb * MB);
pause();
```

结果 A、B 根本没有占到内存，C 一个进程就用到了 305 MB 才被杀。用一个最简单的程序验证：

```c
int main(void) { char* p = malloc(100 << 20); memset(p, 1, 100 << 20); pause(); }
```

```
$ gcc -O2 -o t t.c && objdump -d t | grep -A12 "<main>:" | grep bl
 648:	97ffffee 	bl	600 <pause@plt>
VmRSS:	     880 kB
```

**`main` 中只剩下了对 `pause` 的调用**，`malloc` 和 `memset` 都不见了，RSS 只有 880 KB。编译器知道 `malloc` 和 `memset` 的语义：这块内存写完以后再也没有被读过，写它没有任何可观察的效果，于是整个删掉了。

测量内存的程序要让写入「有意义」：通过 `volatile` 指针写（本文的做法），或者在之后读出数据、打印一个结果。

## 5. 常见错误

1. **检查 malloc 的返回值，以为这样就能处理内存不足**。默认的 overcommit 下，内存真正用完时 malloc 不会返回 NULL，进程会被直接杀掉。
2. **容器的内存限制设得太紧**，只看了平时的用量，没考虑峰值（比如 Redis 的 fork、Java 的堆加堆外内存），进程被反复 OOM 重启。
3. **看到 `memory.current` 接近上限就扩容**，其实大部分是页缓存。看 `memory.stat` 中的 `anon`。
4. **进程莫名其妙地消失，退出码 137，却没有查内核日志**。`dmesg`、`journalctl -k` 中有 OOM 的记录。
5. **关键进程没有设置 `oom_score_adj`**，或者反过来，给所有进程都设了 -1000。
6. **在生产环境用模式 2 却不调整 `overcommit_ratio`**，默认的 50% 加上没有交换分区，机器只能答应出一半的内存，很多程序启动就失败。

## 6. 小结

- 默认的 overcommit 是启发式的：单次申请超过内存加交换分区才拒绝，每次 1 GB 却能连续申请 1000 次；模式 1 从不拒绝，模式 2 按 `CommitLimit` 严格检查；
- 内存真正用完发生在写入时，内核先回收页缓存等，不够就由 OOM killer 杀掉分数最高（通常是占内存最多）的进程；
- 实测 200 MB 的容器：malloc 从没返回 NULL，进程直接被 `SIGKILL`（退出码 137），内核日志中有 `Memory cgroup out of memory` 的记录；
- `oom_score_adj` 调整选择：设为 1000 的 60 MB 进程先被杀；页缓存可以回收，读 1 GB 的文件不会导致 OOM；
- 编译器可能把「malloc + memset、之后不再使用」整个优化掉。

## 7. 练习

1. 一个 Java 服务部署在内存限制为 4 GB 的容器中，堆设为 `-Xmx4g`，运行一段时间后被 OOM kill。为什么？
2. 为什么 OOM killer 杀的是占内存最多的进程，而不是最后申请内存的那个进程？
3. Redis 的文档建议设置 `vm.overcommit_memory = 1`。结合本章的内容，说明原因。

<details>
<summary>点击查看答案</summary>

1. 进程的内存不只是 Java 堆：还有元空间、线程栈、JIT 编译的代码、直接内存（NIO 的缓冲区）、GC 的数据结构、C 库的 malloc……堆用满 4 GB 时，进程总的内存已经超过了容器的 4 GB 限制，cgroup 的 OOM 把它杀掉了。堆的上限应该明显小于容器的限制（比如 70%～75%），或者使用 `-XX:MaxRAMPercentage`。

2. OOM killer 的目的是尽快释放足够多的内存，让系统恢复。最后申请内存的进程可能只是一个很小的进程，杀掉它释放不了多少内存，很快又会再次 OOM；杀掉占内存最多的进程，释放的内存最多。而且「最后申请」的往往只是碰巧，真正吃掉内存的可能是另一个进程。

3. Redis 的 `BGSAVE` 和 AOF 重写要 fork。按严格的计算，fork 一个 20 GB 的进程，相当于又要答应 20 GB（写时复制可能要用到）。默认的启发式模式下，内存紧张时这样的 fork 可能失败，`BGSAVE` 报错 `Can't save in background: fork: Cannot allocate memory`。实际上子进程只读，写时复制的量取决于主进程的写入，通常远小于 20 GB，所以 Redis 建议设为 1（总是答应），同时自己控制 `maxmemory`、留出写时复制的余量。

</details>
