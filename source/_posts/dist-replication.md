---
title: 主从复制：同步与异步
date: 2026-11-14 12:00:00
updated: 2026-11-14 12:00:00
categories:
  - 分布式系统
  - 分布式基础
tags:
  - 分布式系统
  - 复制
order: 3
description: 一份数据放在一台机器上，机器坏了数据就没了，所以要复制到多台机器。本文给 dkv 加上主从复制：主节点把每次写入追加到复制日志，再发给从节点。实测三种确认方式：异步复制每秒 9 万次写入，等一个从节点确认降到 3.2 万次，等两个降到 1.75 万次；从节点的网络慢 1 毫秒时，同步复制只剩每秒 650 次；一个从节点慢 20 毫秒时，等一个确认不受影响，等两个只剩每秒 43 次。在写入中途杀掉主节点：异步复制时网络好的从节点少了最后 3 毫秒的 304 条确认过的写入，网络慢的从节点差了 20 万条；等一个从节点确认时，如果把落后的那个提升为主节点，仍然会丢 2 条。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解主从复制（基于日志的复制）的工作方式；理解异步、半同步、同步复制的区别和代价；会测量复制延迟，理解它为什么会越积越多；理解主节点崩溃时，每种复制方式会丢多少数据，以及为什么必须提升「最新」的从节点。
>
> **前置知识**：本章第 1、2 篇；《数据库》专栏的《主从复制》（MySQL 的 binlog 复制）。
>
> **实验环境**：同第 1 篇。n1 是主节点，n2、n3 是从节点；客户端在 c1 上。

## 1. 基于日志的复制

dkv 的主从复制和 MySQL、Redis、PostgreSQL 的思路一样：

1. 客户端只向**主节点**写入；
2. 主节点执行写入，并把它追加到**复制日志**，每一条有一个递增的编号（索引）；
3. 主节点为每个从节点开一个**复制线程**，把日志按顺序发过去；从节点按顺序执行，回复「我已经收到第 i 条」；
4. 主节点在合适的时候告诉客户端「写入成功」。

<figure class="diagram">
<svg viewBox="0 0 700 270" role="img" aria-label="主从复制：客户端写主节点 n1；n1 执行写入并追加到复制日志（编号 1、2、3……）；每个从节点一个复制线程，按顺序发送 REPL，从节点执行后回复 ACK；主节点按 -acks 等够确认后回复客户端">
  <defs><marker id="repl-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="repl-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="100" width="110" height="44" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="75.0" y="126.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">客户端</text><rect x="170" y="30" width="250" height="210" rx="8" style="fill:none;stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:5 4"/><text x="184.0" y="50.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">n1：主节点</text><rect x="184" y="62" width="222" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="295.0" y="86.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">键值表</text><rect x="184" y="120" width="33" height="30" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="200.5" y="139.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">1</text><rect x="221" y="120" width="33" height="30" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="237.5" y="139.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">2</text><rect x="258" y="120" width="33" height="30" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="274.5" y="139.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">3</text><rect x="295" y="120" width="33" height="30" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="311.5" y="139.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">4</text><rect x="332" y="120" width="33" height="30" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="348.5" y="139.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">5</text><rect x="369" y="120" width="33" height="30" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="385.5" y="139.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">6</text><text x="184.0" y="166.0" text-anchor="start" style="fill:var(--muted);font-size:11px">复制日志（PUT k v）</text><rect x="184" y="176" width="105" height="44" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="236.5" y="194.3" text-anchor="middle" style="fill:var(--fg);font-size:11px">复制线程</text><text x="236.5" y="209.3" text-anchor="middle" style="fill:var(--muted);font-size:10px">→ n2</text><rect x="301" y="176" width="105" height="44" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="353.5" y="194.3" text-anchor="middle" style="fill:var(--fg);font-size:11px">复制线程</text><text x="353.5" y="209.3" text-anchor="middle" style="fill:var(--muted);font-size:10px">→ n3</text><line x1="132" y1="114" x2="182" y2="84" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#repl-a)"/><text x="150.0" y="92.0" text-anchor="middle" style="fill:var(--link);font-size:11px">PUT</text><line x1="182" y1="136" x2="132" y2="134" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#repl-a)"/><text x="158.0" y="150.0" text-anchor="middle" style="fill:var(--link);font-size:11px">OK</text><rect x="530" y="50" width="150" height="70" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="605.0" y="81.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">n2：从节点</text><text x="605.0" y="97.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">已执行到 5</text><rect x="530" y="160" width="150" height="70" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="605.0" y="191.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">n3：从节点</text><text x="605.0" y="207.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">已执行到 3</text><path d="M289,198 C440,198 440,90 528,90" style="fill:none;stroke:var(--accent);stroke-width:1.5" marker-end="url(#repl-a)"/><text x="540.0" y="145.0" text-anchor="start" style="fill:var(--accent);font-size:10px">REPL 任期 i k v</text><path d="M406,206 C470,206 470,200 528,200" style="fill:none;stroke:var(--accent);stroke-width:1.5" marker-end="url(#repl-a)"/><path d="M528,70 C470,40 300,40 240,60" style="fill:none;stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#repl-a)"/><text x="480.0" y="36.0" text-anchor="start" style="fill:var(--muted);font-size:10px">ACK 5</text><path d="M528,215 C470,250 360,250 352,222" style="fill:none;stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#repl-a)"/><text x="442.0" y="260.0" text-anchor="start" style="fill:var(--muted);font-size:10px">ACK 3</text>
  </g>
