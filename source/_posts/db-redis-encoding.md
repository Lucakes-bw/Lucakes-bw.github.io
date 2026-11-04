---
title: 底层编码与内存
date: 2026-11-07 10:20:00
updated: 2026-11-07 10:20:00
categories:
  - 数据库
  - Redis
tags:
  - 数据库
  - Redis
  - 内存
order: 3
description: 同一种类型，Redis 会根据大小选择不同的底层结构：元素少的时候用一块连续内存（listpack），多了才换成哈希表或跳表。本文实测：100 万对 id → 名字存成 100 万个字符串 key 占 59 MB，每 100 个放进一个哈希只占 15 MB；有序集合从 128 个成员变成 129 个，内存从 1.7 KB 跳到 11.6 KB。代价是 listpack 中查找要顺序扫描：10000 个字段时 HGET 要 12.68 微秒，哈希表始终是 0.04 微秒。
top_group_list: true
disableNunjucks: true
viz: [db-redis]
---

> **本文目标**：知道每种类型有哪几种底层编码，会用 `OBJECT ENCODING`、`MEMORY USAGE` 查看；理解 listpack 省内存的原理和它的代价；会估算数据占用的内存，会用小哈希节省内存；知道编码转换的阈值和方向。
>
> **前置知识**：本章前两篇；《数据结构》专栏的哈希表、[跳表](/posts/ds-skiplist/)。
>
> **实验环境**：同第 1 篇，Redis 8.10.3。文中的阈值都是这个版本的默认值，不同版本可能不同，以 `CONFIG GET` 的结果为准。

## 1. 一种类型，几种编码

对外，Redis 只有字符串、列表、哈希这些类型；对内，每种类型有几种实现，叫**编码**。`OBJECT ENCODING` 可以查看一个 key 当前的编码：

```
> SET n 12345          → int
> SET s hello          → embstr
> SET long aaaa…（50 个 a）→ raw
> RPUSH l a b c        → listpack
> SADD si 1 2 3        → intset
> SADD ss a b          → listpack
> HSET h f v           → listpack
> ZADD z 1 a           → listpack
```

| 类型 | 小的时候 | 大的时候 | 转换条件（本版本默认值） |
|---|---|---|---|
| 字符串 | `int`（整数）、`embstr`（≤ 44 字节） | `raw` | 不是整数、长度超过 44 字节 |
| 列表 | `listpack` | `quicklist`（多个 listpack 串成的双向链表） | `list-max-listpack-size -2`：超过 8 KB |
| 哈希 | `listpack` | `hashtable` | 超过 512 个字段，或者某个值超过 64 字节 |
| 集合 | `intset`（全是整数）、`listpack` | `hashtable` | intset 超过 512 个；listpack 超过 128 个或某个值超过 64 字节 |
| 有序集合 | `listpack` | `skiplist`（跳表 + 哈希表） | 超过 128 个成员，或者某个成员超过 64 字节 |

阈值都可以用 `CONFIG GET *listpack*` 查看和修改。

字符串中，`int` 直接把整数存在对象的指针字段中，不另外分配内存；`embstr` 把对象头和字符串放在一次分配的连续内存中；`raw` 分两次分配。

## 2. listpack：一块连续的内存

### 2.1 结构

哈希表中每个元素都是一个单独分配的节点，有 `next` 指针，桶数组中还有一个指针指向它；一个只有 3 个字段的哈希，指针和内存分配的开销远大于数据本身。

**listpack** 把所有的元素依次紧凑地放在**一块连续的内存**中：

