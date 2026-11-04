---
title: fork 与写时复制
date: 2026-11-09 10:30:00
updated: 2026-11-09 10:30:00
categories:
  - Linux基础
  - 内存管理
tags:
  - Linux
  - 内存
  - 进程
order: 4
description: fork 不复制内存，只复制页表，父子进程共享物理页，谁写谁复制。实测：父进程占 1.3 GB 时 fork 要 1.87 毫秒，换成透明大页后只要 0.66 毫秒；子进程写遍 256 MB，发生了 65539 次缺页、复制出 256 MB；只读不写一页都不复制。透明大页上每 2 MB 写一个字节，内核 6.8 只复制了 4 KB 的小页，而不是整个 2 MB。最后在 1 GB 的进程中启动程序：fork + exec 每次 4.3 毫秒，posix_spawn 只要 0.1 毫秒。
top_group_list: true
disableNunjucks: true
viz: [lx-mem]
---

> **本文目标**：理解 fork 时复制了什么、没复制什么；理解写时复制的过程和代价；会测量 fork 的耗时和写时复制的内存；知道透明大页和写时复制的关系；知道在大进程中启动子程序时用 `posix_spawn`。
>
> **前置知识**：本专栏的《进程：fork、exec、wait》（第 1.1 节简单介绍过写时复制）；本章第 1 篇。
>
> **实验环境**：同第 1 篇。每次测量前清空页缓存并整理内存（`echo 1 > /proc/sys/vm/compact_memory`），让透明大页能分配得到。

## 1. fork 复制了什么

《进程：fork、exec、wait》中看到，fork 以后父子进程的变量互不影响，好像整个内存都被复制了一份。如果真的复制，一个 10 GB 的进程 fork 一次就要复制 10 GB，几秒钟，还要多用 10 GB 内存。实际上 fork 做的是：

1. 复制进程的**页表**（以及文件描述符表等内核数据结构），子进程的页表指向**同样的物理页**；
2. 把父子进程中所有可写的私有页都标记为**只读**；
3. 之后任何一方写某一页时，触发缺页；内核发现这是写时复制的页，**复制这一页**，让写的一方指向新的副本，并恢复可写。

<figure class="diagram">
<svg viewBox="0 0 700 220" role="img" aria-label="写时复制：fork 以后父子进程的页表指向同样的物理页，都标记为只读。子进程写页 1 时触发缺页，内核复制一份给子进程，父进程仍然使用原来的页；没被写的页继续共享">
  <defs><marker id="cwc-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="cwc-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="20.0" y="22.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">父进程页表</text><text x="20.0" y="132.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">子进程页表</text><rect x="20" y="32" width="120" height="24" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="80.0" y="47.8" text-anchor="middle" style="fill:var(--fg);font-size:10px">页 0（只读）</text><rect x="20" y="142" width="120" height="24" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="80.0" y="157.8" text-anchor="middle" style="fill:var(--fg);font-size:10px">页 0（只读）</text><rect x="20" y="62" width="120" height="24" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="80.0" y="77.8" text-anchor="middle" style="fill:var(--fg);font-size:10px">页 1（可写）</text><rect x="20" y="172" width="120" height="24" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="80.0" y="187.8" text-anchor="middle" style="fill:var(--fg);font-size:10px">页 1（可写）</text><rect x="20" y="92" width="120" height="24" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="80.0" y="107.8" text-anchor="middle" style="fill:var(--fg);font-size:10px">页 2（只读）</text><rect x="20" y="202" width="120" height="24" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="80.0" y="217.8" text-anchor="middle" style="fill:var(--fg);font-size:10px">页 2（只读）</text><text x="330.0" y="22.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">物理页</text><rect x="330" y="52" width="100" height="30" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="380.0" y="71.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">物理页 A</text><rect x="330" y="92" width="100" height="30" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="380.0" y="111.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">物理页 B</text><rect x="330" y="132" width="100" height="30" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="380.0" y="151.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">物理页 C</text><rect x="330" y="172" width="100" height="30" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="380.0" y="191.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">B′（复制）</text><line x1="140" y1="44" x2="328" y2="67" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#cwc-a)"/><line x1="140" y1="74" x2="328" y2="107" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#cwc-a)"/><line x1="140" y1="104" x2="328" y2="147" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#cwc-a)"/><line x1="140" y1="154" x2="328" y2="67" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#cwc-a)"/><line x1="140" y1="214" x2="328" y2="147" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#cwc-a)"/><line x1="140" y1="184" x2="328" y2="187" style="stroke:var(--accent);stroke-width:1.5" marker-end="url(#cwc-a)"/><text x="460.0" y="120.0" text-anchor="start" style="fill:var(--fg);font-size:12px">子进程写页 1：</text><text x="460.0" y="140.0" text-anchor="start" style="fill:var(--muted);font-size:11px">缺页 → 复制 4 KB → 指向 B′</text><text x="460.0" y="170.0" text-anchor="start" style="fill:var(--muted);font-size:11px">页 0、页 2 仍然共享</text>
  </g>
