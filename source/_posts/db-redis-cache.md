---
title: 实战：用 Redis 给 MySQL 做缓存
date: 2026-11-07 11:00:00
updated: 2026-11-07 11:00:00
categories:
  - 数据库
  - Redis
tags:
  - 数据库
  - Redis
  - MySQL
  - 缓存
order: 7
description: 用 C 实现旁路缓存：先读 Redis，未命中再查 MySQL 并写回。实测：单线程逐个读时总时间只缩短了 28%，但数据库的查询从 20 万次降到 3.4 万次；8 个线程并发读时，吞吐量从每秒 6.6 万次翻倍到 13.4 万次。然后逐个复现缓存的经典问题：热点 key 过期，50 个线程查了 200 次数据库，加互斥锁以后 4 次；不存在的 id 查了 10 万次数据库，缓存空值以后 100 次；1 万个 key 同时过期，0.25 秒内查了 3366 次数据库，过期时间加上随机值以后最多 671 次；最后复现「先改数据库再删缓存」仍然留下旧值的竞争。
top_group_list: true
disableNunjucks: true
viz: [db-redis]
---

> **本文目标**：会实现旁路缓存（Cache-Aside）；能量化缓存带来的好处；理解并能解决缓存击穿、穿透、雪崩；理解缓存和数据库之间的一致性问题，知道「先更新数据库再删除缓存」和延迟双删。
>
> **前置知识**：本章前几篇；本专栏的[《用 C 访问 MySQL》](/posts/db-c-api/)。
>
> **实验环境**：Redis 8.10.3、MySQL 8.4.13，各在一个容器中；程序在第三个容器中，同时连接两者。数据：10 万个商品，每个商品 10 条评论（共 100 万条）。

## 1. 旁路缓存

### 1.1 要缓存的查询

商品详情页要显示名字、价格、评论数和平均评分：

```sql
SELECT p.name, p.price, COUNT(r.id), IFNULL(ROUND(AVG(r.stars), 2), 0)
FROM product p LEFT JOIN review r ON r.product_id = p.id
WHERE p.id = ? GROUP BY p.id
```

`review.product_id` 上有索引，这条查询并不慢，但每次都要连接、聚合。商品详情的读远多于写，适合缓存。

### 1.2 读：先缓存，后数据库

<figure class="diagram">
<svg viewBox="0 0 700 210" role="img" aria-label="旁路缓存的读和写。读：先查 Redis，命中直接返回；未命中查 MySQL，把结果写回 Redis 并设置过期时间。写：先更新 MySQL，再删除 Redis 中的缓存">
  <defs><marker id="ras-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="ras-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="30" y="80" width="90" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="75.0" y="104.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">应用</text><rect x="320" y="20" width="120" height="40" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="380.0" y="44.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">Redis</text><rect x="320" y="140" width="120" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="380.0" y="164.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">MySQL</text><line x1="120" y1="90" x2="318" y2="40" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ras-a)"/><text x="200.0" y="56.0" text-anchor="middle" style="fill:var(--link);font-size:11px">① GET</text><line x1="120" y1="108" x2="318" y2="160" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ras-a)"/><text x="190.0" y="150.0" text-anchor="middle" style="fill:var(--link);font-size:11px">② 未命中：SELECT</text><line x1="140" y1="96" x2="318" y2="52" style="stroke:var(--accent);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#ras-a)"/><text x="230.0" y="82.0" text-anchor="start" style="fill:var(--accent);font-size:11px">③ SET … EX</text><text x="470.0" y="40.0" text-anchor="start" style="fill:var(--fg);font-size:12px">读：命中就返回（大多数请求）</text><text x="470.0" y="58.0" text-anchor="start" style="fill:var(--muted);font-size:11px">未命中：查库，写回缓存</text><text x="470.0" y="150.0" text-anchor="start" style="fill:var(--fg);font-size:12px">写：① UPDATE MySQL</text><text x="470.0" y="168.0" text-anchor="start" style="fill:var(--fg);font-size:12px">      ② DEL Redis 中的 key</text><text x="470.0" y="188.0" text-anchor="start" style="fill:var(--muted);font-size:11px">（删除，而不是更新）</text>
  </g>
