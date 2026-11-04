---
title: 主从复制与哨兵
date: 2026-11-07 10:50:00
updated: 2026-11-07 10:50:00
categories:
  - 数据库
  - Redis
tags:
  - 数据库
  - Redis
  - 复制
  - 高可用
order: 6
description: 一主一从：从节点 5.3 秒完成 100 万个 key 的全量同步，其中 4.8 秒是主节点在故意等待。复制是异步的：从节点卡住 1 秒期间主节点崩溃，36531 次已确认的写入丢失；每次写入后 WAIT 一个从节点，一次都没丢，吞吐量降到五分之一。一个没开持久化的主节点被重启，5 秒后从节点同步了它的空数据库，数据全没了。最后用 3 个哨兵做自动故障转移：主节点挂掉 6.3 秒后完成切换，客户端写入中断 6.46 秒。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 Redis 主从复制的过程：全量同步、命令传播、部分重同步；理解异步复制在故障时会丢失已确认的写入，会用 `WAIT` 缩小这个窗口；知道「主节点不持久化又自动重启」的危险；会搭建哨兵，理解主观下线、客观下线和故障转移的过程。
>
> **前置知识**：本章前几篇，特别是上一篇的 RDB；本专栏的[《主从复制》](/posts/db-replication/)（MySQL 的复制）。
>
> **实验环境**：Redis 8.10.3。所有的容器在一个自定义的 Docker 网络中，用容器名互相访问，重启后地址不变。主节点 `rm`，从节点 `rr`；哨兵实验是 1 主 2 从加 3 个哨兵。

## 1. 为什么要复制

- **高可用**：主节点挂了，从节点有一份几乎完整的数据，可以顶上去；
- **读扩展**：读请求分给多个从节点；
- **在从节点上做持久化和备份**，主节点不用 fork。

和 MySQL 一样，Redis 的复制默认是**异步**的：主节点执行完命令就回复客户端，再把命令发给从节点。

## 2. 搭建与全量同步

在从节点上执行一条命令就开始复制：

```
REPLICAOF rm 6379
```

### 2.1 实测

主节点上先准备 100 万个 key（152 MB），再让从节点开始复制。从节点的日志：

```
07:29:54.344 * Connecting to MASTER rm:6379
07:29:54.346 * MASTER <-> REPLICA sync started
07:29:59.119 * MASTER <-> REPLICA sync: receiving streamed RDB from master with EOF to disk
07:29:59.404 * MASTER <-> REPLICA sync: Loading DB in memory
07:29:59.416 * MASTER <-> REPLICA sync: Flushing old data
07:29:59.417 * Loading RDB produced by version 8.10.3
07:29:59.638 * Done loading RDB, keys loaded: 1000000, keys expired: 0.
07:29:59.638 * MASTER <-> REPLICA sync: Finished with success
```

一共 5.3 秒，但真正传输和加载只用了 0.5 秒：**前 4.8 秒，主节点在等**。默认配置是：

```
repl-diskless-sync yes          # 不落盘：子进程生成 RDB，直接写到从节点的 socket
repl-diskless-sync-delay 5      # 开始传输前等 5 秒
```

无盘复制时，一次 RDB 流只能同时发给开始时已经连上来的从节点。等 5 秒，是为了让同时重启的多个从节点共用一次 fork 和传输。

主节点上看复制的状态：

```
role:master
connected_slaves:1
slave0:ip=172.18.0.4,port=6379,state=online,offset=0,lag=0,io-thread=0
```

从节点默认只读：

```
> SET x 1
READONLY You can't write against a read only replica.
```

### 2.2 同步的过程

