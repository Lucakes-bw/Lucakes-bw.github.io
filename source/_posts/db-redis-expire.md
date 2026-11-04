---
title: 过期与内存淘汰
date: 2026-11-07 10:30:00
updated: 2026-11-07 10:30:00
categories:
  - 数据库
  - Redis
tags:
  - 数据库
  - Redis
  - 缓存
order: 4
description: key 到了过期时间并不会立即被删除。实测：20 万个 key 同时过期，1.3 秒内被后台清理干净；但过期的 key 只占 5% 时，10 秒后还剩 9874 个，直到被访问才删掉。内存到了上限，Redis 按策略淘汰：它的 LRU 是抽样的近似算法，齐夫分布下命中率 73.6%，同样容量的精确 LRU 是 74.8%；LFU 达到了 77.2%，比精确 LRU 还高。默认的 noeviction 则直接拒绝写入。
top_group_list: true
disableNunjucks: true
viz: [db-redis]
---

> **本文目标**：会给 key 设置过期时间；理解惰性删除和定期删除，知道过期的 key 为什么会占着内存；会配置 `maxmemory` 和淘汰策略；理解 Redis 的近似 LRU 和 LFU，能根据访问特点选择策略。
>
> **前置知识**：本章前几篇；《数据结构》专栏的 [LRU 缓存](/posts/ds-lru/)。
>
> **实验环境**：同第 1 篇。

## 1. 过期时间

```
SET code:13800001111 482913 EX 300     # 写入时设置，300 秒后过期
EXPIRE session:abc 1800                # 给已有的 key 设置
PEXPIRE / PX                           # 毫秒
TTL key                                # 还剩多少秒；-1 表示没有过期时间，-2 表示 key 不存在
PERSIST key                            # 去掉过期时间
```

`SET` 一个已有的 key（不带 `EX`）会清除它原来的过期时间，这是一个常见的坑：缓存更新以后变成了永不过期。需要保留时加 `KEEPTTL`。

过期时间存在一个单独的字典中：key → 过期的时间点（毫秒）。到了时间点以后，key 要被删掉，问题是**什么时候删**。

## 2. 什么时候删：惰性删除 + 定期删除

### 2.1 两种方式

- **惰性删除**：每次访问一个 key 之前，先检查它是否过期，过期了就删除，当作不存在。代价最小，但一个过期以后再也没人访问的 key，永远不会被删除；
- **定期删除**：定时任务（默认每秒 10 次，`hz 10`）从带过期时间的 key 中随机抽查一批，删掉其中过期的。如果抽到的过期 key 比例很高（超过 10% 左右），说明还有很多，就接着抽，直到这一轮的时间用完；比例低就停下来，等下一轮。

为什么不在到期的那一刻精确地删除？那需要为每个 key 维护一个定时器（或者按过期时间排序的结构），上千万个 key 时开销很大。抽样只需要一个哈希表，代价是**过期的 key 可能还会在内存中待一会儿**。

### 2.2 实测：大量 key 同时过期

写入 20 万个 2 秒后过期的 key，之后不再访问，每 200 毫秒看一次 `DBSIZE`（它包括已经过期、但还没被删掉的 key）、`expired_keys`（累计删掉了多少个过期的 key）和内存：

```
写入 200000 个 2 秒后过期的 key，之后不再访问它们
时间     DBSIZE expired_keys 内存(MB)
  0.0s     200000            0       17.0
  …
  1.8s     200000            0       14.9
  2.0s     180752        20247       13.9
  2.2s     158509        42167       12.9
  2.4s     116970        84036       10.9
  2.6s      97023       126526        8.9
  2.9s      50947       149721        7.8
  3.1s       6726       194779        2.3
  3.3s          0       200000        1.4
```

全部过期以后，每次抽查几乎都是过期的 key，定期删除一直在工作，**1.3 秒内删完了 20 万个**，内存从 14.9 MB 降到 1.4 MB。