</svg>
</figure>

```c
static int getProduct(redisContext* c, MYSQL* m, int id, int ttl, int emptyTtl, char* out, size_t n, int* hit)
{
    redisReply* r = cmd(c, "GET product:%d", id);
    if (r->type == REDIS_REPLY_STRING) {
        *hit = 1;
        int found = r->len > 0;                  // 空字符串表示「数据库中没有」
        snprintf(out, n, "%s", r->str);
        freeReplyObject(r);
        return found;
    }
    freeReplyObject(r);
    *hit = 0;
    int found = dbLoad(m, id, out, n);
    if (found) freeReplyObject(cmd(c, "SET product:%d %s EX %d", id, out, ttl));
    else if (emptyTtl > 0) freeReplyObject(cmd(c, "SET product:%d %s EX %d", id, "", emptyTtl));
    return found;
}
```

1. 先 `GET`，命中就直接返回；
2. 未命中，查数据库；
3. 把结果写进缓存，**带上过期时间**，然后返回。

写的时候：**先更新数据库，再删除缓存**（不是更新缓存），下次读的时候自然会加载新值。第 5 节说明为什么这样做，以及它仍然有的漏洞。

这叫**旁路缓存**（Cache-Aside）：应用自己负责读缓存、查数据库、写缓存，Redis 和 MySQL 互不知道对方。

<div class="dsviz" data-viz="redis-cache"></div>

### 1.3 实测：快了多少

按齐夫分布（和第 4 篇一样，少数商品很热门）读 20 万次。MySQL 的数据都已经在缓冲池中：

```
只用 MySQL：      200000 次读 9.50 秒，平均  47.5 微秒，查数据库 200000 次
Redis + MySQL：   200000 次读 6.80 秒，平均  34.0 微秒，查数据库 34224 次，命中率 82.9%
抽查 1000 个：缓存中的值和数据库一致
```

单线程逐个读，**总时间只缩短了 28%**。原因在第 1 篇已经看到：数据都在内存中时，一次 MySQL 查询和一次 Redis 请求的耗时都以网络往返为主；而未命中时要多一次 GET 和一次 SET，反而更慢。

但看另一个数字：**数据库只被查了 34224 次，减少了 83%**。数据库的 CPU 是有限的，请求并发时，这才是关键。8 个线程同时读：

```
8 个线程，只用 MySQL  ：20 万次读 3.05 秒， 65566 次/秒，查数据库 200000 次
8 个线程，Redis + MySQL：20 万次读 1.50 秒，133704 次/秒，查数据库 34231 次
```

**吞吐量翻了一倍**。只用 MySQL 时，2 核的虚拟机已经被 MySQL 的查询解析和执行占满；加了缓存，大部分请求只需要 Redis 花零点几微秒处理。查询越复杂（多表连接、排序、聚合，动辄几毫秒），缓存的效果越明显。

## 2. 缓存击穿：热点 key 过期

一个非常热门的 key（秒杀商品、热搜）过期的那一刻，大量请求同时发现缓存未命中，**同时去查数据库**，同时写回缓存。

实验：50 个线程不停地读同一个商品，缓存 2 秒过期；为了让效果明显，查询中加了 `SLEEP(0.05)`，模拟一次 50 毫秒的复杂查询。运行 7 秒，缓存会过期 3 次左右：

```
50 个线程读同一个热点 key 7 秒（缓存 2 秒过期，查一次数据库 50 毫秒），不加锁  ：查数据库 200 次
```

