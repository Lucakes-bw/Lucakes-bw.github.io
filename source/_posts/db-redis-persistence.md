---
title: 持久化：RDB 与 AOF
date: 2026-11-07 10:40:00
updated: 2026-11-07 10:40:00
categories:
  - 数据库
  - Redis
tags:
  - 数据库
  - Redis
  - 持久化
order: 5
description: 数据在内存中，进程一退出就没了。RDB 定期把整个数据库写成快照，AOF 把每条写命令追加到日志。本文让 Redis 不停地 INCR 一个计数器，然后 kill -9：默认配置（只有 RDB）下，重启后 20 万次已确认的写入全部消失；AOF everysec 一次都没丢；AOF always 的吞吐量只有每秒 6239 次，重启后的值比客户端最后收到的确认还多 1。再看 fork：400 MB 的数据 fork 只要 1.5 毫秒，但快照期间重写 85 万个 key，写时复制多用了 202 MB 内存。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 RDB 和 AOF 的原理；会配置保存条件和 `appendfsync`；能说清楚每种配置在进程崩溃、机器断电时会丢多少数据；理解 fork 和写时复制的开销；能根据场景选择持久化方式。
>
> **前置知识**：本章前几篇；本专栏的[《日志与崩溃恢复》](/posts/db-logs-recovery/)（redo log 和 `innodb_flush_log_at_trx_commit`）；《Linux 基础》专栏的 fork 和 mmap。
>
> **实验环境**：同第 1 篇。每种配置单独启动一个 Redis 容器，崩溃用 `docker kill -s KILL` 模拟，相当于 `kill -9`：进程立即终止，但操作系统和磁盘照常工作。断电的情况在第 3.3 节分析。

## 1. 两种持久化

<figure class="diagram">
<svg viewBox="0 0 700 220" role="img" aria-label="RDB 和 AOF。RDB：fork 一个子进程，把某一时刻的整个数据库写成 dump.rdb。AOF：每条写命令执行后追加到 AOF 缓冲区，写入 AOF 文件；重写时由子进程生成新的基础文件">
  <defs><marker id="rtw-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rtw-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="250" y="20" width="200" height="40" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="350.0" y="44.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">Redis 内存中的数据</text><text x="14.0" y="100.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">RDB：快照</text><rect x="40" y="120" width="150" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="115.0" y="142.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">fork 子进程</text><rect x="40" y="176" width="150" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="115.0" y="189.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">dump.rdb</text><text x="115.0" y="204.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">某一时刻的全部数据</text><line x1="290" y1="60" x2="160" y2="118" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rtw-a)"/><line x1="115" y1="156" x2="115" y2="174" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rtw-a)"/><text x="470.0" y="100.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">AOF：日志</text><rect x="450" y="120" width="100" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="500.0" y="142.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">SET a 1</text><rect x="560" y="120" width="110" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="615.0" y="142.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">INCR b</text><rect x="450" y="176" width="220" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="560.0" y="189.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">appendonly.aof</text><text x="560.0" y="204.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">每条写命令依次追加</text><line x1="410" y1="60" x2="500" y2="118" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rtw-a)"/><line x1="560" y1="156" x2="560" y2="174" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rtw-a)"/><text x="250.0" y="120.0" text-anchor="start" style="fill:var(--muted);font-size:11px">重启：加载 RDB（快）</text><text x="250.0" y="140.0" text-anchor="start" style="fill:var(--muted);font-size:11px">或重放 AOF（全）</text>
  </g>
</svg>
</figure>

- **RDB**（Redis Database）：在某个时刻，把内存中的整个数据库写成一个紧凑的二进制文件（快照）。重启时读入这个文件；
- **AOF**（Append Only File）：每执行一条写命令，就把它追加到日志文件的末尾。重启时把日志中的命令重新执行一遍。

这和 MySQL 很像：RDB 相当于全量备份，AOF 相当于 binlog / redo log。区别是 Redis 的数据都在内存中，没有「数据页」要落盘，日志就是全部。

## 2. RDB

### 2.1 SAVE 和 BGSAVE

- `SAVE`：在主线程中写快照，写完之前不处理任何命令。几 GB 的数据要几十秒，线上不能用；
- `BGSAVE`：`fork` 一个子进程，子进程把它看到的内存写成文件，父进程继续处理命令。