</svg>
</figure>

日志中记的是**结果**而不是操作：`INCR k` 在主节点上算出新值 n，日志里记的是 `PUT k n`。这样从节点重复执行一条日志也不会多加一次，而且不同的节点执行出来的结果一定一样。

复制线程的主循环（简化）：先发一个 `HELLO`，从节点回复它已经有几条，然后从下一条开始，一批最多 256 条发过去，再等每一条的确认：

```c
for (uint64_t i = first; i <= last; i++)          /* 一批最多 256 条 */
    len += snprintf(out + len, sizeof out - len, "REPL %lu %lu %s %s\n",
                    epoch, i, logv[i - 1].key, logv[i - 1].val);
writeAll(fd, out, len);
for (uint64_t i = first; i <= last; i++) {
    lrRead(&r, line, sizeof line, 1000);          /* ACK i */
    if (sscanf(line, "ACK %lu", &a) == 1) {
        match[p] = a;                             /* 这个从节点确认到第 a 条 */
        pthread_cond_broadcast(&cv);              /* 唤醒在等确认的写入 */
    }
}
```

从节点收到 `REPL epoch i k v`，如果 i 正好是下一条，就执行并回复 `ACK i`；如果中间缺了，回复 `NEED n` 让主节点从第 n 条重发。`epoch`（任期）在第 4 篇用到。

## 2. 什么时候告诉客户端「成功」

这是复制最重要的选择。kvd 的 `-acks` 参数指定要等几个从节点确认：

```c
static int waitCommit(uint64_t idx)               /* 持锁调用 */
{
    if (acksNeeded == 0) return 0;                /* 异步：写完自己的就返回 */
    for (;;) {
        int n = 0;
        for (int i = 0; i < npeer; i++) n += match[i] >= idx;
        if (n >= acksNeeded) return 0;            /* 够了 */
        if (pthread_cond_timedwait(&cv, &mu, &dl) == ETIMEDOUT) return -1;
    }
}
```

| `-acks` | 名称 | 含义 |
|---|---|---|
| 0 | 异步复制 | 主节点自己写完就返回，日志稍后再发给从节点 |
| 1 | 半同步复制 | 至少一个从节点收到了才返回 |
| 2 | 同步复制 | 所有从节点都收到了才返回 |

3 个节点时，`-acks 1` 意味着「主节点 + 1 个从节点」，也就是**多数**节点有了这条数据。

## 3. 代价

c1 上的一个客户端连续写 5 秒，每次等到回复再写下一个：

| 网络 | 异步 | 等 1 个 | 等 2 个 |
|---|---|---|---|
| 不加延迟 | 90964 次/秒 | 32089 次/秒 | 17546 次/秒 |
| n2、n3 发出的包延迟 1 ms | 91043 次/秒 | 652 次/秒 | 633 次/秒 |
| n2 延迟 1 ms，n3 延迟 20 ms | 113706 次/秒 | 637 次/秒 | **43 次/秒** |