<figure class="diagram">
<svg viewBox="0 0 700 150" role="img" aria-label="缓存击穿：50 个线程读同一个热点 key 7 秒，缓存 2 秒过期，每次查询 50 毫秒。不加锁时查了 200 次数据库，加互斥锁以后只查了 4 次">
  <defs><marker id="rst-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rst-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="160.0" y="46.0" text-anchor="end" style="fill:var(--fg);font-size:12px">不加锁</text><rect x="170" y="24" width="440.0" height="26" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="618.0" y="46.0" text-anchor="start" style="fill:var(--muted);font-size:11px">200 次</text><text x="160.0" y="86.0" text-anchor="end" style="fill:var(--fg);font-size:12px">互斥锁（SET NX PX）</text><rect x="170" y="64" width="8.8" height="26" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="186.8" y="86.0" text-anchor="start" style="fill:var(--muted);font-size:11px">4 次</text><text x="170.0" y="134.0" text-anchor="start" style="fill:var(--muted);font-size:11px">每次过期，不加锁时几十个线程同时查数据库；加锁后只有拿到锁的那一个</text>
  </g>
</svg>
</figure>

**本来过期一次只需要查一次数据库，实际查了 200 次**：每次过期，50 个线程几乎全部扑向了数据库（这 50 毫秒中又有线程读完、再来读，还是未命中）。如果这是一条要 1 秒的查询，数据库会在这一刻被压垮。

解决办法：**未命中时，只让一个线程去查数据库**。用 Redis 本身做一把互斥锁：

```c
r = cmd(c, "SET lock:product:0 1 NX PX 3000");   // 锁带过期时间：拿到锁的线程崩溃了，锁也会自动释放
int got = r->type == REDIS_REPLY_STATUS;
freeReplyObject(r);
if (got) {
    dbLoad(m, 0, buf, n);
    freeReplyObject(cmd(c, "SET product:0 %s EX 2", buf));
    freeReplyObject(cmd(c, "DEL lock:product:0"));
    return;
}
usleep(10000);                                  // 别人正在查，10 毫秒后再看缓存
```

`SET … NX` 只在 key 不存在时设置成功，原子地实现了「抢锁」。

```
50 个线程读同一个热点 key 7 秒（缓存 2 秒过期，查一次数据库 50 毫秒），加互斥锁：查数据库 4 次
```

**200 次变成了 4 次**，每次过期正好一次。代价是其他线程要等那 50 毫秒。

其他做法：

- **热点 key 不设过期时间**，由后台任务定期刷新；
- **逻辑过期**：缓存的值中带一个过期时间，过期以后先返回旧值，同时由一个线程在后台刷新。用户永远不用等，代价是短时间内读到旧值。

注意示例中释放锁的写法是简化的：如果查询超过了锁的 3 秒有效期，锁已经自动释放、被别的线程拿到，`DEL` 就删掉了别人的锁。严谨的做法是锁的值存一个随机数，释放时用 Lua 脚本「值相同才删除」。

## 3. 缓存穿透：查询不存在的数据

查一个数据库中不存在的 id：缓存中没有，数据库中也没有，什么都不会写进缓存，**下一次还是直接查数据库**。恶意的请求可以用不存在的 id 绕过缓存，打到数据库上：

```
反复查询 100 个不存在的 id，共 10 万次，不缓存空值   ：查数据库 100000 次
反复查询 100 个不存在的 id，共 10 万次，缓存空值 60 秒：查数据库 100 次
```

**缓存空值**：数据库中没有，也写一个特殊的值（这里是空字符串）进缓存，过期时间短一些（之后这个 id 可能被创建）。10 万次查询只查了 100 次数据库，每个 id 一次。

如果攻击者每次都用新的随机 id，缓存空值也没用，反而会塞满缓存。这时可以：

- 先校验参数（id 的格式、范围）；
- 用**布隆过滤器**记下所有存在的 id：它说「不存在」就一定不存在，直接返回；说「存在」时可能误判（概率可以控制在 1% 以下），再走正常的流程。Redis 8 内置了布隆过滤器（`BF.ADD`、`BF.EXISTS`）。

## 4. 缓存雪崩：大量 key 同时过期

服务启动时把 1 万个商品一起加载进缓存，过期时间都是 5 秒（实际一般是几十分钟到几小时，这里缩短以便观察）。8 个线程随机读这 1 万个商品，统计每 0.25 秒查了多少次数据库：