<figure class="diagram">
<svg viewBox="0 0 700 250" role="img" aria-label="全量同步的过程：从节点发送 PSYNC，主节点回复 FULLRESYNC，等待 5 秒（repl-diskless-sync-delay）后 fork 子进程，把 RDB 直接写到从节点的 socket；从节点加载完 RDB 以后，主节点继续把新的写命令发给它。实测 100 万个 key：等待 4.8 秒，传输和加载 0.5 秒">
  <defs><marker id="rsy-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rsy-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="120.0" y="22.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">主节点</text><text x="560.0" y="22.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">从节点</text><line x1="120" y1="30" x2="120" y2="240" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="560" y1="30" x2="560" y2="240" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="558" y1="44" x2="122" y2="60" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rsy-a)"/><text x="340.0" y="46.0" text-anchor="middle" style="fill:var(--link);font-size:11px">PSYNC ? -1</text><line x1="122" y1="72" x2="558" y2="82" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rsy-a)"/><text x="340.0" y="70.0" text-anchor="middle" style="fill:var(--link);font-size:11px">FULLRESYNC 复制id 偏移量</text><rect x="100" y="92" width="40" height="44" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="150.0" y="118.0" text-anchor="start" style="fill:var(--warn);font-size:11px">等 5 秒，让同时到来的从节点一起同步（实测 4.8 秒）</text><line x1="122" y1="150" x2="558" y2="168" style="stroke:var(--accent);stroke-width:1.5" marker-end="url(#rsy-a)"/><text x="340.0" y="150.0" text-anchor="middle" style="fill:var(--link);font-size:11px">RDB 流（fork 子进程直接写 socket）</text><rect x="545" y="170" width="30" height="20" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="580.0" y="186.0" text-anchor="start" style="fill:var(--muted);font-size:11px">清空、加载</text><line x1="122" y1="206" x2="558" y2="222" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rsy-a)"/><text x="340.0" y="206.0" text-anchor="middle" style="fill:var(--link);font-size:11px">之后的写命令（命令传播）</text><text x="14.0" y="246.0" text-anchor="start" style="fill:var(--muted);font-size:11px">传输 + 加载 100 万个 key：0.5 秒</text>
  </g>
</svg>
</figure>

1. 从节点连上主节点，发送 `PSYNC 复制id 偏移量`，说明自己已经有了哪些数据；
2. 第一次复制，或者从节点落后太多，主节点回复 `FULLRESYNC`：fork 一个子进程生成 RDB，传给从节点（**全量同步**）。从节点清空自己的数据，加载 RDB；
3. 生成和传输 RDB 期间，主节点上新的写命令先放在给这个从节点的缓冲区中，RDB 加载完以后再发过去；
4. 之后，主节点每执行一条写命令，就把它发给所有的从节点（**命令传播**）。从节点每秒用 `REPLCONF ACK 偏移量` 报告自己收到了哪里。

主节点还维护一个**复制积压缓冲区**（`repl-backlog-size`，默认 1 MB），保存最近发出的命令。从节点短暂断开后重连，如果它缺少的部分还在积压缓冲区中，主节点只补发这一段（**部分重同步**），不用重新全量同步。写入量大时，1 MB 往往只够几秒，断开稍久就要全量同步，应该调大。

## 3. 异步复制会丢数据

### 3.1 实测：从节点跟不上时主节点崩溃

同一台机器上，复制几乎是实时的，主节点被杀掉时从节点通常已经收到了所有的命令。真实环境中，网络抖动、从节点在加载数据或者忙于别的事，都会让它落后。用 `docker pause` 让从节点暂停 1 秒来模拟：

```bash
# 客户端不停地向主节点 INCR counter
docker pause rr; sleep 1
docker kill -s KILL rm; docker unpause rr
```

```
最后一次得到确认的值：108245（38084 次/秒）
counter = 71714                    ← 从节点上的值
```

**客户端收到确认的 108245 次写入中，36531 次只存在于主节点的内存和发往从节点的缓冲区中**，主节点一死就没了。如果这时把从节点提升为新的主节点，这些写入就永远丢失了。

### 3.2 WAIT

`WAIT 从节点数 超时毫秒数` 阻塞当前客户端，直到之前的写入被指定数量的从节点确认收到，返回实际确认的从节点数：

```c
r = redisCommand(c, "INCR counter");
/* ... */
r = redisCommand(c, "WAIT 1 1000");
if (r->integer < 1) { /* 超时，没有从节点确认：不能算写入成功 */ }
```

同样的实验：

```
最后一次得到确认的值：22767（7933 次/秒），WAIT 超时 1 次
counter = 22769                    ← 从节点上的值
```

**从节点上的值不小于客户端确认过的值，一次都没丢**。从节点暂停期间，WAIT 超时了 1 次，程序没有把那次写入算作成功；从节点上的 22769 比确认值多 2，是写入已经到达但还没来得及确认的。代价是吞吐量从每秒 38084 次降到 7933 次，每次写入都多了一次等待。

WAIT 不能把 Redis 变成强一致的系统：它只保证从节点**收到**了（不保证持久化），也不能阻止故障转移时选中一个没收到的从节点。它把丢数据的窗口从「复制延迟」缩小到了「极少数情况」。

`min-replicas-to-write` 和 `min-replicas-max-lag` 是另一种保护：在线的、延迟不超过 N 秒的从节点少于指定数量时，主节点拒绝写入，防止一个和所有从节点失联的主节点继续接受注定会丢的写入。

## 4. 不持久化的主节点被自动重启

一个常见的配置：为了性能，主节点关闭持久化，只在从节点上开 RDB/AOF；同时用 systemd、Docker 的重启策略让崩溃的进程自动重启。接着上一个实验：主节点被 kill 以后重新启动：

```
counter = （不存在）                ← 重启后的主节点：没有持久化，是空的
counter = 71714                    ← 从节点：数据还在
主节点启动后 5.07 秒，从节点重新连上：
counter = （不存在）
    从: 07:31:08.026 * Done loading RDB, keys loaded: 0, keys expired: 0.
```

**从节点重新连上它的主节点，全量同步了一个空数据库，自己的数据也被清空了。** 在从节点看来，主节点没有任何问题，只是重启了，它只能以主节点为准。开着持久化的从节点，持久化文件也会被这个空数据库覆盖。

<figure class="diagram">
<svg viewBox="0 0 700 200" role="img" aria-label="不持久化的主节点被自动重启：主节点被杀前从节点有 71714；主节点重启后是空的；5.07 秒后从节点重新连上它，全量同步了一个 0 个 key 的数据库，自己的数据也被清空">
  <defs><marker id="rwp-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rwp-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="40" width="150" height="70" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="95.0" y="63.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">主节点崩溃</text><text x="95.0" y="79.6" text-anchor="middle" style="fill:var(--muted);font-size:11px">主：—</text><text x="95.0" y="95.6" text-anchor="middle" style="fill:var(--muted);font-size:11px">从：71714</text><rect x="192" y="40" width="150" height="70" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="267.0" y="63.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">主节点自动重启</text><text x="267.0" y="79.6" text-anchor="middle" style="fill:var(--muted);font-size:11px">主：空</text><text x="267.0" y="95.6" text-anchor="middle" style="fill:var(--muted);font-size:11px">从：71714</text><line x1="172" y1="75" x2="190" y2="75" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rwp-a)"/><rect x="364" y="40" width="150" height="70" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="439.0" y="63.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">5.07 秒后从节点重连</text><text x="439.0" y="79.6" text-anchor="middle" style="fill:var(--muted);font-size:11px">全量同步</text><text x="439.0" y="95.6" text-anchor="middle" style="fill:var(--muted);font-size:11px">0 个 key</text><line x1="344" y1="75" x2="362" y2="75" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rwp-a)"/><rect x="536" y="40" width="150" height="70" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="611.0" y="63.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">结果</text><text x="611.0" y="79.6" text-anchor="middle" style="fill:var(--muted);font-size:11px">主：空</text><text x="611.0" y="95.6" text-anchor="middle" style="fill:var(--muted);font-size:11px">从：空</text><line x1="516" y1="75" x2="534" y2="75" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rwp-a)"/><text x="20.0" y="150.0" text-anchor="start" style="fill:var(--muted);font-size:11px">从节点只能以主节点为准：主节点「正常地」重启了，它的空数据库就是最新的数据</text><text x="20.0" y="172.0" text-anchor="start" style="fill:var(--danger);font-size:12px">主节点不开持久化时，不能让它自动重启</text>
  </g>