<figure class="diagram">
<svg viewBox="0 0 700 236" role="img" aria-label="listpack 和哈希表的内存布局。listpack 是一块连续的内存：开头是总字节数和元素个数，之后每个元素是编码、数据和本元素长度，最后是结束标记。哈希表中每个元素是单独分配的节点，有 next 指针，桶数组中还有指针指向它">
  <defs><marker id="rlk-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rlk-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="14.0" y="22.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">listpack：一块连续的内存</text><rect x="20" y="34" width="56" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="48.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">总字节</text><rect x="76" y="34" width="44" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="98.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">个数</text><rect x="120" y="34" width="60" height="34" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="150.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">name</text><rect x="180" y="34" width="60" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="210.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">alice</text><rect x="240" y="34" width="50" height="34" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="265.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">age</text><rect x="290" y="34" width="40" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="310.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">31</text><rect x="330" y="34" width="54" height="34" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="357.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">city</text><rect x="384" y="34" width="70" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="419.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">beijing</text><rect x="454" y="34" width="36" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="472.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">FF</text><text x="140.0" y="88.0" text-anchor="start" style="fill:var(--muted);font-size:11px">每个元素：编码（类型 + 长度）| 数据 | 本元素的长度（可以倒着走）</text><text x="14.0" y="128.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">哈希表：每个元素一个节点，靠指针连起来</text><rect x="20" y="140" width="44" height="28" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="42.0" y="158.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">[0]</text><rect x="70" y="140" width="44" height="28" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="92.0" y="158.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">[1]</text><rect x="120" y="140" width="44" height="28" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="142.0" y="158.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">[2]</text><rect x="170" y="140" width="44" height="28" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="192.0" y="158.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">[3]</text><rect x="20" y="196" width="150" height="30" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="95.0" y="215.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">name → alice</text><rect x="200" y="196" width="150" height="30" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="275.0" y="215.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">age → 31</text><rect x="400" y="196" width="150" height="30" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="475.0" y="215.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">city → beijing</text><line x1="42" y1="168" x2="42" y2="194" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rlk-a)"/><line x1="142" y1="168" x2="240" y2="194" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rlk-a)"/><line x1="350" y1="211" x2="398" y2="211" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rlk-a)"/><text x="374.0" y="204.0" text-anchor="middle" style="fill:var(--link);font-size:11px">next</text><text x="300.0" y="150.0" text-anchor="start" style="fill:var(--muted);font-size:11px">每个节点：key、value 指针 + next 指针，</text><text x="300.0" y="166.0" text-anchor="start" style="fill:var(--muted);font-size:11px">再加上分配器的开销；桶数组中还有一个指针</text>
  </g>
</svg>
</figure>

- 开头是总字节数和元素个数；
- 每个元素由「编码（类型和长度）+ 数据 + 这个元素的总长度」组成。小整数只用 1 个字节，短字符串的长度只用 1 个字节表示；
- 元素末尾记下自己的长度，所以可以从后往前遍历。

哈希的字段和值、有序集合的成员和分数，都是相邻的两个元素。

### 2.2 代价：查找要顺序扫描

listpack 没有索引，`HGET` 要从头逐个比较字段名，O(n)。插入、删除可能要移动后面所有的元素，甚至重新分配整块内存。所以它只适合元素少的时候：n 很小时，顺序扫描一块连续内存，CPU 缓存命中率高，比跟着指针跳来跳去的哈希表并不慢多少。

下面的演示比较在 listpack 和哈希表中查找一个字段：

<div class="dsviz" data-viz="redis-listpack"></div>

实测：把 `hash-max-listpack-entries` 临时调大，让不同大小的哈希都用 listpack 存，和哈希表比较 HGET 在服务器上的平均耗时（`INFO commandstats` 的 `usec_per_call`，随机读 10 万次）和 `MEMORY USAGE`：

```
     10 个字段  listpack   HGET   0.05 微秒  内存      207 字节
     10 个字段  hashtable  HGET   0.04 微秒  内存      564 字节
    100 个字段  listpack   HGET   0.18 微秒  内存     2007 字节
    100 个字段  hashtable  HGET   0.04 微秒  内存     4880 字节
    512 个字段  listpack   HGET   0.83 微秒  内存    11071 字节
    512 个字段  hashtable  HGET   0.04 微秒  内存    24432 字节
   1000 个字段  listpack   HGET   1.52 微秒  内存    21807 字节
   1000 个字段  hashtable  HGET   0.04 微秒  内存    48048 字节
  10000 个字段  listpack   HGET  12.68 微秒  内存   237807 字节
  10000 个字段  hashtable  HGET   0.04 微秒  内存   548928 字节
```