</svg>
</figure>

<div class="dsviz" data-viz="lx-cow"></div>

## 2. fork 的耗时

fork 一个立即退出的子进程，测 `fork()` 本身的时间（10 次平均）：

```c
fflush(stdout);   // 否则缓冲区中还没输出的内容会被子进程复制一份，输出两次
for (int k = 0; k < 10; ++k) {
    double t0 = nowSeconds();
    pid_t pid = fork();
    if (pid == 0) _exit(0);
    total += nowSeconds() - t0;
    waitpid(pid, NULL, 0);
}
```

（这个 `fflush` 是实验中踩到的坑：第一次运行时忘了它，程序的输出被打印了好几遍。输出被重定向到管道时 stdout 是全缓冲的，fork 把缓冲区也复制了一份，就是《进程：fork、exec、wait》第 1.2 节中「printf 被输出了两次」的问题。）

```
父进程 RSS      1 MB：fork 0.022 毫秒
父进程 RSS    257 MB：fork 0.380 毫秒（4 KB 页）
父进程 RSS   1281 MB：fork 1.874 毫秒（4 KB 页）
父进程 RSS   1281 MB：fork 0.660 毫秒（其中 1 GB 是透明大页，AnonHugePages 868 MB）
```

- **fork 的耗时和父进程的内存大小成正比**：复制页表要逐项处理，1.3 GB 有 33 万个 4 KB 的页表项，用了 1.87 毫秒，大约每 GB 1.5 毫秒；
- 同样的内存，其中 1 GB 换成透明大页（实际分到了 868 MB 的大页），**只要 0.66 毫秒**：一个大页只需要一个页表项，要复制的页表少了很多。

1.87 毫秒看起来不多，但 fork 期间父进程什么也做不了。Redis 这种单线程的服务，几十 GB 的实例每次 `BGSAVE` 都要 fork，主线程就会停顿几十到上百毫秒（《数据库》专栏的《持久化》中，400 MB 的实例 fork 用了 1.5 毫秒，Redis 自己统计在 `latest_fork_usec` 中）。

## 3. 写时复制

父进程先写满 256 MB（4 KB 页），fork 以后，让子进程以不同的方式写这块内存，用 `/proc/self/smaps_rollup` 中的 `Private_Dirty` 计算子进程复制出了多少私有内存：

```
写 256 MB 中的每一页（4 KB 页） 缺页  65539 次，用时   53.6 毫秒，复制出  256.0 MB
只读不写                         缺页      3 次，用时    0.0 毫秒，复制出    0.0 MB
每 2 MB 写一个字节（4 KB 页）    缺页    131 次，用时    0.1 毫秒，复制出    0.5 MB
```

- **写遍 256 MB：65536 次写时复制的缺页**（另外 3 次是程序本身），复制了全部 256 MB，用了 53.6 毫秒。这时父子进程各有一份，机器上实际多用了 256 MB；
- **只读不写：一页都不复制**。fork 以后立即 exec（大部分的 fork 都是这样）、或者子进程只读数据（Redis 的 `BGSAVE` 子进程只读内存、写文件），代价都很小；
- **每 2 MB 写一个字节：只复制了被写的那 128 个页**（0.5 MB）。写时复制的单位是页。