</svg>
</figure>

所以：**主节点关闭持久化时，绝对不能让它自动重启**；或者在它重启之前，先把一个从节点提升为主节点（哨兵就是做这件事的，但重启比哨兵判定故障更快时，同样会出问题）。

## 5. 哨兵：自动故障转移

### 5.1 原理

手动切换主节点：选一个从节点执行 `REPLICAOF NO ONE`，让其他从节点改为复制它，再通知所有的客户端连新的地址。**哨兵**（Sentinel）自动做这些事：

1. **监控**：每个哨兵每秒 PING 主节点和从节点。主节点在 `down-after-milliseconds` 内没有正确回复，这个哨兵认为它**主观下线**（`+sdown`）；
2. **确认**：询问其他哨兵，认为它下线的哨兵数达到 `quorum` 时，判定为**客观下线**（`+odown`）。一个哨兵的网络有问题，不会导致误判；
3. **选举**：哨兵之间用类似 Raft 的方法选出一个领头的哨兵，由它执行故障转移，需要多数哨兵同意；
4. **故障转移**：选一个从节点（优先级、复制偏移量最大、运行 id 最小），提升为主节点，其他从节点改为复制它，旧主节点恢复后也变成从节点；
5. **通知**：客户端连接哨兵，用 `SENTINEL get-master-addr-by-name` 询问当前主节点的地址，也可以订阅切换的消息（`+switch-master`）。