<figure class="diagram">
<svg viewBox="0 0 700 230" role="img" aria-label="HGET 在服务器上的平均耗时：哈希表在 10 到 10000 个字段时都是 0.04 微秒；listpack 是 0.05、0.18、0.83、1.52、12.68 微秒，和字段数成正比">
  <defs><marker id="rhg-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rhg-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <line x1="80" y1="190" x2="680" y2="190" style="stroke:var(--border-2);stroke-width:1.2"/><rect x="90" y="189.4" width="34" height="1.5" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="107.0" y="183.4" text-anchor="middle" style="fill:var(--warn);font-size:11px">0.05</text><rect x="130" y="189.5" width="34" height="1.5" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="147.0" y="182.0" text-anchor="middle" style="fill:var(--accent);font-size:11px">0.04</text><text x="127.0" y="208.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">10 个字段</text><rect x="200" y="187.7" width="34" height="2.3" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="217.0" y="181.7" text-anchor="middle" style="fill:var(--warn);font-size:11px">0.18</text><rect x="240" y="189.5" width="34" height="1.5" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="257.0" y="182.0" text-anchor="middle" style="fill:var(--accent);font-size:11px">0.04</text><text x="237.0" y="208.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">100 个字段</text><rect x="310" y="179.5" width="34" height="10.5" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="327.0" y="173.5" text-anchor="middle" style="fill:var(--warn);font-size:11px">0.83</text><rect x="350" y="189.5" width="34" height="1.5" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="367.0" y="182.0" text-anchor="middle" style="fill:var(--accent);font-size:11px">0.04</text><text x="347.0" y="208.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">512 个字段</text><rect x="420" y="170.8" width="34" height="19.2" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="437.0" y="164.8" text-anchor="middle" style="fill:var(--warn);font-size:11px">1.52</text><rect x="460" y="189.5" width="34" height="1.5" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="477.0" y="182.0" text-anchor="middle" style="fill:var(--accent);font-size:11px">0.04</text><text x="457.0" y="208.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">1000 个字段</text><rect x="530" y="30.0" width="34" height="160.0" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="547.0" y="24.0" text-anchor="middle" style="fill:var(--warn);font-size:11px">12.68</text><rect x="570" y="189.5" width="34" height="1.5" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="587.0" y="182.0" text-anchor="middle" style="fill:var(--accent);font-size:11px">0.04</text><text x="567.0" y="208.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">10000 个字段</text><rect x="90" y="18" width="14" height="12" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="110.0" y="28.0" text-anchor="start" style="fill:var(--fg);font-size:11px">listpack（顺序扫描）</text><rect x="270" y="18" width="14" height="12" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="290.0" y="28.0" text-anchor="start" style="fill:var(--fg);font-size:11px">哈希表</text><text x="370.0" y="28.0" text-anchor="start" style="fill:var(--muted);font-size:11px">单位：微秒；默认阈值 512</text>
  </g>
</svg>
</figure>

- **哈希表始终是 0.04 微秒**，和大小无关；
- **listpack 的耗时和字段数成正比**：10 个字段时和哈希表一样，512 个（默认的阈值）时 0.83 微秒，10000 个时 12.68 微秒，慢了 300 倍；
- listpack 的内存始终只有哈希表的 40% 左右。

默认阈值 512 是一个折中：最慢不到 1 微秒，和一次网络往返（18 微秒以上）相比可以忽略，同时节省一半以上的内存。把阈值调到几千、几万，内存省了，但每次访问都可能是几微秒到十几微秒的 CPU 时间，而且单线程，所有人都要等。

### 2.3 只升不降

一旦超过阈值，转换成哈希表或跳表，就不会再转回来：

```
有序集合：129 个成员 → skiplist；删到 29 个 → 仍然是 skiplist
哈希：600 个字段 → hashtable；删到 9 个 → 仍然是 hashtable
哈希：写入一个 100 字节的值 → hashtable
```

所以一个短暂变大的 key，以后即使变小了，仍然按大 key 的方式占内存。需要的话，可以把它读出来、删掉、重新写入。

## 3. 内存实测