```
过期时间都是 5 秒：每 0.25 秒查数据库的次数
  0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 2749 3298 3366 620 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
  一共 10033 次，最多的 0.25 秒中 3366 次
过期时间 3～7 秒随机：每 0.25 秒查数据库的次数
  0 0 0 0 0 0 0 0 0 87 323 449 649 572 636 599 633 600 605 615 671 643 626 616 640 545 372 146 4 0 0 0 0 0 0 0 0 0 0 0
  一共 10031 次，最多的 0.25 秒中 671 次
```

<figure class="diagram">
<svg viewBox="0 0 700 250" role="img" aria-label="缓存雪崩：1 万个 key 的过期时间都是 5 秒时，每 0.25 秒查数据库的次数在 1 秒内冲到 3366 次；过期时间在 3 到 7 秒之间随机时，分散在 4 秒多的时间里，最多 671 次">
  <defs><marker id="rav-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rav-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="40.0" y="30.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">过期时间都是 5 秒：峰值 3366</text><line x1="40" y1="120" x2="640" y2="120" style="stroke:var(--border-2);stroke-width:1.2"/><rect x="295" y="54.7" width="12" height="65.3" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><rect x="310" y="41.6" width="12" height="78.4" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><rect x="325" y="40.0" width="12" height="80.0" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><rect x="340" y="105.3" width="12" height="14.7" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="40.0" y="140.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">3～7 秒随机：峰值 671</text><line x1="40" y1="230" x2="640" y2="230" style="stroke:var(--border-2);stroke-width:1.2"/><rect x="175" y="227.9" width="12" height="2.1" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="190" y="222.3" width="12" height="7.7" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="205" y="219.3" width="12" height="10.7" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="220" y="214.6" width="12" height="15.4" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="235" y="216.4" width="12" height="13.6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="250" y="214.9" width="12" height="15.1" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="265" y="215.8" width="12" height="14.2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="280" y="215.0" width="12" height="15.0" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="295" y="215.7" width="12" height="14.3" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="310" y="215.6" width="12" height="14.4" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="325" y="215.4" width="12" height="14.6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="340" y="214.1" width="12" height="15.9" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="355" y="214.7" width="12" height="15.3" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="370" y="215.1" width="12" height="14.9" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="385" y="215.4" width="12" height="14.6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="400" y="214.8" width="12" height="15.2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="415" y="217.0" width="12" height="13.0" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="430" y="221.2" width="12" height="8.8" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="445" y="226.5" width="12" height="3.5" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><rect x="460" y="229.9" width="12" height="1.0" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="640.0" y="244.0" text-anchor="end" style="fill:var(--muted);font-size:10px">横轴：时间（每格 0.25 秒，共 10 秒）</text>
  </g>
</svg>
</figure>

（加载 1 万个商品本身花了不到 1 秒，所以过期出现在统计开始后 4.3 秒左右。）

两种情况查数据库的总次数一样（每个商品一次），但**同时过期时，1 万次查询挤在 1 秒内，最多的 0.25 秒中有 3366 次**；过期时间加上随机值以后，分散在 4 秒多的时间里，峰值降到 671 次，只有原来的五分之一。数据库能承受的是峰值，不是总量。

所以写缓存时，**过期时间要加一个随机值**：`ttl = 基础时间 + rand() % 抖动范围`。

更严重的雪崩是 **Redis 本身挂了**，所有的请求都打到数据库上。这要靠第 6 篇的复制和哨兵保证 Redis 的可用性，同时在应用中做限流和降级：数据库扛不住时，宁可让一部分请求返回「稍后再试」，也不要让数据库彻底崩溃。

| 问题 | 现象 | 解决 |
|---|---|---|
| 击穿 | 一个热点 key 过期，大量请求同时查数据库 | 互斥锁、热点不过期、逻辑过期 |
| 穿透 | 查询不存在的数据，缓存永远不命中 | 缓存空值、参数校验、布隆过滤器 |
| 雪崩 | 大量 key 同时过期，或者 Redis 挂了 | 过期时间加随机值；Redis 高可用；限流降级 |

## 5. 缓存和数据库的一致性

### 5.1 为什么删除缓存，而不是更新缓存

写数据时有几种选择：更新数据库后更新缓存、更新数据库后删除缓存、先删除缓存再更新数据库……