### 2.3 实测：少量 key 过期

换一种情况：19 万个 1 小时后过期的 key，1 万个 1 秒后过期的 key：

```
19 万个 1 小时后过期的 key + 1 万个 1 秒后过期的 key
时间     DBSIZE expired_keys
  0.0s     200000            0
  1.0s     199999            1
  2.0s     199985           15
  3.0s     199970           30
  …
 10.0s     199886          114
还没删掉的过期 key 有 9874 个；访问全部 1 万个 short key：返回了值的 0 个，expired_keys 增加 9874，DBSIZE 变成 190000
```

**10 秒过去了，1 万个过期的 key 只删掉了 114 个**，每秒十来个。随机抽查时，过期的 key 只占 5%，比例低于阈值，每一轮抽查一批就停了。剩下的 9874 个一直占着内存，直到被访问：访问时惰性删除生效，返回空，同时删掉，`DBSIZE` 才变成 19 万。

<figure class="diagram">
<svg viewBox="0 0 700 250" role="img" aria-label="过期 key 的清理。左：20 万个 key 在 2 秒时同时过期，DBSIZE 在 1.3 秒内降到 0。右：1 万个 key 在 1 秒时过期，但只占全部带过期时间的 key 的 5%，10 秒后还有 9886 个没被删除">
  <defs><marker id="rex-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rex-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="30.0" y="30.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">20 万个 key 同时过期</text><line x1="30" y1="200" x2="320" y2="200" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="30" y1="40" x2="30" y2="200" style="stroke:var(--border-2);stroke-width:1.2"/><polyline points="30.0,40.0 43.8,40.0 57.6,40.0 71.4,40.0 85.2,40.0 99.0,40.0 112.9,40.0 126.7,40.0 140.5,40.0 154.3,40.0 168.1,55.4 181.9,73.2 195.7,106.4 209.5,122.4 230.2,159.2 244.0,194.6 257.9,200.0 271.7,200.0 285.5,200.0 299.3,200.0 313.1,200.0" style="fill:none;stroke:var(--accent);stroke-width:2"/><text x="320.0" y="216.0" text-anchor="end" style="fill:var(--muted);font-size:10px">秒</text><text x="34.0" y="50.0" text-anchor="start" style="fill:var(--muted);font-size:10px">DBSIZE（20 万）</text><line x1="168" y1="40" x2="168" y2="200" style="stroke:var(--warn);stroke-width:1;stroke-dasharray:4 3"/><text x="172.0" y="56.0" text-anchor="start" style="fill:var(--warn);font-size:10px">2 秒：过期</text><text x="30.0" y="228.0" text-anchor="start" style="fill:var(--accent);font-size:11px">1.3 秒内全部删除</text><text x="390.0" y="30.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">1 万个过期 + 19 万个未过期</text><line x1="390" y1="200" x2="680" y2="200" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="390" y1="40" x2="390" y2="200" style="stroke:var(--border-2);stroke-width:1.2"/><polyline points="419.0,72.0 448.0,72.2 477.0,72.4 506.0,72.5 535.0,72.7 564.0,72.8 593.0,73.0 622.0,73.1 651.0,73.3 680.0,73.5" style="fill:none;stroke:var(--accent);stroke-width:2"/><text x="680.0" y="216.0" text-anchor="end" style="fill:var(--muted);font-size:10px">秒</text><text x="394.0" y="50.0" text-anchor="start" style="fill:var(--muted);font-size:10px">没删掉的过期 key</text><text x="419.0" y="94.0" text-anchor="start" style="fill:var(--warn);font-size:10px">1 秒时 1 万个 key 过期，之后几乎不降</text><text x="390.0" y="228.0" text-anchor="start" style="fill:var(--danger);font-size:11px">10 秒后还剩 9886 个：只能等访问时惰性删除</text>
  </g>
</svg>
</figure>

所以：