### 3.1 一个 key 有多大的开销

把 100 万对「用户 id → 名字」（名字形如 `name-123456`）用三种方式存进 Redis，看 `used_memory` 增加了多少：

```c
// 方式一：每个用户一个字符串 key
redisAppendCommand(c, "SET user:%d name-%d", id, id);
// 方式二、三：每 per 个用户放进一个哈希
redisAppendCommand(c, "HSET user:%d %d name-%d", id / per, id % per, id);
```

```
== 100 万对 id → 名字，三种存法
  每个用户一个字符串 key：       59.2 MB，平均每对  62.1 字节（编码 embstr）
  每  100 个用户放进一个哈希：    15.1 MB，平均每对  15.8 字节（编码 listpack）
  每 1000 个用户放进一个哈希：    36.2 MB，平均每对  38.0 字节（编码 hashtable）
```

<figure class="diagram">
<svg viewBox="0 0 700 150" role="img" aria-label="100 万对 id 到名字的映射，三种存法占用的内存：每个用户一个字符串 key 59.2 MB；每 100 个用户放进一个哈希（listpack）15.1 MB；每 1000 个用户放进一个哈希（超过阈值，变成哈希表）36.2 MB">
  <defs><marker id="rmm-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rmm-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="160.0" y="38.0" text-anchor="end" style="fill:var(--fg);font-size:12px">字符串 key × 100 万</text><rect x="170" y="20" width="414.4" height="22" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="592.4" y="38.0" text-anchor="start" style="fill:var(--muted);font-size:11px">59.2 MB</text><text x="160.0" y="72.0" text-anchor="end" style="fill:var(--fg);font-size:12px">每 100 个一个哈希</text><rect x="170" y="54" width="105.7" height="22" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="283.7" y="72.0" text-anchor="start" style="fill:var(--muted);font-size:11px">15.1 MB</text><text x="160.0" y="106.0" text-anchor="end" style="fill:var(--fg);font-size:12px">每 1000 个一个哈希</text><rect x="170" y="88" width="253.4" height="22" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="431.4" y="106.0" text-anchor="start" style="fill:var(--muted);font-size:11px">36.2 MB</text><text x="170.0" y="138.0" text-anchor="start" style="fill:var(--muted);font-size:11px">平均每对：62.1 字节、15.8 字节（listpack）、38.0 字节（hashtable）</text>
  </g>
</svg>
</figure>

实际的数据（key 中的 id 加上名字）每对只有十几个字节。**一个字符串 key 平均占 62 字节**：主哈希表中的槽位和条目、对象头（16 字节）、key 和 value 的字符串（各带长度头），再加上内存分配器按档位对齐浪费的部分。数据越小，这些固定开销占的比例越大。

**每 100 个用户放进一个哈希，只占 15.1 MB，是原来的四分之一。** 主哈希表中只有 1 万个 key，每个哈希是一个 listpack，100 个字段紧凑地排在一起，几乎没有额外的开销。

每 1000 个放进一个哈希，超过了 512 的阈值，变成了哈希表，每个字段又有了自己的节点和指针，内存回到 36.2 MB。

所以这种「分桶」的技巧有一个前提：**每个桶的大小不超过 listpack 的阈值**。代价是：

- 不能对单个用户设置过期时间（过期是 key 级别的；Redis 7.4 以后可以用 `HEXPIRE` 给哈希的字段设置过期时间）；
- 访问时要先算出桶号，代码复杂一些；
- 每个桶内的查找是 O(100) 的顺序扫描（第 2.2 节：约 0.18 微秒）。

数据量大、每条数据很小（比如上亿个计数器、id 映射）时，这个技巧能省下大量内存。

### 3.2 有序集合的跳变

有序集合的阈值是 128：

```
== 有序集合的编码转换
  128 个成员：listpack    1712 字节
  129 个成员：skiplist   11575 字节
```

**多一个成员，内存从 1.7 KB 变成 11.6 KB，是原来的 6.8 倍。** 跳表中每个成员一个节点（成员、分数、后退指针、平均 1.33 层的前进指针和跨度，见《跳表》），再加上一个从成员到分数的哈希表（`ZSCORE` 要 O(1)）。有成千上万个小的有序集合时（比如每个用户一个），尽量让它们保持在 128 以内。