`fork` 以后，父子进程共享同样的物理内存页，子进程看到的是 fork 那一刻的数据，无论父进程之后怎么修改。这依靠操作系统的**写时复制**（copy-on-write）：父进程修改某一页时，内核先把这一页复制一份给父进程，子进程仍然看到原来的那一页。第 4 节实测它的代价。

### 2.2 自动保存

本文环境的默认配置：

```
save 3600 1 300 100 60 10000
appendonly no
```

意思是：3600 秒内至少 1 次修改、300 秒内至少 100 次修改、60 秒内至少 10000 次修改，满足任意一个就执行一次 `BGSAVE`。也就是说，**默认情况下最近一分钟到一小时的修改只在内存中**。

### 2.3 实测：kill -9

一个程序不停地 `INCR counter`，记下最后一次收到的回复（客户端认为成功了的值）；4 秒后杀掉 Redis 进程，再重启：

```c
long long acked = 0;
for (;;) {
    redisReply* r = redisCommand(c, "INCR counter");
    if (!r) break;                  // 连接断开：服务器被杀掉了
    acked = r->integer;             // 服务器已经回复：客户端认为这次写入成功了
    freeReplyObject(r);
}
```

```
=== RDB（默认配置）
逐个 SET 50000 次：0.97 秒，51430 次/秒，平均 19.4 微秒
最后一次得到确认的值：202014
重启以后 counter = （不存在）
    -rw------- 1 redis redis 89 Nov  4 07:26 dump.rdb
```

**20 万次已经确认的写入全部丢失。** 4 秒内只有一个 key 被修改了 20 万次，还没满足「60 秒内 10000 次修改」（时间不够 60 秒）。`dump.rdb` 只有 89 字节，是实验开始时 `FLUSHALL` 触发的一次空数据库的快照。

RDB 适合做**备份**（文件紧凑、一个文件就是一个时间点）和**快速重启**（加载二进制文件比重放命令快得多），不适合做唯一的持久化手段：两次快照之间的修改，崩溃时全部丢失。

## 3. AOF

### 3.1 写日志和刷盘

开启 AOF（`appendonly yes`）以后，每条写命令执行完，Redis 把它以协议格式追加到一个缓冲区中。在事件循环回到 `epoll_wait` 之前（也就是**回复客户端之前**），缓冲区被 `write` 到 AOF 文件。

`write` 只是把数据交给了操作系统的页缓存，还没有到磁盘。什么时候 `fsync`，由 `appendfsync` 决定：

| `appendfsync` | 什么时候 fsync | 进程崩溃 | 机器断电、内核崩溃 |
|---|---|---|---|
| `always` | 每次写入后，回复客户端之前 | 不丢 | 不丢已确认的写入 |
| `everysec`（默认） | 后台线程每秒一次 | 不丢 | 最多丢约 1 秒（极端情况 2 秒） |
| `no` | 交给操作系统（通常 30 秒左右） | 不丢 | 可能丢几十秒 |

<figure class="diagram">
<svg viewBox="0 0 700 210" role="img" aria-label="AOF 写入的路径：命令执行后进入 AOF 缓冲区，回复客户端之前 write 到内核页缓存，之后由 fsync 写到磁盘。always 每次写入后 fsync，everysec 每秒一次，no 交给操作系统。进程崩溃时页缓存中的数据不会丢；断电时会丢失还没 fsync 的部分">
  <defs><marker id="rfs-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rfs-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="40" width="130" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="85.0" y="64.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">执行命令</text><rect x="170" y="40" width="130" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="235.0" y="64.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">AOF 缓冲区</text><rect x="340" y="40" width="130" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="405.0" y="64.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">内核页缓存</text><rect x="540" y="40" width="140" height="40" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="610.0" y="64.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">磁盘</text><line x1="150" y1="60" x2="168" y2="60" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rfs-a)"/><line x1="300" y1="60" x2="338" y2="60" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rfs-a)"/><text x="319.0" y="52.0" text-anchor="middle" style="fill:var(--link);font-size:11px">write</text><line x1="470" y1="60" x2="538" y2="60" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rfs-a)"/><text x="504.0" y="52.0" text-anchor="middle" style="fill:var(--link);font-size:11px">fsync</text><text x="305.0" y="104.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">回复客户端之前完成</text><text x="505.0" y="104.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">always：每次 / everysec：每秒</text><text x="505.0" y="120.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">no：交给操作系统</text><line x1="330" y1="138" x2="330" y2="200" style="stroke:var(--danger);stroke-width:1.5;stroke-dasharray:5 4"/><line x1="530" y1="138" x2="530" y2="200" style="stroke:var(--danger);stroke-width:1.5;stroke-dasharray:5 4"/><text x="320.0" y="170.0" text-anchor="end" style="fill:var(--danger);font-size:11px">进程崩溃：左边的丢失</text><text x="320.0" y="188.0" text-anchor="end" style="fill:var(--muted);font-size:11px">（已回复的都在右边，不丢）</text><text x="540.0" y="170.0" text-anchor="start" style="fill:var(--danger);font-size:11px">断电：右边以外的都丢失</text><text x="540.0" y="188.0" text-anchor="start" style="fill:var(--muted);font-size:11px">（everysec 最多约 1 秒）</text>
  </g>