<figure class="diagram">
<svg viewBox="0 0 700 260" role="img" aria-label="一个客户端顺序写入的吞吐量（对数坐标）：网络不加延迟时异步 90964、等 1 个 32089、等 2 个 17546 次/秒；从节点慢 1 毫秒时 91043、652、633；n3 慢 20 毫秒时 113706、637、43">
  <defs><marker id="thr-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="thr-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="20.0" y="30.0" text-anchor="start" style="fill:var(--fg);font-size:12px">不加延迟</text><text x="202.0" y="30.0" text-anchor="end" style="fill:var(--muted);font-size:10px">异步</text><rect x="210.0" y="18.0" width="336.5" height="14.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="552.5" y="30.0" text-anchor="start" style="fill:var(--fg);font-size:10px">90,964</text><text x="202.0" y="48.0" text-anchor="end" style="fill:var(--muted);font-size:10px">等 1 个</text><rect x="210.0" y="36.0" width="298.0" height="14.0" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="514.0" y="48.0" text-anchor="start" style="fill:var(--fg);font-size:10px">32,089</text><text x="202.0" y="66.0" text-anchor="end" style="fill:var(--muted);font-size:10px">等 2 个</text><rect x="210.0" y="54.0" width="275.8" height="14.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="491.8" y="66.0" text-anchor="start" style="fill:var(--fg);font-size:10px">17,546</text><text x="20.0" y="96.0" text-anchor="start" style="fill:var(--fg);font-size:12px">n2、n3 慢 1 ms</text><text x="202.0" y="96.0" text-anchor="end" style="fill:var(--muted);font-size:10px">异步</text><rect x="210.0" y="84.0" width="336.5" height="14.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="552.5" y="96.0" text-anchor="start" style="fill:var(--fg);font-size:10px">91,043</text><text x="202.0" y="114.0" text-anchor="end" style="fill:var(--muted);font-size:10px">等 1 个</text><rect x="210.0" y="102.0" width="154.2" height="14.0" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="370.2" y="114.0" text-anchor="start" style="fill:var(--fg);font-size:10px">652</text><text x="202.0" y="132.0" text-anchor="end" style="fill:var(--muted);font-size:10px">等 2 个</text><rect x="210.0" y="120.0" width="153.1" height="14.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="369.1" y="132.0" text-anchor="start" style="fill:var(--fg);font-size:10px">633</text><text x="20.0" y="162.0" text-anchor="start" style="fill:var(--fg);font-size:12px">n2 慢 1 ms，n3 慢 20 ms</text><text x="202.0" y="162.0" text-anchor="end" style="fill:var(--muted);font-size:10px">异步</text><rect x="210.0" y="150.0" width="344.7" height="14.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="560.7" y="162.0" text-anchor="start" style="fill:var(--fg);font-size:10px">113,706</text><text x="202.0" y="180.0" text-anchor="end" style="fill:var(--muted);font-size:10px">等 1 个</text><rect x="210.0" y="168.0" width="153.4" height="14.0" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="369.4" y="180.0" text-anchor="start" style="fill:var(--fg);font-size:10px">637</text><text x="202.0" y="198.0" text-anchor="end" style="fill:var(--muted);font-size:10px">等 2 个</text><rect x="210.0" y="186.0" width="53.8" height="14.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="269.8" y="198.0" text-anchor="start" style="fill:var(--fg);font-size:10px">43</text><line x1="210.0" y1="216.0" x2="575.6" y2="216.0" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="210.0" y1="216.0" x2="210.0" y2="222.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="210.0" y="236.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">10</text><line x1="295.0" y1="216.0" x2="295.0" y2="222.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="295.0" y="236.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">100</text><line x1="380.0" y1="216.0" x2="380.0" y2="222.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="380.0" y="236.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1000</text><line x1="465.0" y1="216.0" x2="465.0" y2="222.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="465.0" y="236.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1 万</text><line x1="550.0" y1="216.0" x2="550.0" y2="222.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="550.0" y="236.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">10 万 次/秒</text>
  </g>
</svg>
</figure>

- 在同一台虚拟机中，网络往返只有几十微秒，同步复制的代价是多一次往返，吞吐量降到三分之一和五分之一；
- 从节点的回复慢 1 毫秒，同步复制的延迟就是 1.5 毫秒，吞吐量只剩 650 次，**是异步的 1/140**。异步复制完全不受影响，因为主节点根本不等；
- 只有一个从节点慢（20 毫秒）：等 1 个确认时，总有那个快的先确认，几乎不受影响；等 2 个确认，就必须等最慢的那个，每次写入 23 毫秒。**同步复制的速度由最慢的副本决定**，这就是为什么很少有系统「等所有副本」，而是等多数。