哨兵至少要 3 个，部署在不同的机器上：2 个哨兵时，挂一个就凑不够多数，无法选出领头的哨兵。

### 5.2 搭建

3 个 Redis（`rm` 为主，`rr1`、`rr2` 为从，都开了 AOF），3 个哨兵，配置文件：

```
port 26379
sentinel monitor mymaster 172.18.0.3 6379 2       # 名字、主节点地址、quorum
sentinel down-after-milliseconds mymaster 5000   # 5 秒没有回复就认为主观下线
sentinel failover-timeout mymaster 60000
```

```bash
redis-sentinel /tmp/s.conf
```

哨兵通过主节点自动发现从节点和其他哨兵：

```
ip                     172.18.0.3
num-slaves             2
num-other-sentinels    2
quorum                 2
```

### 5.3 实测：kill -9 主节点

客户端只知道 3 个哨兵的地址，每次连接前先问哨兵谁是主节点；写入失败时，每 100 毫秒重新问一次：

```c
redisReply* r = redisCommand(s, "SENTINEL get-master-addr-by-name mymaster");
/* r->element[0] 是主节点的地址，r->element[1] 是端口 */
```

客户端运行 6 秒后，`docker kill -s KILL rm`：

```
15:31:51.497 kill -9 主节点 rm
  0.00s 哨兵说主节点是 172.18.0.3:6379
  5.00s counter = 149308
  5.87s 写入失败：Server closed the connection，最后一次确认的值 175531
 12.33s 哨兵说主节点是 172.18.0.5:6379
 12.33s 恢复写入，中断了 6.46 秒；新的主节点上 counter = 175532
```

哨兵 s1 的日志：

```
07:31:56.595 # +sdown master mymaster 172.18.0.3 6379
07:31:56.609 # +vote-for-leader cc7bc5588b28619a98e9675db2f27fe1cf24aa57 1
07:31:56.672 # +odown master mymaster 172.18.0.3 6379 #quorum 3/2
07:31:57.783 # +switch-master mymaster 172.18.0.3 6379 172.18.0.5 6379
```