</svg>
</figure>

注意第三列：**只要 `write` 已经完成，进程崩溃不会丢数据**，数据在内核的页缓存中，操作系统会把它写到磁盘。这和《日志与崩溃恢复》中 `innodb_flush_log_at_trx_commit = 2` 的分析一样。

### 3.2 实测

```
=== AOF everysec
逐个 SET 50000 次：1.09 秒，45824 次/秒，平均 21.8 微秒
最后一次得到确认的值：188629
重启以后 counter = 188629

=== AOF always
逐个 SET 50000 次：8.01 秒，6239 次/秒，平均 160.3 微秒
最后一次得到确认的值：27358
重启以后 counter = 27359
```

| 配置 | 逐个写入 | kill -9 前确认的值 | 重启后 |
|---|---|---|---|
| 只有 RDB（默认） | 51430 次/秒 | 202014 | 不存在 |
| AOF everysec | 45824 次/秒（慢 11%） | 188629 | 188629，一个不少 |
| AOF always | 6239 次/秒（慢 88%） | 27358 | 27359，多了 1 |

- **everysec 一次都没丢**，只慢了 11%。原因就是上面说的：回复之前已经 `write`，进程被杀掉不影响页缓存中的数据；
- **always 每次写入都要等 fsync**，每次 160 微秒，吞吐量只有原来的八分之一。在机械硬盘或者云盘上，一次 fsync 要几毫秒，差距会更大；
- always 重启后的值是 27359，**比客户端最后收到的确认多 1**。最后一次 INCR 已经执行并写入了 AOF，但回复还没发出（或者还在网络中），进程就被杀了。客户端看到的是「连接断开」，不知道这次写入到底成功了没有。

最后一点对任何数据库都成立：**连接断开时，最后一个请求的结果是未知的**。如果客户端「失败就重试」，计数器就多加了一次。需要准确的场景，操作要设计成幂等的（比如带一个请求 id，`SET … NX` 去重），或者重连后先查询再决定。

### 3.3 断电

上面的实验只模拟了进程崩溃。如果是断电或者内核崩溃，页缓存中还没 fsync 的数据会丢失：

- `everysec` 最多丢最近约 1 秒的写入；
- `always` 不会丢已经回复给客户端的写入；
- 只有 RDB 时，丢失上一次快照之后的所有修改。

而且 AOF 文件末尾可能有一条只写了一半的命令。Redis 启动时发现末尾不完整，默认（`aof-load-truncated yes`）截掉这条命令并继续启动，日志中会有警告。

### 3.4 AOF 重写

AOF 只追加，对同一个 key 修改 100 万次，日志中就有 100 万条命令，重启时也要重放 100 万次。**AOF 重写**（`BGREWRITEAOF`，或者文件增长到一定比例时自动触发）同样 fork 一个子进程，把当前的数据库写成一个新的基础文件，之前的日志就可以丢掉了。

Redis 7 以后，AOF 是一个目录，由清单文件管理多个文件：

```
appendonlydir/
    appendonly.aof.1.base.rdb      ← 基础文件：重写时的快照，默认用 RDB 格式
    appendonly.aof.1.incr.aof      ← 增量文件：之后的写命令
    appendonly.aof.manifest        ← 清单：
        file appendonly.aof.1.base.rdb seq 1 type b
        file appendonly.aof.1.incr.aof seq 1 type i startoffset 0
```