- 过期的 key **在逻辑上**一定不存在，读不到；
- 但**在内存中**可能存在很久，`DBSIZE`、`INFO keyspace` 和内存都会包括它们；
- 过期的 key 只占一小部分时，它们几乎只能靠访问和内存淘汰来清理。

## 3. 内存上限与淘汰策略

### 3.1 maxmemory

不设上限（默认 `maxmemory 0`），Redis 会一直用内存，直到操作系统的 OOM killer 把它杀掉。作为缓存使用时，应该设一个上限，比如机器内存的 60%～70%（留出 fork 写时复制和缓冲区的空间，第 5 篇）。

到了上限以后怎么办，由 `maxmemory-policy` 决定：

| 策略 | 从哪些 key 中选 | 选哪个 |
|---|---|---|
| `noeviction`（默认） | 不淘汰 | 写入命令返回错误 |
| `allkeys-lru` | 所有的 key | 最近最少使用的 |
| `allkeys-lfu` | 所有的 key | 访问频率最低的 |
| `allkeys-random` | 所有的 key | 随机 |
| `volatile-lru`、`volatile-lfu`、`volatile-random` | 只在设置了过期时间的 key 中 | 同上 |
| `volatile-ttl` | 只在设置了过期时间的 key 中 | 剩余时间最短的 |

### 3.2 实测：noeviction

先看 1 万个 100 字节的 key 占多少内存，把上限设成大约能放下 1 万个：

```
每个 key 大约 171 字节，空库 1.41 MB，maxmemory 设为 3.04 MB
noeviction：写到第 9950 个 key 时报错：OOM command not allowed when used memory > 'maxmemory'.
```

`noeviction` 下，内存满了以后**写入直接失败**，读仍然可以。数据必须完整保留的场景（比如 Redis 存的是队列、会话，不能悄悄丢掉）应该用它，并且监控内存；用作缓存时，这通常不是想要的结果。

## 4. 近似 LRU

### 4.1 为什么不用精确的 LRU

《LRU 缓存》中的精确 LRU 用哈希表加双向链表：每个 key 多两个指针（16 字节），每次访问都要把节点移到链表头部。对 Redis 来说，几千万个 key 多出几百 MB 的指针，每次读都要修改链表，代价不小。

Redis 的做法：

- 每个对象头中有一个 **24 位的时间戳**，记录最后一次访问的时间（秒级精度，不增加额外的内存）；
- 需要淘汰时，**随机抽取 `maxmemory-samples` 个 key**（默认 5 个），把它们放进一个 16 个位置的**淘汰池**，池中按空闲时间排序；
- 淘汰池中空闲时间最长的那个被淘汰。池中的候选会保留到下一次淘汰，所以比单纯「抽 5 个、淘汰最旧的」更接近真正的 LRU。

<div class="dsviz" data-viz="redis-evict"></div>

### 4.2 实测：和精确 LRU 比

和《LRU 缓存》一样，用齐夫分布生成访问序列：10 万个不同的 key（值 100 字节），50 万次访问，第 r 热门的 key 被访问的概率和 1/r 成正比。每次 `GET`，未命中就 `SET`，就像一个真实的缓存。上限还是大约 1 万个 key。在同一个序列上，用 C 写一个精确 LRU，容量设为 Redis 实际缓存的 key 数：

```
== 齐夫分布，100000 个不同的 key，500000 次访问
  allkeys-random  samples=5  命中率  71.2%  缓存了 9991 个 key（同样容量的精确 LRU： 74.6%），淘汰了 134200 次，写入失败 0 次
  allkeys-lru     samples=5  命中率  73.6%  缓存了 10236 个 key（同样容量的精确 LRU： 74.8%），淘汰了 122010 次，写入失败 0 次
  allkeys-lru     samples=10 命中率  73.8%  缓存了 10250 个 key（同样容量的精确 LRU： 74.9%），淘汰了 120930 次，写入失败 0 次
  allkeys-lfu     samples=5  命中率  77.2%  缓存了 10276 个 key（同样容量的精确 LRU： 74.9%），淘汰了 104000 次，写入失败 0 次
```

