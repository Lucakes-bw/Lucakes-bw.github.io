---
title: 单线程、Pipeline 与慢命令
date: 2026-11-07 10:10:00
updated: 2026-11-07 10:10:00
categories:
  - 数据库
  - Redis
tags:
  - 数据库
  - Redis
  - 性能
order: 2
description: Redis 用一个线程执行所有的命令。这让它简单、没有锁，也带来两个后果：一次请求的时间大部分是网络往返，用 Pipeline 把 1000 条命令一起发出去，吞吐量从每秒 5 万次涨到 300 万次；往返 1.4 毫秒时，逐条请求每秒只有 719 次。另一个后果是一条慢命令会挡住所有人：100 万个 key 时 KEYS * 让别的客户端等了 109 毫秒，删除 100 万个字段的哈希等了 65 毫秒；换成 SCAN 和 UNLINK 以后都不到 2 毫秒。
top_group_list: true
disableNunjucks: true
viz: [db-redis]
---

> **本文目标**：理解 Redis 的单线程事件循环；会用 Pipeline 和批量命令减少网络往返；知道哪些命令是慢命令，会用 SCAN、UNLINK 代替 KEYS、DEL，会用 SLOWLOG 找出慢命令。
>
> **前置知识**：上一篇；《计算机网络》专栏的[《从 epoll 到 Reactor：事件循环》](/posts/srv-reactor/)。
>
> **实验环境**：同上一篇。Redis 和客户端在同一台虚拟机的两个容器中，往返时间约 18 微秒；第 2.3 节用 `tc netem` 人为增加了延迟。

## 1. 一个线程处理所有的客户端

Redis 服务器的主循环就是[《从 epoll 到 Reactor：事件循环》](/posts/srv-reactor/)中的 Reactor：

<figure class="diagram">
<svg viewBox="0 0 700 230" role="img" aria-label="Redis 的事件循环：一个线程反复执行 epoll_wait 等待、读出命令、依次执行、写回回复、处理定时任务。所有客户端的命令都由这一个线程依次执行">
  <defs><marker id="rlp-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rlp-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="30" width="90" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="65.0" y="51.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">客户端 A</text><line x1="110" y1="47" x2="196" y2="107" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#rlp-a)"/><rect x="20" y="90" width="90" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="65.0" y="111.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">客户端 B</text><line x1="110" y1="107" x2="196" y2="107" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#rlp-a)"/><rect x="20" y="150" width="90" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="65.0" y="171.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">客户端 C</text><line x1="110" y1="167" x2="196" y2="107" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#rlp-a)"/><rect x="200" y="90" width="120" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="260.0" y="112.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">epoll_wait</text><rect x="330" y="30" width="120" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="390.0" y="52.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">读命令</text><rect x="470" y="90" width="140" height="36" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="540.0" y="112.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">执行（单线程）</text><rect x="330" y="160" width="120" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="390.0" y="182.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">写回复</text><line x1="290" y1="90" x2="328" y2="52" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rlp-a)"/><line x1="450" y1="48" x2="530" y2="88" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rlp-a)"/><line x1="540" y1="126" x2="452" y2="172" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rlp-a)"/><line x1="330" y1="172" x2="270" y2="128" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rlp-a)"/><text x="560.0" y="160.0" text-anchor="start" style="fill:var(--accent);font-size:11px">同一时刻只执行一条命令</text><text x="560.0" y="176.0" text-anchor="start" style="fill:var(--muted);font-size:11px">每条命令都是原子的</text><text x="200.0" y="220.0" text-anchor="start" style="fill:var(--muted);font-size:11px">每一轮还要处理定时任务（过期 key 清理、统计……）</text>
  </g>
</svg>
</figure>

1. `epoll_wait` 等待：哪些客户端的连接上有数据可读；
2. 读出命令，**依次执行**，把回复放进这个客户端的输出缓冲区；
3. 把输出缓冲区写回 socket；
4. 处理定时任务（过期 key 的清理、统计……），回到第 1 步。

执行命令的始终是同一个线程。这样做的好处：

- **没有锁**。哈希表、跳表都不需要考虑并发修改，代码简单，也没有加锁的开销；
- **每条命令都是原子的**（上一篇的 INCR）；
- **够快**。数据都在内存中，一条简单命令只要零点几微秒，瓶颈在网络和系统调用，不在 CPU。

Redis 6 以后可以配置 I/O 线程（`io-threads`，本文的环境是默认的 1），让多个线程负责读写 socket、解析协议，但**执行命令仍然是一个线程**，上面的结论不变。