这里只有一个客户端、每次等到回复再发下一个，所以吞吐量就是延迟的倒数。实际中有很多客户端同时写，复制线程一次发一批，等一次往返就能确认很多条写入（和《数据库》专栏「存储引擎」一章中 WAL 的组提交是一个道理），同步复制的吞吐量会高得多，但**每一次写入的延迟**仍然至少多一次往返。

## 4. 复制延迟会越积越多

异步复制时，从节点落后多少？n3 的网络慢 20 毫秒，写入开始后 2 秒查看三个节点的日志长度：

```
n1 log=201037
n2 log=206560          （比 n1 多，是因为三次查询有先后，n2 是稍后查的）
n3 log=19201
```

n3 只收到了十分之一。原因在 dkv 的复制线程：每批最多 256 条，发完要等确认才发下一批，一批要一个往返（20 毫秒），所以最快每秒 256 / 0.02 = 12800 条，而主节点每秒写入约 9 万条。**从节点追不上，落后的条数就一直增长**。

真实系统的复制线程会流水线式地发送，不必等上一批的确认，但同样的问题仍然存在：从节点执行日志比主节点慢（单线程执行、磁盘慢、正在执行一个大查询），复制延迟就会越积越多。MySQL 的从库延迟几分钟甚至几小时，是运维中的常见问题。

## 5. 主节点崩溃，丢了什么

在客户端写入的过程中，用 `kill -9` 杀掉主节点 n1，然后检查客户端**收到了 OK 的写入**在 n2、n3 上还有多少。客户端把每次收到 OK 的写入都记在文件中，`cli check` 逐条核对：

```
== acks 0
acked 226832, errors 1
acked 226832, missing on 10.77.0.12:7000: 304 (acked between 1793947547.009 and 1793947547.012)
acked 226832, missing on 10.77.0.13:7000: 206863 (acked between 1793947544.894 and 1793947547.012)
```

| 复制方式 | 客户端收到 OK 的写入 | n2（延迟 1 ms）缺少 | n3（延迟 20 ms）缺少 |
|---|---|---|---|
| 异步 | 226832 | **304** | **206863** |
| 等 1 个从节点 | 1484 | 0 | **2** |
| 等 2 个从节点 | 107 | 0 | 0 |

<figure class="diagram">
<svg viewBox="0 0 700 200" role="img" aria-label="写入中途 kill -9 主节点后，客户端收到 OK 的写入在各从节点上缺少的条数（对数坐标）：异步复制时 n2 缺 304 条、n3 缺 206863 条；等 1 个从节点时 n2 缺 0、n3 缺 2 条；等 2 个时都不缺">
  <defs><marker id="loss-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="loss-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="190.0" y="29.0" text-anchor="end" style="fill:var(--fg);font-size:12px">异步：n2（慢 1 ms）</text><rect x="200.0" y="14.0" width="209.0" height="20.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="417.0" y="29.0" text-anchor="start" style="fill:var(--fg);font-size:11px">304 条</text><text x="190.0" y="59.0" text-anchor="end" style="fill:var(--fg);font-size:12px">异步：n3（慢 20 ms）</text><rect x="200.0" y="44.0" width="378.9" height="20.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="586.9" y="59.0" text-anchor="start" style="fill:var(--fg);font-size:11px">206,863 条</text><text x="190.0" y="89.0" text-anchor="end" style="fill:var(--fg);font-size:12px">等 1 个：n2</text><text x="208.0" y="89.0" text-anchor="start" style="fill:var(--muted);font-size:11px">0（一条不缺）</text><text x="190.0" y="119.0" text-anchor="end" style="fill:var(--fg);font-size:12px">等 1 个：n3</text><rect x="200.0" y="104.0" width="78.1" height="20.0" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="286.1" y="119.0" text-anchor="start" style="fill:var(--fg);font-size:11px">2 条</text><text x="190.0" y="149.0" text-anchor="end" style="fill:var(--fg);font-size:12px">等 2 个：n2、n3</text><text x="208.0" y="149.0" text-anchor="start" style="fill:var(--muted);font-size:11px">0（一条不缺）</text><text x="20.0" y="186.0" text-anchor="start" style="fill:var(--muted);font-size:11px">确认了却不在从节点上的写入：提升这个从节点为主节点，它们就永久丢失</text>
  </g>
</svg>
</figure>