**更新缓存**的问题：两个写请求 A、B 并发，数据库中 A 先写、B 后写（最终是 B 的值），但缓存中可能 B 先写、A 后写（最终是 A 的值），从此不一致，直到过期。而且很多缓存的值是计算出来的（比如本文的评论数、平均分），每次写都重新计算，可能根本没有人读。

**删除缓存**，让下一次读去加载最新的值，简单得多。

**先删缓存、再更新数据库**：删除之后、更新之前，一个读请求未命中，从数据库读到旧值写回缓存，然后数据库才被更新，缓存中就一直是旧值。这个时间窗口是「一次数据库更新」那么长，很容易发生。

**先更新数据库、再删除缓存**是最常用的做法。但它也有漏洞。

### 5.2 复现：仍然可能留下旧值

时间顺序：

1. 读请求：缓存未命中，从数据库读到价格 100；
2. 写请求：把价格改成 200，删除缓存；
3. 读请求：把刚才读到的 100 写回缓存。

读请求在第 1 步和第 3 步之间被「卡住」了（GC、线程调度、网络慢），这期间写请求完成了更新和删除。在程序中按这个顺序执行：

```
读请求：缓存未命中，从数据库读到 product-1|100|10|3.40，准备写回缓存……
写请求：数据库改成 200，删除缓存
读请求：写回缓存。现在缓存中是 product-1|100|10|3.40，数据库中是 product-1|200|10|3.40
写请求 500 毫秒后再删一次缓存（延迟双删）。再读：product-1|200|10|3.40，缓存未命中，从数据库读
```

<figure class="diagram">
<svg viewBox="0 0 700 250" role="img" aria-label="先更新数据库再删除缓存时的竞争：读请求未命中，从数据库读到 100；写请求把数据库改成 200 并删除缓存；读请求随后把 100 写回缓存。缓存中留下了旧值，直到过期或者被再次删除">
  <defs><marker id="rrc-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rrc-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="110.0" y="22.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">读请求</text><text x="350.0" y="22.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">写请求</text><text x="590.0" y="22.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">缓存 / 数据库</text><line x1="110" y1="30" x2="110" y2="240" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="350" y1="30" x2="350" y2="240" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="590" y1="30" x2="590" y2="240" style="stroke:var(--border-2);stroke-width:1.2"/><rect x="15" y="44" width="190" height="30" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="110.0" y="63.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">① GET 未命中，查库读到 100</text><text x="590.0" y="64.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">缓存：空 / 库：100</text><rect x="255" y="94" width="190" height="30" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="350.0" y="113.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">② UPDATE 改成 200</text><text x="590.0" y="114.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">缓存：空 / 库：200</text><rect x="255" y="134" width="190" height="30" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="350.0" y="153.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">③ DEL 缓存</text><text x="590.0" y="154.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">缓存：空 / 库：200</text><rect x="15" y="179" width="190" height="30" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="110.0" y="198.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">④ SET 100（旧值！）</text><text x="590.0" y="199.0" text-anchor="middle" style="fill:var(--danger);font-size:11px">缓存：100 / 库：200</text><line x1="110" y1="80" x2="110" y2="178" style="stroke:var(--warn);stroke-width:4"/><text x="118.0" y="130.0" text-anchor="start" style="fill:var(--warn);font-size:11px">卡住（GC、调度）</text><text x="20.0" y="240.0" text-anchor="start" style="fill:var(--accent);font-size:11px">延迟双删：写请求过几百毫秒再 DEL 一次，清掉 ④ 写回的旧值</text>
  </g>
</svg>
</figure>

缓存中是 100，数据库中是 200，**这个旧值会一直留到缓存过期**。这个窗口比「先删缓存」的窗口小得多（读请求要在写请求的整个更新和删除期间都卡住），但不是不可能。

**延迟双删**：写请求删除缓存以后，过一小段时间（比一次读请求的耗时长，比如几百毫秒）再删一次，把可能写回的旧值清掉。实验的最后一行，第二次删除以后，读请求从数据库读到了 200。