单线程的代价也很明显：**一条命令执行期间，其他所有的命令都在排队。** 第 3 节实测。

<div class="dsviz" data-viz="redis-loop"></div>

## 2. Pipeline：减少往返

### 2.1 逐条请求的时间花在哪里

上一篇看到，服务器执行一次 GET 只要 0.21 微秒，客户端却要等 19 微秒。每一次请求都要：客户端 `write` → 网络 → 服务器 `epoll_wait` 醒来、`read`、执行、`write` → 网络 → 客户端 `read`。客户端发出一条命令以后什么都不做，就在等回复。

**Pipeline**（管道）的做法是：一次把很多条命令都写出去，再依次读回复。服务器按顺序执行，回复的顺序和命令一致。

<figure class="diagram">
<svg viewBox="0 0 700 250" role="img" aria-label="逐条请求和 Pipeline 的时间线。逐条请求时，每条命令都要等一次完整的往返才能发下一条；Pipeline 把多条命令一起发出去，再一起读回复，三条命令只用一次往返">
  <defs><marker id="rpp-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rpp-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="14.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">逐条请求</text><text x="14.0" y="40.0" text-anchor="start" style="fill:var(--muted);font-size:11px">客户端</text><text x="14.0" y="100.0" text-anchor="start" style="fill:var(--muted);font-size:11px">服务器</text><line x1="70" y1="36" x2="680" y2="36" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="70" y1="96" x2="680" y2="96" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="80" y1="36" x2="140" y2="94" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rpp-a)"/><text x="100.0" y="60.0" text-anchor="middle" style="fill:var(--link);font-size:11px">GET 1</text><rect x="140" y="90" width="8" height="8" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><line x1="148" y1="94" x2="208" y2="38" style="stroke:var(--accent);stroke-width:1.5" marker-end="url(#rpp-a)"/><line x1="270" y1="36" x2="330" y2="94" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rpp-a)"/><text x="290.0" y="60.0" text-anchor="middle" style="fill:var(--link);font-size:11px">GET 2</text><rect x="330" y="90" width="8" height="8" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><line x1="338" y1="94" x2="398" y2="38" style="stroke:var(--accent);stroke-width:1.5" marker-end="url(#rpp-a)"/><line x1="460" y1="36" x2="520" y2="94" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rpp-a)"/><text x="480.0" y="60.0" text-anchor="middle" style="fill:var(--link);font-size:11px">GET 3</text><rect x="520" y="90" width="8" height="8" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><line x1="528" y1="94" x2="588" y2="38" style="stroke:var(--accent);stroke-width:1.5" marker-end="url(#rpp-a)"/><text x="650.0" y="72.0" text-anchor="end" style="fill:var(--danger);font-size:11px">3 次往返</text><text x="14.0" y="142.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Pipeline</text><text x="14.0" y="164.0" text-anchor="start" style="fill:var(--muted);font-size:11px">客户端</text><text x="14.0" y="224.0" text-anchor="start" style="fill:var(--muted);font-size:11px">服务器</text><line x1="70" y1="160" x2="680" y2="160" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="70" y1="220" x2="680" y2="220" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="80" y1="160" x2="140" y2="218" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rpp-a)"/><line x1="94" y1="160" x2="154" y2="218" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rpp-a)"/><line x1="108" y1="160" x2="168" y2="218" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rpp-a)"/><rect x="142" y="214" width="8" height="8" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><rect x="156" y="214" width="8" height="8" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><rect x="170" y="214" width="8" height="8" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><line x1="186" y1="218" x2="246" y2="162" style="stroke:var(--accent);stroke-width:1.5" marker-end="url(#rpp-a)"/><line x1="200" y1="218" x2="260" y2="162" style="stroke:var(--accent);stroke-width:1.5" marker-end="url(#rpp-a)"/><line x1="214" y1="218" x2="274" y2="162" style="stroke:var(--accent);stroke-width:1.5" marker-end="url(#rpp-a)"/><text x="110.0" y="186.0" text-anchor="start" style="fill:var(--link);font-size:11px">GET 1、2、3</text><text x="320.0" y="196.0" text-anchor="start" style="fill:var(--accent);font-size:11px">1 次往返：回复的顺序和命令一致</text><text x="650.0" y="244.0" text-anchor="end" style="fill:var(--muted);font-size:11px">实测：每批 1000 条，快 60 倍</text>
  </g>
</svg>
</figure>

### 2.2 实测

hiredis 中，`redisAppendCommand` 只把命令写进发送缓冲区，第一次 `redisGetReply` 时才真正发出去：

```c
for (int i = 0; i < N; i += batch) {
    int n = i + batch <= N ? batch : N - i;
    for (int k = 0; k < n; ++k) redisAppendCommand(c, "GET user:%d", i + k);   // 只写入发送缓冲区
    for (int k = 0; k < n; ++k) {                                              // 第一次读时才真正发出去
        redisReply* r;
        CHECK(redisGetReply(c, (void**)&r) == REDIS_OK);
        /* 检查回复和命令按顺序一一对应 */
        freeReplyObject(r);
    }
}
```

读 10 万个 key，每批的条数不同：

```
读 100000 个 key
  Pipeline 每批     1 条： 1.907 秒，   52433 次/秒
  Pipeline 每批    10 条： 0.233 秒，  429009 次/秒
  Pipeline 每批   100 条： 0.055 秒， 1821857 次/秒
  Pipeline 每批  1000 条： 0.032 秒， 3126835 次/秒
  Pipeline 每批 10000 条： 0.029 秒， 3473166 次/秒
  MGET 每次 100 个 key：  0.034 秒， 2910023 个/秒
```

每批 10 条，快了 8 倍；每批 1000 条，**每秒 312 万次，是逐条请求的 60 倍**。批量再大，收益就很小了：这时瓶颈已经不是往返，而是服务器执行命令和双方处理数据的时间。

`MGET`（一条命令读多个 key）效果和 Pipeline 差不多。区别是：`MGET`、`MSET`、`HMGET` 只能对同一种操作批量；Pipeline 可以把任意命令放在一起。

### 2.3 网络延迟越大，差距越大

同一台机器上的往返只有 18 微秒。应用服务器和 Redis 在不同的机房时，往返可能是 1 毫秒甚至更多。用 `tc netem` 给客户端的网卡加上 1 毫秒的发送延迟：

```bash
tc qdisc add dev eth0 root netem delay 1ms
```

`redis-cli --latency` 测得平均往返 1.42 毫秒。再读 1 万个 key：

```
读 10000 个 key
  Pipeline 每批     1 条：13.902 秒，     719 次/秒
  Pipeline 每批    10 条： 1.424 秒，    7022 次/秒
  Pipeline 每批   100 条： 0.149 秒，   66981 次/秒
  Pipeline 每批  1000 条： 0.036 秒，  280474 次/秒
  Pipeline 每批 10000 条： 0.019 秒，  537899 次/秒
  MGET 每次 100 个 key：  0.143 秒，   70160 个/秒
```

**逐条请求每秒只有 719 次**，一个循环读 1 万个 key 要 14 秒，而服务器几乎一直空闲。每批 1000 条，快了 390 倍。这就是为什么「在循环中逐个调用 Redis」是最常见的性能问题之一。

### 2.4 Pipeline 不是事务

- Pipeline 只是**减少往返**。一批命令在服务器上不一定连续执行，中间可能插进别的客户端的命令；
- 某一条命令出错，不影响其他命令，也没有回滚；
- 一批不要太大：所有的回复都要放在服务器的输出缓冲区和客户端的内存中，几万条一批就足够了。

需要一批命令连续执行，用 `MULTI`/`EXEC`（它们也可以放进一个 Pipeline 中发出去）；需要根据中间结果做判断，用 Lua 脚本。

## 3. 慢命令会挡住所有人

### 3.1 实验

准备 100 万个 key。一个线程每隔 1 毫秒 `PING` 一次，记下每次的延迟；同时，另一个连接执行各种命令：

```c
static void* monitor(void* arg)
{
    redisContext* c = connectRedis();
    while (atomic_load(&g_running)) {
        double a = nowSeconds();
        freeReplyObject(cmd(c, "PING"));
        double lat = nowSeconds() - a;
        if (lat > g_maxLat) g_maxLat = lat;
        if (lat > 0.010) ++g_slow;
        ++g_pings;
        usleep(1000);
    }
    redisFree(c);
    return NULL;
}
```

```
准备了 1000000 个 key
（空闲，对照）              用时      0.0 毫秒；同时 PING  284 次，最慢     0.5 毫秒，超过 10 毫秒的 0 次
KEYS *                             用时    147.0 毫秒；同时 PING  324 次，最慢   109.0 毫秒，超过 10 毫秒的 1 次
SCAN COUNT 1000（1000 次调用） 用时    181.1 毫秒；同时 PING  440 次，最慢     1.1 毫秒，超过 10 毫秒的 0 次
  SCAN 一共返回 1000000 个 key（可能有重复，这里没有）
DEL 100 万个字段的哈希      用时     65.6 毫秒；同时 PING  290 次，最慢    65.1 毫秒，超过 10 毫秒的 1 次
UNLINK 100 万个字段的哈希   用时      0.1 毫秒；同时 PING  294 次，最慢     0.3 毫秒，超过 10 毫秒的 0 次
SLOWLOG 中最近的慢命令：
  DEL         65437 微秒
  KEYS       109250 微秒
```

<figure class="diagram">
<svg viewBox="0 0 700 200" role="img" aria-label="一个线程每毫秒 PING 一次，另一个连接执行各种命令时，PING 的最大延迟：空闲 0.5 毫秒，KEYS * 109.0 毫秒，SCAN 1.1 毫秒，DEL 大哈希 65.1 毫秒，UNLINK 大哈希 0.3 毫秒">
  <defs><marker id="rbk-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rbk-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="140.0" y="34.0" text-anchor="end" style="fill:var(--fg);font-size:12px">空闲（对照）</text><rect x="150" y="20" width="2.1" height="18" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="160.1" y="34.0" text-anchor="start" style="fill:var(--muted);font-size:11px">0.5 毫秒</text><text x="140.0" y="62.0" text-anchor="end" style="fill:var(--fg);font-size:12px">KEYS *</text><rect x="150" y="48" width="457.8" height="18" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="615.8" y="62.0" text-anchor="start" style="fill:var(--muted);font-size:11px">109.0 毫秒</text><text x="140.0" y="90.0" text-anchor="end" style="fill:var(--fg);font-size:12px">SCAN COUNT 1000</text><rect x="150" y="76" width="4.6" height="18" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="162.6" y="90.0" text-anchor="start" style="fill:var(--muted);font-size:11px">1.1 毫秒</text><text x="140.0" y="118.0" text-anchor="end" style="fill:var(--fg);font-size:12px">DEL 大哈希</text><rect x="150" y="104" width="273.4" height="18" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="431.4" y="118.0" text-anchor="start" style="fill:var(--muted);font-size:11px">65.1 毫秒</text><text x="140.0" y="146.0" text-anchor="end" style="fill:var(--fg);font-size:12px">UNLINK 大哈希</text><rect x="150" y="132" width="2.0" height="18" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="160.0" y="146.0" text-anchor="start" style="fill:var(--muted);font-size:11px">0.3 毫秒</text><text x="150.0" y="192.0" text-anchor="start" style="fill:var(--muted);font-size:11px">100 万个 key；大哈希有 100 万个字段。单线程：一条慢命令执行时，别人的命令都在排队</text>
  </g>
</svg>
</figure>

### 3.2 KEYS 和 SCAN

`KEYS *` 在服务器上执行了 109 毫秒（SLOWLOG 记录的是执行时间，客户端看到的 147 毫秒还包括传输 100 万个 key 的时间）。这 109 毫秒中，**`PING` 这个最简单的命令也等了 109 毫秒**。如果这是线上的服务，每秒几万个请求全部卡住 0.1 秒；key 有几千万个时，就是几秒钟的停顿。

`SCAN` 把遍历拆成很多小步：每次传入一个游标，服务器只遍历哈希表的一小部分（`COUNT` 是建议的数量），返回一批 key 和下一个游标，游标回到 0 时结束：

```c
long long cursor = 0;
do {
    r = cmd(c, "SCAN %lld COUNT 1000", cursor);
    cursor = atoll(r->element[0]->str);
    /* r->element[1] 是这一批 key */
    freeReplyObject(r);
} while (cursor != 0);
```

总时间反而长一些（1000 次往返），但每次只占用服务器一小会儿，`PING` 最慢只有 1.1 毫秒。代价是：遍历期间有增删时，同一个 key 可能返回两次（哈希表扩容时），调用方要能处理重复。`HSCAN`、`SSCAN`、`ZSCAN` 用同样的方法遍历一个大的哈希、集合、有序集合。

### 3.3 DEL 和 UNLINK

删除一个有 100 万个字段的哈希，要释放 100 万个字段和值的内存，`DEL` 在主线程中做完这件事，用了 65 毫秒，期间所有的命令都在等。

`UNLINK` 只把 key 从数据库中摘下来（这一步是 O(1)），真正释放内存的工作交给后台线程。客户端 0.1 毫秒就得到了回复，`PING` 没有受到任何影响。

本文环境中 `lazyfree-lazy-user-del` 的值是 `no`，所以 `DEL` 仍然在主线程中释放内存；设为 `yes` 以后，`DEL` 的行为和 `UNLINK` 一样。过期、淘汰、`FLUSHALL` 也有对应的 `lazyfree-*` 配置。

### 3.4 常见的慢命令

| 命令 | 复杂度 | 代替 |
|---|---|---|
| `KEYS pattern` | O(所有 key) | `SCAN` |
| `HGETALL`、`SMEMBERS`、`LRANGE 0 -1`（大集合） | O(元素个数) | `HSCAN`、`SSCAN`，或者只取需要的部分 |
| `DEL` 大 key | O(元素个数) | `UNLINK` |
| `SORT`、`SUNION`、`ZUNIONSTORE`（大集合） | O(n log n) 或更高 | 在从节点上执行，或者在应用中分批做 |
| `FLUSHALL`、`FLUSHDB` | O(所有 key) | `FLUSHALL ASYNC` |
| 执行时间长的 Lua 脚本 | 脚本执行期间独占 | 控制每次处理的数量 |

一个元素特别多（几十万个以上）或者特别大（几 MB 以上）的 key 叫**大 key**：读写它很慢，删除它会阻塞，复制和迁移它也很麻烦。`redis-cli --bigkeys`（内部用 SCAN）可以找出每种类型最大的 key。

### 3.5 SLOWLOG

执行时间超过 `slowlog-log-slower-than`（默认 10000 微秒）的命令会被记进慢日志：

```
SLOWLOG GET 10      # 最近 10 条
SLOWLOG RESET
```

每条记录有执行的时间点、耗时（微秒）、命令和参数、客户端地址。注意它只记**执行时间**，不包括排队等待和网络传输：被 `KEYS` 挡住了 109 毫秒的 `PING` 不会出现在慢日志中。

## 4. 常见错误

1. **在循环中逐条调用 Redis**。往返 1.4 毫秒时，每秒只能做 719 次。用 Pipeline、`MGET`/`MSET`。
2. **一个 Pipeline 塞进几百万条命令**，回复占满了服务器的输出缓冲区和客户端的内存。分批，每批几百到几千条。
3. **把 Pipeline 当成事务**。一批命令中间可能插进别的命令，出错也不会回滚。
4. **线上执行 `KEYS *`、`HGETALL` 大哈希、`DEL` 大 key**。单线程，一条慢命令挡住所有人。
5. **以为 SLOWLOG 是空的就没有问题**。慢日志只记录执行时间超过阈值的命令，被它们挡住的请求、网络慢、客户端慢都不会记录。
6. **用 `SCAN` 却假设不会有重复的 key**。

## 5. 小结

- Redis 用一个线程执行所有的命令（事件循环），没有锁，每条命令都是原子的；
- 一次请求的时间主要是网络往返：同机 18 微秒、跨机房毫秒级。Pipeline 每批 1000 条，吞吐量是逐条请求的 60 倍（往返 1.4 毫秒时是 390 倍）；
- 单线程的代价是一条慢命令挡住所有人：`KEYS *` 让 PING 等了 109 毫秒，`DEL` 大哈希等了 65 毫秒；
- 用 `SCAN` 代替 `KEYS`，用 `UNLINK` 代替 `DEL`，用 `SLOWLOG`、`--bigkeys` 找出慢命令和大 key。

## 6. 练习

1. 一个接口要读 200 个商品的缓存，每个商品是一个哈希，要读其中 3 个字段。怎样只用一次往返完成？
2. 删除所有以 `session:` 开头的 key（有几百万个），怎样做才不会影响线上服务？
3. 为什么 Redis 选择单线程执行命令，而 MySQL 每个连接一个线程？

<details>
<summary>点击查看答案</summary>

1. 把 200 条 `HMGET product:id name price stock` 放进一个 Pipeline，一次发出，再依次读 200 个回复。`MGET` 只能读字符串，不能读哈希的字段，所以这里用 Pipeline。

2. 用 `SCAN 0 MATCH session:* COUNT 1000` 分批遍历，每批得到的 key 用 `UNLINK` 删除（多个 key 可以放在一条 `UNLINK` 中），直到游标回到 0。每一步只占用服务器很短的时间，内存在后台线程中释放。不要用 `KEYS session:*` 一次取出所有的 key。

3. Redis 的数据在内存中，一条命令通常只要几微秒以内，执行命令几乎不需要等待（没有磁盘 IO、没有锁等待），一个线程就能把 CPU 用满；多线程反而要给所有的数据结构加锁。MySQL 的一条查询可能要读磁盘、等行锁、执行复杂的连接和排序，耗时从微秒到秒不等，如果只有一个线程，一条慢查询就会让所有人等待，所以要用多个线程并发执行。

</details>