所以写时复制多用多少内存，取决于 fork 以后**父子进程总共修改了多少个不同的页**。Redis 的 `BGSAVE` 期间，主进程照常处理写入，每改一个 key，它所在的页就要复制一次；写入越多、越分散，多用的内存越多（《持久化》中重写 85 万个 key，多用了 202 MB）。

## 4. 透明大页和写时复制

一种常见的说法是：开启透明大页以后，写时复制的单位变成了 2 MB，改一个字节也要复制 2 MB，内存会暴增。Redis 启动时如果发现透明大页开着，也会在日志中警告。在本文的内核（6.8）上测一下：

```
父进程 AnonHugePages 256 MB
每 2 MB 写一个字节（透明大页）   缺页    131 次，用时    0.3 毫秒，复制出    0.5 MB
写 256 MB 中的每一页（透明大页） 缺页  65539 次，用时   46.0 毫秒，复制出  256.0 MB
```

父进程的 256 MB 全部是大页。子进程每 2 MB 写一个字节，**只复制出了 0.5 MB，和 4 KB 页时完全一样**，并没有复制 128 个 2 MB 的大页（那会是 256 MB）。写遍每一页时，同样是 65539 次缺页，而不是 128 次。

<figure class="diagram">
<svg viewBox="0 0 700 170" role="img" aria-label="透明大页上的写时复制：子进程每 2 MB 写一个字节。内核 6.8 先把共享的 2 MB 大页拆分成 512 个 4 KB 的页表项，只复制被写的那一个 4 KB 页，实测复制出 0.5 MB；较老的内核会复制整个 2 MB 大页，那样会复制出 256 MB">
  <defs><marker id="cwt-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="cwt-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="20.0" y="22.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">一个 2 MB 的透明大页（512 个 4 KB）</text><rect x="20" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="40" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="60" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="80" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="100" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="120" y="34" width="18" height="30" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><rect x="140" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="160" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="180" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="200" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="220" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="240" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="260" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="280" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="300" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="320" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="340" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="360" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="380" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="400" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="420" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="440" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="460" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="480" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="500" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="520" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="540" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="560" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="580" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="600" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="620" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><rect x="640" y="34" width="18" height="30" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="20.0" y="86.0" text-anchor="start" style="fill:var(--warn);font-size:12px">写其中一个字节：拆分大页，只复制这 4 KB（黄色）</text><text x="160.0" y="114.0" text-anchor="end" style="fill:var(--fg);font-size:12px">内核 6.8 实测</text><rect x="170" y="100" width="2.0" height="18" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="180.0" y="114.0" text-anchor="start" style="fill:var(--muted);font-size:11px">0.5 MB</text><text x="160.0" y="140.0" text-anchor="end" style="fill:var(--fg);font-size:12px">复制整个大页（推算）</text><rect x="170" y="126" width="409.6" height="18" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="587.6" y="140.0" text-anchor="start" style="fill:var(--muted);font-size:11px">256 MB</text>
  </g>
</svg>
</figure>

原因是：在这个内核上，对一个共享的透明大页发生写时复制时，内核先把它**拆分**成 512 个 4 KB 的页表项，然后只复制被写的那一个 4 KB 页。较老的内核在这种情况下会尝试分配一个新的 2 MB 大页、复制整个 2 MB，这正是那种说法的来源。

所以「透明大页让写时复制复制 2 MB」**取决于内核版本**，在 6.8 上不成立。但这不代表透明大页没有代价：大页被拆分以后就不再是大页了（之后需要后台线程重新合并）；分配大页时可能要整理内存，造成不可预测的延迟；只用了一小部分的大页也占着 2 MB。这些仍然是 Redis、MongoDB 等建议关闭透明大页（或者使用 `madvise` 模式）的理由。

## 5. 在大进程中启动子程序

一个占 1 GB 内存的服务要执行一个外部命令（比如调用 `/bin/true`）。传统的做法是 fork + exec，但 fork 要先复制 1 GB 内存的页表，exec 又马上把它们全部丢掉。比较 200 次：