**异步复制**：n2 缺少最后 3 毫秒内确认的 304 条，这些写入主节点已经告诉客户端成功了，但还没来得及发给 n2。n3 落后得多，缺了 20 万条。提升任何一个从节点为新的主节点，这些写入都**永久丢失**了，而客户端以为它们早已成功。

**等 1 个从节点**：每条确认过的写入至少在一个从节点上。n2 一条不缺；但 n3 缺了 2 条，那两条是由 n2 确认的。如果把 n3 提升为主节点，仍然会丢数据。所以**半同步复制只有在提升「拥有最新日志」的从节点时才不丢数据**，故障转移时必须比较各个从节点的日志长度。MySQL 的半同步复制配合 MHA 等工具做故障转移时，第一步就是找出最新的从节点；Raft 则把这个规则写进了选举：日志不够新的节点得不到选票。

**等 2 个从节点**：每条确认过的写入在所有节点上，提升谁都不丢。代价是第 3 节的速度，以及：**任何一个从节点挂了，整个系统都不能写入**。

## 6. 读从节点

复制还可以用来分担读：读请求发给从节点。但从节点可能落后，读到的是旧数据。比如一个用户刚改了昵称，刷新页面时读的是落后的从节点，昵称又变回去了。常见的应对：

- **读自己的写**：用户刚写过的数据，一段时间内从主节点读；
- 记下自己写入时的日志位置，只从已经追上这个位置的从节点读；
- 需要最新数据的读，一律走主节点。

第 6 篇会从另一个角度讨论这个问题：读写多少个副本，才能保证读到最新的数据。

## 7. 常见错误

1. **以为异步复制「几乎不丢」**。主节点崩溃时，最后几毫秒确认的写入（实测 304 条）一定丢；从节点落后时，丢得更多。
2. **以为半同步复制一定不丢**。落后的从节点上缺了 2 条确认过的写入，提升它就会丢数据。
3. **同步复制等所有从节点**。速度由最慢的副本决定（43 次/秒），任何一个副本挂了就无法写入。
4. **不监控复制延迟**。从节点追不上时，落后会一直增长。
5. **从从节点读数据却假设它是最新的**。

## 8. 小结

- 主从复制：主节点把写入的结果追加到复制日志，按顺序发给从节点；
- 异步复制每秒 9.1 万次写入，等 1 个确认 3.2 万次，等 2 个 1.75 万次；从节点慢 1 毫秒时同步复制只剩 650 次；
- 一个从节点慢 20 毫秒时，等 1 个确认不受影响，等 2 个降到每秒 43 次，所以实际系统等多数而不是等全部；
- 复制追不上写入时，落后会越积越多：2 秒时慢的从节点只有主节点十分之一的日志；
- 杀掉主节点：异步复制丢最后 3 毫秒的 304 条确认过的写入；等 1 个确认时，提升落后的从节点仍丢 2 条。

## 9. 练习

1. 3 个节点，等 1 个从节点确认。如果一个从节点挂了，还能写入吗？两个都挂了呢？
2. 主节点在执行写入之后、复制之前崩溃了，客户端还没收到回复。这条写入算丢了吗？客户端应该怎么做？
3. 为什么日志中要记录 `PUT k n`，而不是 `INCR k`？

<details>
<summary>点击查看答案</summary>

1. 一个从节点挂了，另一个还能确认，可以继续写入。两个都挂了，等不到任何确认，每次写入都会超时失败（kvd 等 2 秒后返回 `ERR timeout`）。这就是「多数」的含义：3 个节点能容忍 1 个故障。

2. 不算丢：客户端没有收到 OK，系统没有承诺过它成功。但客户端也不知道它失败了（第 1 篇的「超时之后不知道执行了没有」），只能重试，所以写入要幂等，或者带上请求编号。而且，这条写入可能已经在主节点上执行了，如果主节点之后重启并重新加入，它的数据和新的主节点就不一致了，需要回滚这些没有复制出去的日志。

3. 有三个原因。一是幂等：从节点因为重连而收到同一条日志两次（`idx <= logLen`），`PUT k n` 执行两次结果一样，`INCR k` 会多加一次。二是确定性：有些操作在不同节点上执行结果可能不同，比如取当前时间、生成随机数，必须在主节点上算好结果再复制。三是从节点不需要知道原来的值，即使它的状态和主节点稍有不同，执行完同一条日志后这个键的值也是一样的。代价是日志可能更大，比如「把所有用户的积分加一」，复制结果要写每一个用户。

</details>