<figure class="diagram">
<svg viewBox="0 0 700 224" role="img" aria-label="哨兵故障转移的时间线：0 秒主节点被杀；5.10 秒哨兵判定主观下线；5.18 秒客观下线并开始选举；6.29 秒新主节点切换完成；客户端写入一共中断 6.46 秒">
  <defs><marker id="rfo-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rfo-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <line x1="40" y1="120" x2="670" y2="120" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="40" y1="116" x2="40" y2="124" style="stroke:var(--border-2);stroke-width:1.2"/><text x="40.0" y="140.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">0s</text><line x1="130" y1="116" x2="130" y2="124" style="stroke:var(--border-2);stroke-width:1.2"/><text x="130.0" y="140.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1s</text><line x1="220" y1="116" x2="220" y2="124" style="stroke:var(--border-2);stroke-width:1.2"/><text x="220.0" y="140.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">2s</text><line x1="310" y1="116" x2="310" y2="124" style="stroke:var(--border-2);stroke-width:1.2"/><text x="310.0" y="140.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">3s</text><line x1="400" y1="116" x2="400" y2="124" style="stroke:var(--border-2);stroke-width:1.2"/><text x="400.0" y="140.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">4s</text><line x1="490" y1="116" x2="490" y2="124" style="stroke:var(--border-2);stroke-width:1.2"/><text x="490.0" y="140.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">5s</text><line x1="580" y1="116" x2="580" y2="124" style="stroke:var(--border-2);stroke-width:1.2"/><text x="580.0" y="140.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">6s</text><line x1="670" y1="116" x2="670" y2="124" style="stroke:var(--border-2);stroke-width:1.2"/><text x="670.0" y="140.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">7s</text><line x1="40.0" y1="66" x2="40.0" y2="120" style="stroke:var(--fg-2);stroke-width:1;stroke-dasharray:3 3"/><text x="40.0" y="60.0" text-anchor="start" style="fill:var(--danger);font-size:11px">kill -9 主节点</text><line x1="499.0" y1="46" x2="499.0" y2="120" style="stroke:var(--fg-2);stroke-width:1;stroke-dasharray:3 3"/><text x="499.0" y="40.0" text-anchor="middle" style="fill:var(--warn);font-size:11px">+sdown</text><line x1="506.2" y1="86" x2="506.2" y2="120" style="stroke:var(--fg-2);stroke-width:1;stroke-dasharray:3 3"/><text x="506.2" y="80.0" text-anchor="middle" style="fill:var(--warn);font-size:11px">+odown，选举</text><line x1="606.1" y1="46" x2="606.1" y2="120" style="stroke:var(--fg-2);stroke-width:1;stroke-dasharray:3 3"/><text x="606.1" y="40.0" text-anchor="middle" style="fill:var(--accent);font-size:11px">+switch-master</text><rect x="40" y="160" width="581.4" height="16" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="40.0" y="194.0" text-anchor="start" style="fill:var(--danger);font-size:11px">客户端写入中断 6.46 秒（客户端每 100 毫秒向哨兵询问一次）</text><text x="40.0" y="214.0" text-anchor="start" style="fill:var(--muted);font-size:11px">大部分时间在等 down-after-milliseconds（5 秒）：确认主节点真的挂了</text>
  </g>
</svg>
</figure>

- 主节点 51.497 秒被杀，**5.1 秒后**（`down-after-milliseconds`）哨兵判定主观下线；
- 77 毫秒后 3 个哨兵都认为它下线了，客观下线，同时投票选出领头的哨兵；
- 又过了 1.1 秒，`rr2`（172.18.0.5）被提升为主节点；
- 客户端一共中断了 **6.46 秒**，大部分花在等待「确认主节点真的挂了」上。`down-after-milliseconds` 设得越小，切换越快，但网络抖动时越容易误判；
- 这一次新主节点上的值是 175532，紧接着最后一次确认的 175531，没有丢失。但第 3 节已经看到，从节点落后时，故障转移同样会丢失已确认的写入。

旧主节点重启后，哨兵把它改成了新主节点的从节点：