### 3.3 查看内存

```
MEMORY USAGE key        # 一个 key 大约占多少字节（大集合是抽样估计的）
INFO memory             # used_memory：分配器分配出去的；used_memory_rss：进程实际占用的物理内存
MEMORY STATS            # 各部分的开销
redis-cli --memkeys     # 找出占内存最多的 key
```

`used_memory_rss` 除以 `used_memory` 是**碎片率**。大量删除以后，分配器手中有很多空闲的小块，碎片率会明显大于 1；`activedefrag yes` 可以让 Redis 在后台整理。

## 4. 哈希表的渐进式 rehash

Redis 的主数据库（所有的 key）和大的哈希、集合，都用链地址法的哈希表（《哈希表》）。哈希表满了要扩容：分配一个两倍大的新表，把所有的元素重新放进去。几千万个 key 时，一次搬完要几秒钟，单线程的 Redis 不能停这么久。

所以 Redis 的哈希表有**两张表**，扩容时**渐进地**搬：

- 扩容开始时只分配新表，记下「搬到了旧表的第几个桶」；
- 之后每次对这个哈希表的增删改查，都顺便搬一个桶；定时任务也会在空闲时每次搬一小批；
- 搬迁期间，查找要两张表都看，新增的元素只放进新表；
- 旧表搬空以后释放，新表成为唯一的表。

<figure class="diagram">
<svg viewBox="0 0 700 220" role="img" aria-label="渐进式 rehash：扩容时同时存在旧表 ht[0]（4 个桶）和新表 ht[1]（8 个桶），rehashidx 记录搬到了旧表的第几个桶。桶 0、1 已经搬到新表，桶 2、3 还在旧表；查找要两张表都看，新元素只放进新表">
  <defs><marker id="rrh-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rrh-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="14.0" y="24.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">ht[0]（旧，4 个桶）</text><rect x="20" y="36" width="56" height="30" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="48.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">[0]</text><rect x="84" y="36" width="56" height="30" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="112.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">[1]</text><rect x="148" y="36" width="56" height="30" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="176.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">[2]</text><rect x="212" y="36" width="56" height="30" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="240.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">[3]</text><text x="20.0" y="90.0" text-anchor="start" style="fill:var(--muted);font-size:10px">已搬走</text><text x="148.0" y="90.0" text-anchor="start" style="fill:var(--muted);font-size:10px">还没搬</text><rect x="148" y="100" width="56" height="26" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="176.0" y="117.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">k7</text><rect x="212" y="100" width="56" height="26" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="240.0" y="117.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">k3</text><line x1="176" y1="66" x2="176" y2="98" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rrh-a)"/><line x1="240" y1="66" x2="240" y2="98" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rrh-a)"/><text x="130.0" y="152.0" text-anchor="start" style="fill:var(--warn);font-size:12px">rehashidx = 2</text><text x="330.0" y="24.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">ht[1]（新，8 个桶）</text><rect x="330" y="36" width="40" height="30" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="350.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0</text><rect x="374" y="36" width="40" height="30" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="394.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">1</text><rect x="418" y="36" width="40" height="30" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="438.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><rect x="462" y="36" width="40" height="30" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="482.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">3</text><rect x="506" y="36" width="40" height="30" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="526.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">4</text><rect x="550" y="36" width="40" height="30" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="570.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">5</text><rect x="594" y="36" width="40" height="30" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="614.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">6</text><rect x="638" y="36" width="40" height="30" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="658.0" y="55.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">7</text><rect x="326" y="100" width="48" height="26" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="350.0" y="117.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">k8</text><line x1="350" y1="66" x2="350" y2="98" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rrh-a)"/><rect x="502" y="100" width="48" height="26" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="526.0" y="117.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">k4</text><line x1="526" y1="66" x2="526" y2="98" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rrh-a)"/><rect x="546" y="100" width="48" height="26" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="570.0" y="117.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">k1</text><line x1="570" y1="66" x2="570" y2="98" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rrh-a)"/><text x="330.0" y="152.0" text-anchor="start" style="fill:var(--muted);font-size:11px">每次增删改查顺便搬一个桶；定时任务也会搬</text><text x="330.0" y="172.0" text-anchor="start" style="fill:var(--muted);font-size:11px">查找：先查 ht[0]，再查 ht[1]；新增：只放进 ht[1]</text><text x="330.0" y="192.0" text-anchor="start" style="fill:var(--muted);font-size:11px">旧表搬空后释放，ht[1] 变成 ht[0]</text>
  </g>