重启时先加载 RDB 格式的基础文件（快），再重放增量文件中的命令（少）。重写时开始一个新的增量文件，子进程写好新的基础文件以后，更新清单，删除旧文件。

## 4. fork 的代价

`BGSAVE` 和 AOF 重写都要 fork。写入 150 万个 key（每个值 200 字节），然后执行 `BGSAVE`，分别在「期间没有写入」和「期间不停地重写 key」两种情况下观察：

```
1500000 个 key，used_memory 399 MB
  期间没有写入    ：fork 1.5 毫秒，BGSAVE 共 0.52 秒，期间PING 最慢一次 1.5 毫秒，写时复制 0.8 MB
  同时重写全部 key：fork 0.9 毫秒，BGSAVE 共 0.50 秒，期间每批 1000 个 SET 最慢一次 2.0 毫秒，写时复制 202.1 MB
  BGSAVE 期间重写了 849000 个 key
```

<figure class="diagram">
<svg viewBox="0 0 700 230" role="img" aria-label="写时复制：fork 以后父子进程共享物理内存页。父进程修改某一页时，内核复制这一页给父进程，子进程仍然看到原来的页。实测 400 MB 数据，快照期间没有写入时多用 0.8 MB；重写 85 万个 key 时多用 202.1 MB">
  <defs><marker id="rcw-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rcw-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="30" width="120" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="80.0" y="51.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">父进程（Redis）</text><rect x="20" y="150" width="120" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="80.0" y="171.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">子进程（写 RDB）</text><rect x="260" y="90" width="64" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="292.0" y="111.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">页 1</text><rect x="340" y="90" width="64" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="372.0" y="111.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">页 2</text><rect x="420" y="90" width="64" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="452.0" y="111.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">页 3</text><rect x="500" y="90" width="64" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="532.0" y="111.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">页 4</text><line x1="140" y1="47" x2="258" y2="92" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rcw-a)"/><line x1="140" y1="47" x2="418" y2="92" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rcw-a)"/><line x1="140" y1="47" x2="498" y2="92" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rcw-a)"/><line x1="140" y1="167" x2="258" y2="122" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rcw-a)"/><line x1="140" y1="167" x2="338" y2="122" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rcw-a)"/><line x1="140" y1="167" x2="418" y2="122" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rcw-a)"/><line x1="140" y1="167" x2="498" y2="122" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rcw-a)"/><rect x="340" y="20" width="64" height="34" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="372.0" y="41.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">页 2′</text><line x1="140" y1="40" x2="338" y2="37" style="stroke:var(--accent);stroke-width:1.5" marker-end="url(#rcw-a)"/><text x="420.0" y="42.0" text-anchor="start" style="fill:var(--warn);font-size:11px">父进程改了页 2：复制一份</text><text x="260.0" y="150.0" text-anchor="start" style="fill:var(--muted);font-size:11px">子进程仍然看到 fork 那一刻的页 2</text><text x="20.0" y="214.0" text-anchor="start" style="fill:var(--muted);font-size:11px">实测（400 MB）：fork 1.5 毫秒；快照期间没有写入 → 多用 0.8 MB；重写 85 万个 key → 多用 202.1 MB</text>
  </g>
</svg>
</figure>

- **fork 本身很快**：400 MB 的数据只要 1.5 毫秒（`INFO stats` 的 `latest_fork_usec`）。fork 只复制页表，不复制数据，耗时和内存大小成正比，大约每 GB 几毫秒到十几毫秒（虚拟机中会慢一些）。几十 GB 的实例，fork 一次可能要卡住主线程几百毫秒；
- **写时复制的内存取决于快照期间改了多少页**：没有写入时只多用了 0.8 MB；0.5 秒内重写了 85 万个 key，**多用了 202 MB，是数据量的一半**。写入越密集、快照越慢，多出来的内存越多，最坏情况下接近整个数据集的大小。

所以：