```
rm:  role:slave  master_link_status:up  counter=719717
rr1: role:slave  master_link_status:up  counter=719717
rr2: role:master counter=719717
```

它的日志中可以看到哨兵发来的 `REPLICAOF 172.18.0.5:6379`，然后做了一次全量同步。

### 5.4 脑裂

如果主节点没有挂，只是和哨兵之间的网络断了：哨兵选出了新的主节点，旧主节点还在接受和它在同一侧的客户端的写入。网络恢复后，旧主节点变成从节点，**这段时间写到它上面的数据全部被丢弃**。`min-replicas-to-write 1` 加 `min-replicas-max-lag 10` 能限制损失：旧主节点和所有的从节点都失联超过 10 秒后，拒绝写入。

### 5.5 哨兵之外

哨兵解决的是「一个主节点的高可用」，数据量和写入量仍然受一台机器的限制。数据需要分片到多个主节点时，用 **Redis Cluster**：16384 个槽分布在多个主节点上，每个主节点有自己的从节点，节点之间互相检测故障并自动切换，不需要单独的哨兵。

## 6. 常见错误

1. **以为有了从节点就不会丢数据**。复制是异步的，实验中丢了 36531 次已确认的写入。
2. **主节点不开持久化，又配置了自动重启**，重启后的空数据库被同步到所有的从节点。
3. **复制积压缓冲区太小**，从节点断开几秒就要全量同步，主节点频繁 fork。
4. **只部署 2 个哨兵**，或者 3 个哨兵在同一台机器上。
5. **客户端写死了主节点的地址**，故障转移以后仍然连着旧地址。
6. **在从节点上读刚写入的数据**，复制延迟导致读到旧值（和 MySQL 的读写分离一样）。

## 7. 小结

- 复制：第一次全量同步（fork 生成 RDB，默认无盘传输，传输前等 5 秒），之后命令传播；短暂断开时从积压缓冲区部分重同步；
- 复制是异步的：从节点落后时主节点崩溃，已确认的写入会丢失；`WAIT` 让写入等从节点确认，实测不再丢失，吞吐量降到五分之一；
- 不持久化的主节点被重启后，空数据库会覆盖所有的从节点；
- 哨兵：主观下线 → 客观下线 → 选举 → 提升从节点 → 通知客户端；实测 6.46 秒恢复写入；脑裂时旧主节点上的写入会丢失。

## 8. 练习

1. 一个电商把用户的购物车只存在 Redis 中，一主两从加哨兵。一次故障转移以后，部分用户发现购物车里刚加的商品不见了。解释原因，给出改进方法。
2. `down-after-milliseconds` 设为 500 毫秒有什么好处和风险？
3. 为什么哨兵选新主节点时优先选复制偏移量最大的从节点？

<details>
<summary>点击查看答案</summary>

1. 复制是异步的：用户加入购物车的写入在主节点上执行、回复了成功，还没传到从节点，主节点就挂了；哨兵提升的从节点上没有这些写入，它们就丢失了（实验中丢了 36531 次）。改进：重要的写入之后执行 `WAIT 1 超时`，等从节点确认，超时就提示失败；设置 `min-replicas-to-write`，防止孤立的主节点继续接受写入；或者购物车的权威数据放在 MySQL 中，Redis 只做缓存。

2. 好处：主节点挂掉以后切换更快，写入中断的时间从 5 秒以上缩短到 1 秒多。风险：网络短暂抖动、主节点执行一条慢命令（比如第 2 篇中 109 毫秒的 `KEYS *`、几百毫秒的 fork）、虚拟机暂停，都可能让哨兵误判主节点下线，触发不必要的故障转移；每次切换都可能丢数据、让客户端断开重连，频繁切换反而降低了可用性。

3. 复制偏移量表示这个从节点收到了主节点的多少数据，偏移量最大的从节点最接近旧主节点，提升它丢失的写入最少。其他从节点改为复制它时，如果它们的数据是它的前缀，还可能只做部分重同步。

</details>