</svg>
</figure>

这和 `SCAN` 一样，都是把一个大操作拆成很多小步，**单线程的系统中，任何可能很慢的操作都要这样拆**。代价是搬迁期间两张表同时占着内存：一个有 1 亿个 key 的实例扩容时，桶数组从 1 亿个指针（800 MB）变成 2 亿个，会额外占用 1.6 GB。内存设了上限时，这可能引发大量淘汰，Redis 在内存紧张时会推迟扩容。

## 5. 常见错误

1. **把阈值调得很大**来省内存。listpack 中的查找和修改是 O(n)，几千个元素时每次操作几微秒到十几微秒，单线程下所有人都要等。
2. **以为大 key 删掉一部分元素就会变回紧凑的编码**。编码只升不降。
3. **用大量的字符串 key 存很小的数据**，固定开销是数据本身的好几倍。考虑分桶放进小哈希。
4. **哈希的某个值偶尔超过 64 字节**，整个哈希就变成了哈希表，内存翻倍。
5. **只看 `used_memory`**，不看 `used_memory_rss` 和碎片率，以为还有很多内存，实际上进程已经被操作系统杀掉了。

## 6. 小结

- 每种类型有多种编码：元素少用 listpack（一块连续内存），多了换成哈希表、跳表；`OBJECT ENCODING` 查看；
- listpack 省内存（约为哈希表的 40%），但查找是顺序扫描：512 个字段 0.83 微秒，10000 个字段 12.68 微秒，哈希表始终 0.04 微秒；
- 一个字符串 key 平均占 62 字节，每 100 个放进一个小哈希只要 15.8 字节；有序集合多一个成员（128 → 129），内存变成 6.8 倍；
- 编码只升不降；哈希表用两张表渐进式 rehash，避免一次搬迁卡住服务器。

## 7. 练习

1. 要存 1 亿个「手机号 → 用户 id」的映射，用字符串 key 大约要多少内存？怎样用本文的方法减少？
2. 一个排行榜有 100 万个成员，为什么不能为了省内存把 `zset-max-listpack-entries` 调到 200 万？
3. 为什么 `embstr` 的上限是 44 字节？（提示：jemalloc 的一个分配档位是 64 字节，对象头 16 字节，短字符串头 3 字节，结尾还有一个 `'\0'`。）

<details>
<summary>点击查看答案</summary>

1. 按本文每个字符串 key 约 62 字节估算（key 是 11 位手机号，比本文的 key 稍长，实际会更多），1 亿个约 6 GB。分桶：用手机号的前 9 位（或者手机号除以 100）作为哈希的 key，后 2 位作为字段，每个哈希最多 100 个字段，保持 listpack 编码，每对大约 16 字节，1 亿个约 1.6 GB。代价是不能对单个映射设置过期时间，访问前要先算桶号。

2. listpack 中的操作是 O(n)：每次 `ZADD`、`ZINCRBY` 都要在 100 万个元素中顺序查找成员，再在一块几十 MB 的连续内存中插入（移动后面所有的数据），每次可能要几毫秒到几十毫秒；`ZRANK` 也要顺序扫描。单线程下，这会让整个 Redis 变慢。跳表的 O(log n) 才是排行榜需要的。

3. 64 - 16（对象头）- 3（sdshdr8：长度 1 字节、分配的大小 1 字节、标志 1 字节）- 1（结尾的 `'\0'`）= 44。对象头和字符串放在同一次分配的 64 字节中，正好用满一个分配档位。

</details>