```c
// fork + exec
pid_t pid = fork();
if (pid == 0) {
    execv("/bin/true", args);
    _exit(127);
}
waitpid(pid, NULL, 0);

// posix_spawn
posix_spawn(&pid, "/bin/true", NULL, NULL, args, environ);   // glibc 用 clone(CLONE_VM | CLONE_VFORK) 实现，不复制页表
waitpid(pid, NULL, 0);
```

```
fork + exec：  4.3 毫秒/次
posix_spawn：  0.1 毫秒/次
```

**`posix_spawn` 快了 40 多倍**。glibc 的 `posix_spawn` 用 `clone(CLONE_VM | CLONE_VFORK)`：子进程暂时和父进程共用同一个地址空间（不复制页表），父进程暂停，直到子进程 exec 或退出。fork + exec 的 4.3 毫秒中，除了复制页表，还有子进程退出、exec 时释放这些页表的时间。

大进程中启动外部命令应该用 `posix_spawn`（或者 `vfork`，但它的限制很多，容易用错）。很多语言的进程库在 Linux 上也会尽量使用 `vfork` 或 `posix_spawn`，而不是 fork。

## 6. 常见错误

1. **以为 fork 会复制所有的内存**。只复制页表，写时才复制被写的页。
2. **以为 fork 不占额外的内存**。fork 以后父子进程写了多少页，就多用多少内存，最坏情况下翻倍；内存不足时可能触发 OOM（下一篇）。
3. **fork 之前不 `fflush`**，缓冲区中的内容被输出两次。
4. **在多线程程序中 fork 以后调用非异步信号安全的函数**（`malloc`、`printf`）：fork 只复制调用它的那个线程，别的线程持有的锁在子进程中永远不会被释放，子进程可能死锁。fork 以后应该尽快 exec。
5. **在内存很大的进程中频繁 fork + exec**，改用 `posix_spawn`。
6. **把「透明大页让写时复制复制 2 MB」当成所有内核都成立的事实**。要以实测为准。

## 7. 小结

- fork 只复制页表，父子进程共享物理页并标记为只读，谁写谁复制被写的那一页；
- fork 的耗时和内存大小成正比：1.3 GB 时 1.87 毫秒，换成透明大页 0.66 毫秒；
- 子进程写遍 256 MB 复制了 256 MB，只读不复制，每 2 MB 写一个字节只复制 0.5 MB；
- 内核 6.8 上，透明大页的写时复制会先拆分大页，只复制 4 KB；
- 在 1 GB 的进程中启动程序，fork + exec 4.3 毫秒，`posix_spawn` 0.1 毫秒。

## 8. 练习

1. 一个 20 GB 的 Redis 每小时 `BGSAVE` 一次，期间写入很频繁。fork 会让主线程停顿多久？`BGSAVE` 期间最多可能多用多少内存？
2. 为什么 fork 以后，父进程的写入也会触发写时复制，而不只是子进程？
3. Shell 执行每一条外部命令都要 fork + exec。为什么 Shell 本身不会因此变慢？

<details>
<summary>点击查看答案</summary>

1. 按本文约每 GB 1.5 毫秒估算，20 GB 的页表复制约 30 毫秒；在有的虚拟机上可能慢得多（每 GB 十几毫秒甚至更多），主线程会停顿几十到几百毫秒。`BGSAVE` 期间多用的内存取决于主进程修改了多少个不同的页，最坏情况下所有的页都被改过，接近 20 GB；实际取决于写入量和写入的分布。

2. fork 以后，物理页同时被父子进程的页表引用，内核把双方的页表项都标记为只读。无论哪一方先写，都会触发缺页，内核复制一份给写的一方，另一方继续使用原来的页。所以 Redis 的 `BGSAVE` 中，实际复制内存的是不断写入的父进程（主进程），而子进程只读。

3. Shell 本身占用的内存很小（几 MB），fork 只需要复制很少的页表，耗时在几十微秒（本文中 RSS 1 MB 的进程 fork 只要 0.022 毫秒）；子进程立即 exec，几乎没有写时复制。fork + exec 只在父进程很大时才明显变慢。

</details>