<figure class="diagram">
<svg viewBox="0 0 700 200" role="img" aria-label="齐夫分布下各种淘汰策略的命中率：allkeys-random 71.2%，allkeys-lru（抽样 5）73.6%，allkeys-lru（抽样 10）73.8%，同样容量的精确 LRU 约 74.8%，allkeys-lfu 77.2%">
  <defs><marker id="rhr-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rhr-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="160.0" y="32.0" text-anchor="end" style="fill:var(--fg);font-size:12px">allkeys-random</text><rect x="170" y="16" width="186.0" height="20" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="364.0" y="32.0" text-anchor="start" style="fill:var(--muted);font-size:11px">71.2%</text><text x="160.0" y="62.0" text-anchor="end" style="fill:var(--fg);font-size:12px">allkeys-lru，抽样 5</text><rect x="170" y="46" width="258.0" height="20" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="436.0" y="62.0" text-anchor="start" style="fill:var(--muted);font-size:11px">73.6%</text><text x="160.0" y="92.0" text-anchor="end" style="fill:var(--fg);font-size:12px">allkeys-lru，抽样 10</text><rect x="170" y="76" width="264.0" height="20" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="442.0" y="92.0" text-anchor="start" style="fill:var(--muted);font-size:11px">73.8%</text><text x="160.0" y="122.0" text-anchor="end" style="fill:var(--fg);font-size:12px">精确 LRU（C 模拟）</text><rect x="170" y="106" width="294.0" height="20" rx="2" style="fill:none;stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:5 4"/><text x="472.0" y="122.0" text-anchor="start" style="fill:var(--muted);font-size:11px">74.8%</text><text x="160.0" y="152.0" text-anchor="end" style="fill:var(--fg);font-size:12px">allkeys-lfu</text><rect x="170" y="136" width="366.0" height="20" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="544.0" y="152.0" text-anchor="start" style="fill:var(--muted);font-size:11px">77.2%</text><text x="170.0" y="192.0" text-anchor="start" style="fill:var(--muted);font-size:11px">横轴从 65% 开始；10 万个 key，50 万次访问，缓存大约放得下 1 万个</text>
  </g>
</svg>
</figure>

- **近似 LRU 只比精确 LRU 低 1.2 个百分点**（73.6% 对 74.8%），抽样 10 个时差距缩小到 1.1 个百分点。用很少的内存和 CPU，换来了几乎一样的效果；
- 随机淘汰低了 2.4 个百分点，和《LRU 缓存》中 FIFO、随机淘汰的结论一致；
- **LFU 达到了 77.2%，比精确 LRU 还高 2.3 个百分点**，下一节解释。

另外，实验中把 `maxmemory-samples` 设成 1 或 3 时，跑了一阵以后出现过写入返回 OOM 错误的情况（内存已满而这一轮没能淘汰掉 key）。默认值 5 和 10 在所有的运行中都没有出现。没有特别的理由，不要把它调得很小。

### 4.3 LFU

LRU 只看「最后一次是什么时候访问的」：一个热门的 key 刚好有一会儿没被访问，可能被一个只访问一次的冷门 key 挤掉。齐夫分布中，大量冷门的 key 偶尔出现一次，每次都会进入缓存，把热门的 key 往淘汰的方向推。

**LFU**（Least Frequently Used）看的是访问频率。Redis 把对象头中那 24 位拆成两部分：

- 8 位的**对数计数器**：访问时不是简单地加一，而是以一定的概率加一，计数越大，加一的概率越小（`lfu-log-factor` 控制）。8 位就能区分几次和上百万次的访问；
- 16 位的**上次衰减时间**：每过 `lfu-decay-time` 分钟（默认 1），计数减一。曾经热门、现在没人访问的 key，频率会慢慢降下来。