### 5.3 更可靠的做法

- **缓存一定要有过期时间**：它是最后的兜底，任何不一致最多持续一个过期时间；
- **删除失败要重试**：更新数据库成功、删除缓存失败（Redis 暂时不可用），缓存中就是旧值。可以把删除放进消息队列重试；
- **订阅 binlog 删除缓存**：用 Canal 之类的工具读取 MySQL 的 binlog，数据一有变化就删除对应的缓存，应用的写逻辑中不用关心缓存；
- **接受最终一致**：缓存本质上是数据的一份副本，它和数据库之间不可能做到强一致。价格、库存这类必须准确的数据，在下单、支付这一步一定要以数据库为准，缓存只用来展示。

## 6. 常见错误

1. **缓存不设过期时间**，任何一次不一致都会永远存在，内存也会一直增长。
2. **写时更新缓存**，并发写导致缓存和数据库顺序不一致。
3. **先删除缓存再更新数据库**，读请求很容易把旧值写回去。
4. **所有的 key 用同样的过期时间**，同时过期造成雪崩。
5. **热点 key 没有保护**，过期瞬间大量请求打到数据库（实验中 50 个线程查了 200 次）。
6. **不存在的数据不做处理**，被恶意请求穿透。
7. **用缓存中的库存、价格做最终判断**。
8. **互斥锁不设过期时间**，拿锁的线程崩溃以后，所有的请求都在等一把永远不会释放的锁。

## 7. 小结

- 旁路缓存：读时先查 Redis，未命中查 MySQL 并写回（带过期时间）；写时先更新数据库，再删除缓存；
- 单线程逐个读总时间只缩短了 28%，但数据库的查询减少了 83%；8 个线程并发时吞吐量翻了一倍；
- 击穿用互斥锁（200 次 → 4 次），穿透用缓存空值或布隆过滤器（10 万次 → 100 次），雪崩用随机过期时间（峰值 3366 → 671）；
- 先更新数据库再删除缓存仍然可能留下旧值，延迟双删、过期时间、binlog 订阅让不一致的时间尽量短。

## 8. 练习

1. 本章第 4 篇说，Redis 可以用 LFU 淘汰。商品缓存应该用 `allkeys-lru`、`allkeys-lfu` 还是 `volatile-lru`？
2. 第 2 节的互斥锁中，没拿到锁的线程每 10 毫秒重试一次。如果数据库查询要 2 秒，会发生什么？怎样改进？
3. 一个用户修改了自己的昵称，刷新页面后看到的还是旧昵称，过一会儿才变。可能的原因有哪些？

<details>
<summary>点击查看答案</summary>

1. 如果这个 Redis 实例只用来做缓存，用 `allkeys-lfu` 或 `allkeys-lru`：所有的 key 都可以淘汰，商品的访问有明显的热点，第 4 篇的实测中 LFU 的命中率更高。`volatile-lru` 只在有过期时间的 key 中淘汰，本文的缓存都有过期时间，效果和 `allkeys-lru` 差不多，但如果实例中混有不能丢的数据（没有过期时间），它可以保护那些数据。

2. 等锁的线程在 2 秒内每个都要重试 200 次，50 个线程就是每秒 5000 次额外的 `GET` 和 `SET NX`，请求的响应时间也都超过了 2 秒，可能引发上游超时。另外锁的有效期 3 秒，查询再慢一点锁就会过期，第二个线程也去查数据库。改进：查询慢的热点数据用「逻辑过期」，过期后先返回旧值，后台一个线程刷新；或者等待时设置上限，超时就返回旧值或者降级的结果；锁的有效期要大于查询的最长时间，或者在持有期间续期。

3. 几种可能：（1）读写分离，读请求到了 MySQL 的从库，复制还没跟上（《主从复制》）；（2）缓存删除失败，或者第 5.2 节的竞争把旧值写回了缓存，要等过期；（3）页面或 CDN、浏览器也有缓存；（4）Redis 的读请求到了 Redis 的从节点，删除还没复制过去。排查时依次比较数据库主库、从库、Redis 主从中的值。

</details>