- `maxmemory` 和机器内存之间要留出余量，否则 BGSAVE 时可能触发操作系统的 OOM killer；
- 关闭透明大页（Transparent Huge Pages）：开启时一页是 2 MB，改一个字节也要复制 2 MB，写时复制的内存会暴增。Redis 启动时如果发现透明大页开着，会在日志中警告；
- 写入高峰期避免手动执行 `BGSAVE`、`BGREWRITEAOF`；单机多实例时错开它们的快照时间。

## 5. 怎么选

| 场景 | 建议 |
|---|---|
| 纯缓存，数据可以从数据库重建 | 可以都关掉（`save ""`、`appendonly no`），或者只保留 RDB 用来加速重启 |
| 数据丢一点可以接受 | AOF `everysec` + RDB 定期备份（最常见） |
| 一条都不能丢 | AOF `always`，代价是吞吐量；更好的办法是不要把这种数据只放在 Redis 中 |

无论哪种，都要定期把 RDB 文件拷贝到另一台机器上：持久化只能防止进程崩溃和重启，防不了磁盘损坏、误删（`FLUSHALL` 同样会被写进 AOF）。

## 6. 常见错误

1. **以为 Redis 默认会保存所有的数据**。默认只有 RDB，崩溃时丢失上次快照之后的所有修改，实验中 20 万次写入全部丢失。
2. **为了「绝对安全」开 `always`**，吞吐量降到八分之一，却仍然防不了误删和磁盘损坏。
3. **以为 everysec 进程崩溃也会丢 1 秒**。进程崩溃不丢，丢的是断电和内核崩溃时的最后约 1 秒。
4. **内存用满了 `maxmemory` 设到机器内存的 90%**，fork 以后写时复制，进程被 OOM killer 杀掉。
5. **连接断开以后盲目重试非幂等的写入**，最后一次写入可能已经成功了（实验中重启后多了 1）。
6. **只在本机保存 RDB/AOF**，没有拷到别处，磁盘坏了就什么都没了。

## 7. 小结

- RDB 是快照，fork 子进程写，紧凑、加载快，但两次快照之间的修改会丢失：默认配置下 kill -9 丢了 20 万次写入；
- AOF 记录每条写命令，回复之前已经 `write`：everysec 进程崩溃不丢，断电最多丢约 1 秒，吞吐量只降 11%；always 不丢已确认的写入，但吞吐量只有八分之一；
- Redis 7 的 AOF 是 RDB 格式的基础文件加增量文件，重写时生成新的基础文件；
- fork 本身很快（400 MB 用 1.5 毫秒），但写时复制可能多用大量内存（实测 202 MB），要留出余量、关闭透明大页。

## 8. 练习

1. 一个 Redis 实例开了 AOF everysec，运行在云主机上。机房断电后重启，发现最后几百条订单状态的修改丢了。这符合预期吗？怎样避免？
2. 为什么 RDB 适合做备份，AOF 不适合？
3. 一个 20 GB 的实例，每小时执行一次 BGSAVE，期间写入很频繁。机器内存是 32 GB，`maxmemory` 设为 24 GB。可能出现什么问题？

<details>
<summary>点击查看答案</summary>

1. 符合预期：everysec 在断电时最多丢失最近约 1 秒（极端情况 2 秒）的写入，几百条修改正好在这个窗口内。订单状态这种不能丢的数据，权威来源应该是 MySQL（事务提交、`innodb_flush_log_at_trx_commit = 1`），Redis 中只放它的缓存；如果一定要在 Redis 中保存，用 `appendfsync always` 并接受吞吐量的下降，或者写入后用 `WAITAOF`（Redis 7.2 以后）等待 AOF 刷盘确认。

2. RDB 是一个时间点的完整快照，一个文件、紧凑，可以直接拷贝和归档，恢复时加载快；需要回到某个时间点，就用那个时间点的文件。AOF 是从基础文件开始的命令序列，文件大、恢复慢，而且误操作（`FLUSHALL`、误删）会原样记录在里面，恢复时会被重放（要手工从文件中删掉那条命令）。

3. BGSAVE 期间写入频繁，写时复制可能复制相当大一部分页面，最坏情况下额外占用接近 20 GB，加上本来的 20 GB 超过了 32 GB，操作系统的 OOM killer 可能杀掉 Redis（或者开始使用交换分区，所有的请求都变慢）。应该降低 `maxmemory`、在写入低峰期做快照、关闭透明大页，或者只在从节点上做持久化。

</details>