新写入的 key 计数从 5 开始，避免刚进来就被淘汰。实测中 LFU 的淘汰次数（10.4 万）也明显少于 LRU（12.2 万）：热门的 key 不再被反复淘汰、又反复写回。

LFU 也有缺点：访问模式突然变化时（比如一条新闻刚刚变热），新的热点要积累一段时间的频率才能稳定地留在缓存中。

### 4.4 怎么选

- **纯缓存，所有的 key 都可以丢**：`allkeys-lru` 或 `allkeys-lfu`。访问有明显的热点、热点比较稳定时，LFU 更好；
- **缓存和不能丢的数据混在一个实例中**：给缓存的 key 设过期时间，用 `volatile-lru`/`volatile-lfu`，没有过期时间的 key 不会被淘汰。但如果带过期时间的 key 都淘汰完了还不够，写入同样会失败。更好的做法是分开部署两个实例；
- **数据不能丢**：`noeviction`，并监控内存。

## 5. 常见错误

1. **以为 key 一过期就释放了内存**。过期的 key 只占少数时，可能在内存中待很久，`DBSIZE` 也会包括它们。
2. **作为缓存却保持默认的 `noeviction`**，内存满了以后所有的写入都报错。
3. **不设 `maxmemory`**，内存一直增长，最后进程被操作系统杀掉。
4. **用 `volatile-*` 策略，但大部分 key 没有过期时间**，没有可以淘汰的 key，写入报错。
5. **`SET` 覆盖已有的 key 时丢了过期时间**，缓存变成了永不过期。用 `SET … EX` 或 `KEEPTTL`。
6. **大量 key 设置了相同的过期时间**，同一时刻一起失效，后端数据库被打垮（第 7 篇的「雪崩」）。

## 6. 小结

- 过期的 key 靠惰性删除（访问时检查）和定期删除（随机抽查）清理；大量 key 同时过期时 1.3 秒就删完了，过期的只占 5% 时，10 秒只删掉 114 个，剩下的等访问时才删除；
- `maxmemory` 设上限，`maxmemory-policy` 选择淘汰策略，默认的 `noeviction` 会让写入失败；
- Redis 的 LRU 是抽样加淘汰池的近似算法，齐夫分布下只比精确 LRU 低 1.2 个百分点；
- LFU 用 8 位对数计数器加衰减，命中率 77.2%，比精确 LRU 还高。

## 7. 练习

1. 一个实例只用来存会话，每个会话 30 分钟过期，内存一直在缓慢增长，`DBSIZE` 远大于在线用户数。可能的原因是什么？
2. 什么样的访问模式下 LRU 比 LFU 好？
3. 为什么 Redis 的 LRU 时间戳只有 24 位、精度是秒，却够用？

<details>
<summary>点击查看答案</summary>

1. 大部分会话过期以后再也不会被访问（用户已经走了），只能靠定期删除清理。如果过期的会话在所有带过期时间的 key 中占比不高（比如活跃用户的会话不断续期，数量很多），每轮随机抽查到的过期 key 比例低于阈值，清理就很慢，于是过期的会话越积越多，`DBSIZE` 和内存都包括它们。可以调高 `active-expire-effort`（让定期删除更积极），设置 `maxmemory` 和 `volatile-lru` 让内存满时淘汰它们，或者定期用 `SCAN` 遍历（遍历时访问到的过期 key 会被惰性删除）。

2. 热点变化很快的时候。比如新闻、直播：几分钟前的热点很快没人看了，新的热点刚出现。LFU 中旧热点的计数要按分钟衰减，新热点要积累频率，转换得慢；LRU 只看最近有没有被访问，立刻就能反映变化。

3. 淘汰只需要比较「哪个 key 更久没有被访问」，不需要精确的时间。24 位的秒级时钟大约 194 天回绕一次，Redis 计算空闲时间时处理了回绕；一个缓存中的 key，几秒和几十秒的空闲时间差别已经足以排序。这 24 位放在对象头中原本就有的位置，不增加任何内存。

</details>
