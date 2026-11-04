---
title: Redis 入门：数据类型与命令
date: 2026-11-07 10:00:00
updated: 2026-11-07 10:00:00
categories:
  - 数据库
  - Redis
tags:
  - 数据库
  - Redis
order: 1
description: Redis 把数据放在内存中，value 不只是字符串，还可以是列表、哈希、集合、有序集合，每种类型都有自己的命令。本文用 redis-cli 演示五种基本类型和它们的典型用途；用 8 个线程各加 1 万次，「先 GET 再 SET」只剩下 17384，INCR 一次不少；再和 MySQL 比较按主键读一个值：服务器端 Redis 只花 0.21 微秒、MySQL 花 11.5 微秒，但客户端看到的分别是 19 和 35 微秒，时间大部分花在了网络往返上。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：知道 Redis 是什么、适合做什么；会用字符串、列表、哈希、集合、有序集合的常用命令；理解单线程执行命令带来的原子性；知道 Redis 快在哪里，以及一次请求的时间花在了哪里。
>
> **前置知识**：本专栏前几章（MySQL 的基本使用）；《数据结构》专栏的哈希表、跳表。本章的完整代码在 [projects/redis-lab](https://github.com/Lucakes-bw/Lucakes-bw.github.io/tree/main/projects/redis-lab)。
>
> **实验环境**：Redis 8.10.3（Docker 镜像 `redis:8`）和前几章的 MySQL 8.4.13 各在一个容器中；程序在另一个 Ubuntu 24.04 容器中编译运行，GCC 13.3，客户端库 hiredis 1.2.0（`apt install libhiredis-dev`）。三个容器在同一台虚拟机中（2 核、2 GB 内存）。

## 1. Redis 是什么

前几章的 MySQL 把数据存在磁盘上，用 SQL 查询表中的行。**Redis**（Remote Dictionary Server）是另一种数据库：

- **数据都在内存中**，读写不用访问磁盘（持久化是另外的事，第 5 篇）；
- **数据模型是 key → value**，没有表、没有 SQL；
- **value 有多种类型**：字符串、列表、哈希、集合、有序集合……每种类型有自己的一组命令，比如列表可以从两端推入弹出，有序集合可以按分数取前 10 名。

<figure class="diagram">
<svg viewBox="0 0 700 280" role="img" aria-label="Redis 的数据模型：每个 key 对应一个有类型的 value。字符串是一个值；列表是有序的一串；哈希是字段到值的映射；集合是不重复的成员；有序集合的每个成员带一个分数，按分数排序">
  <defs><marker id="rmd-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rmd-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="14" width="130" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="85.0" y="35.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">user:1:name</text><text x="160.0" y="36.0" text-anchor="start" style="fill:var(--muted);font-size:11px">字符串</text><line x1="222" y1="31" x2="252" y2="31" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rmd-a)"/><rect x="20" y="66" width="130" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="85.0" y="87.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">queue:email</text><text x="160.0" y="88.0" text-anchor="start" style="fill:var(--muted);font-size:11px">列表</text><line x1="222" y1="83" x2="252" y2="83" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rmd-a)"/><rect x="20" y="118" width="130" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="85.0" y="139.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">user:1</text><text x="160.0" y="140.0" text-anchor="start" style="fill:var(--muted);font-size:11px">哈希</text><line x1="222" y1="135" x2="252" y2="135" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rmd-a)"/><rect x="20" y="170" width="130" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="85.0" y="191.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">tag:redis</text><text x="160.0" y="192.0" text-anchor="start" style="fill:var(--muted);font-size:11px">集合</text><line x1="222" y1="187" x2="252" y2="187" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rmd-a)"/><rect x="20" y="222" width="130" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="85.0" y="243.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">rank</text><text x="160.0" y="244.0" text-anchor="start" style="fill:var(--muted);font-size:11px">有序集合</text><line x1="222" y1="239" x2="252" y2="239" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rmd-a)"/><rect x="256" y="14" width="90" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="301.0" y="35.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">alice</text><rect x="256" y="66" width="96" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="304.0" y="87.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">b@x.com</text><rect x="360" y="66" width="96" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="408.0" y="87.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">c@x.com</text><line x1="352" y1="83" x2="359" y2="83" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rmd-a)"/><rect x="464" y="66" width="96" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="512.0" y="87.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">d@x.com</text><line x1="456" y1="83" x2="463" y2="83" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rmd-a)"/><rect x="256" y="118" width="60" height="34" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="286.0" y="139.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">name</text><rect x="318" y="118" width="72" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="354.0" y="139.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">alice</text><rect x="396" y="118" width="60" height="34" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="426.0" y="139.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">age</text><rect x="458" y="118" width="72" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="494.0" y="139.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">31</text><rect x="536" y="118" width="60" height="34" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="566.0" y="139.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">city</text><rect x="598" y="118" width="72" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="634.0" y="139.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">beijing</text><ellipse cx="330" cy="187" rx="74" ry="20" style="fill:none;stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:5 4"/><text x="300.0" y="191.0" text-anchor="middle" style="fill:var(--fg);font-size:12px">alice</text><text x="360.0" y="191.0" text-anchor="middle" style="fill:var(--fg);font-size:12px">bob</text><rect x="256" y="222" width="112" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="312.0" y="235.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">bob</text><text x="312.0" y="250.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">4100</text><rect x="376" y="222" width="112" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="432.0" y="235.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">carol</text><text x="432.0" y="250.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">3800</text><rect x="496" y="222" width="112" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="552.0" y="235.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">alice</text><text x="552.0" y="250.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">3200</text><text x="618.0" y="244.0" text-anchor="start" style="fill:var(--muted);font-size:11px">按分数排序</text>
  </g>
</svg>
</figure>

所以 Redis 常被叫作「数据结构服务器」：《数据结构》专栏中的哈希表、跳表，在 Redis 中可以直接通过网络使用，而且多个程序共享同一份数据。

| | MySQL | Redis |
|---|---|---|
| 数据在哪里 | 磁盘（缓冲池中缓存热点页） | 内存 |
| 数据模型 | 表、行、列，SQL | key → 各种类型的 value，每种类型一组命令 |
| 查询能力 | 任意条件、连接、聚合 | 只能按 key 访问，类型自带的操作 |
| 事务 | 完整的 ACID | 单条命令是原子的；MULTI 不能回滚 |
| 容量 | 磁盘大小 | 内存大小 |
| 典型用途 | 业务数据的权威来源 | 缓存、计数器、排行榜、会话、限流、队列 |

两者通常一起使用：MySQL 存所有的数据，Redis 存热点数据和那些用表很难高效实现的东西（第 7 篇）。

## 2. 启动和连接

```bash
docker run -d --name redis8 redis:8
docker exec -it redis8 redis-cli
```

`redis-cli` 是命令行客户端，输入一条命令，显示回复。key 是任意的字符串，习惯上用冒号分出层次，比如 `user:1:name`、`page:home:views`，方便阅读和按前缀查找。

## 3. 五种基本类型

### 3.1 字符串

最简单的类型：一个 key 对应一个字符串（最大 512 MB，可以是任意二进制数据）。

```
> SET user:1:name alice
OK
> GET user:1:name
alice
> INCR page:home:views
1
> INCRBY page:home:views 10
11
> SET code:13800001111 482913 EX 300
OK
> TTL code:13800001111
300
```

- 字符串的内容是整数时，`INCR`、`INCRBY` 直接在服务器上加减，适合做**计数器**（浏览量、点赞数）；
- `EX 300` 让 key 在 300 秒后自动删除，`TTL` 查看还剩多少秒。短信验证码、登录会话都这样存，到期自动失效，不用写清理程序。

### 3.2 列表

按插入顺序排列的字符串，可以从两端推入（`LPUSH`/`RPUSH`）和弹出（`LPOP`/`RPOP`）：

```
> RPUSH queue:email a@x.com b@x.com c@x.com
3
> LPOP queue:email
a@x.com
> LRANGE queue:email 0 -1
b@x.com
c@x.com
```

右边进、左边出，就是一个**队列**：网站把要发的邮件推进列表，后台程序从另一端取出来发送。`BLPOP` 在列表为空时会阻塞等待，取代了轮询。

### 3.3 哈希

一个 key 下面有多个字段，每个字段一个值，适合存**对象**：

```
> HSET user:1 name alice age 30 city beijing
3
> HGET user:1 age
30
> HINCRBY user:1 age 1
31
> HGETALL user:1
name
alice
age
31
city
beijing
```

和把整个对象序列化成一个 JSON 字符串相比，哈希可以只读、只改其中一个字段。

### 3.4 集合

不重复、无序的字符串集合，支持交集、并集、差集：

```
> SADD tag:redis alice bob
2
> SADD tag:mysql bob carol
2
> SINTER tag:redis tag:mysql
bob
> SISMEMBER tag:redis carol
0
```

用途：标签、共同好友（两个人的好友集合求交集）、抽奖去重。

### 3.5 有序集合

每个成员有一个分数，按分数排序。底层是[《跳表》](/posts/ds-skiplist/)一文中的跳表加一个哈希表：

```
> ZADD rank 3200 alice 4100 bob 2800 carol
3
> ZINCRBY rank 1000 carol
3800
> ZREVRANGE rank 0 2 WITHSCORES
bob
4100
carol
3800
alice
3200
> ZREVRANK rank carol
1
```

**排行榜**是最典型的用法：加分是 `ZINCRBY`，前 N 名是 `ZREVRANGE`，查自己排第几是 `ZREVRANK`，都是 O(log n)。用 MySQL 做同样的事，查排名要 `SELECT COUNT(*) … WHERE score > ?`，用户一多就很慢。

### 3.6 通用命令

```
> TYPE rank
zset
> EXISTS user:1 nosuch
1
> DEL user:1:name
1
> KEYS *
queue:email
tag:redis
page:home:views
rank
tag:mysql
code:13800001111
user:1
```

`EXISTS` 返回存在的 key 的个数。`KEYS *` 列出所有的 key，**在生产环境中不要用**，下一篇会看到它让所有的客户端卡住了 109 毫秒。

| 类型 | 底层结构（下一篇之后细讲） | 典型用途 |
|---|---|---|
| 字符串 | 动态字符串，整数直接存 | 缓存、计数器、验证码、分布式锁 |
| 列表 | listpack / quicklist | 队列、最新的 N 条消息 |
| 哈希 | listpack / 哈希表 | 对象、购物车 |
| 集合 | intset / listpack / 哈希表 | 标签、去重、共同好友 |
| 有序集合 | listpack / 跳表 + 哈希表 | 排行榜、延迟队列（分数是时间戳） |

此外还有位图（签到）、HyperLogLog（估算不重复的个数，12 KB 就能数上亿个）、地理位置、Stream（消息队列）等，用到时查文档即可。

## 4. 单线程与原子性

Redis 用**一个线程**执行所有客户端的命令：同一时刻只执行一条命令，执行完才轮到下一条（下一篇细讲）。所以**每条命令都是原子的**，执行过程中不会被别的命令打断。

这件事有多重要？8 个线程，每个对同一个计数器加 1 万次，比较两种写法：

```c
// 写法一：服务器一步完成
freeReplyObject(cmd(c, "INCR counter"));

// 写法二：读出来，加一，再写回去
redisReply* r = cmd(c, "GET counter");
long long v = r->type == REDIS_REPLY_STRING ? atoll(r->str) : 0;
freeReplyObject(r);
freeReplyObject(cmd(c, "SET counter %lld", v + 1));
```

```
GET 再 SET ：8 个线程各加 10000 次，counter = 17384（应该是 80000）
INCR       ：8 个线程各加 10000 次，counter = 80000（应该是 80000）
```

**「先 GET 再 SET」只剩下 17384，丢了 78% 的更新。** 每条命令本身是原子的，但两条命令之间，别的线程可以插进来：A 读到 100，B 也读到 100，A 写回 101，B 也写回 101，两次加一只生效了一次。这就是《C++11》专栏中的数据竞争，只是发生在服务器上。

所以要尽量用一条命令完成一个操作（`INCR`、`HINCRBY`、`SET … NX`、`LPOP`……）。多个操作必须一起完成时，用 Lua 脚本（`EVAL`）：脚本执行期间，其他命令同样不会插进来。

## 5. 有多快

同样是「按主键读一个值」，在 C 中用 hiredis 逐个发 GET，和用 libmysqlclient 逐个执行主键查询，各 10 万次，id 随机。MySQL 的数据事先读过一遍，都在缓冲池中，比较的是「内存中的数据」：

```c
double a = nowSeconds();
redisReply* rep = cmd(r, "GET user:%d", id);
lat[i] = nowSeconds() - a;
```

```
Redis GET                1.90 秒    52600 次/秒  平均  19.0 微秒  中位数  18.4  P99   25.0
Redis PING               1.88 秒    53257 次/秒  平均  18.8 微秒  中位数  18.3  P99   24.7
MySQL 主键查询       3.48 秒    28724 次/秒  平均  34.8 微秒  中位数  35.1  P99   40.3
MySQL SELECT 1           3.06 秒    32629 次/秒  平均  30.6 微秒  中位数  30.8  P99   35.8
```

Redis 只快了不到一倍？注意两行对照：**`GET` 和什么都不做的 `PING` 几乎一样快**，MySQL 的主键查询和 `SELECT 1` 也只差 4 微秒。也就是说，客户端看到的时间，大部分不是花在查找数据上。

再看服务器自己统计的「执行命令花了多少时间」。Redis 的 `INFO commandstats`，MySQL 的 `performance_schema`：

```
cmdstat_get:calls=100000,usec=21175,usec_per_call=0.21,...
cmdstat_ping:calls=100000,usec=5409,usec_per_call=0.05,...

SELECT NAME FROM `rlab` . `user` WHERE `id` = ?    200000    11.5
SELECT ?                                           100000     7.5
```

<figure class="diagram">
<svg viewBox="0 0 700 190" role="img" aria-label="一次请求的时间：Redis GET 在客户端看来要 19.0 微秒，其中服务器执行只占 0.21 微秒；MySQL 主键查询 34.8 微秒，服务器执行 11.5 微秒。其余时间是网络往返、系统调用和协议处理">
  <defs><marker id="rtm-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rtm-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="140.0" y="50.0" text-anchor="end" style="fill:var(--fg);font-size:12px">Redis GET</text><rect x="150" y="30" width="228.0" height="28" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><rect x="262.7" y="30" width="2.5" height="28" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="264.0" y="74.0" text-anchor="middle" style="fill:var(--warn);font-size:11px">服务器 0.21</text><text x="386.0" y="49.0" text-anchor="start" style="fill:var(--muted);font-size:11px">19.0 微秒</text><text x="140.0" y="112.0" text-anchor="end" style="fill:var(--fg);font-size:12px">MySQL 主键查询</text><rect x="150" y="92" width="417.6" height="28" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><rect x="289.8" y="92" width="138.0" height="28" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="358.8" y="136.0" text-anchor="middle" style="fill:var(--warn);font-size:11px">服务器 11.5</text><text x="575.6" y="111.0" text-anchor="start" style="fill:var(--muted);font-size:11px">34.8 微秒</text><text x="150.0" y="164.0" text-anchor="start" style="fill:var(--muted);font-size:11px">灰色：网络往返、系统调用、协议解析；黄色：服务器真正执行命令</text><text x="150.0" y="182.0" text-anchor="start" style="fill:var(--muted);font-size:11px">服务器上 Redis 比 MySQL 快 55 倍，但被往返冲淡了</text>
  </g>
</svg>
</figure>

| | 服务器执行 | 客户端看到 |
|---|---|---|
| Redis GET | 0.21 微秒 | 19.0 微秒 |
| MySQL 主键查询 | 11.5 微秒（解析、优化、B+ 树查找、权限……） | 34.8 微秒 |

**在服务器上，Redis 比 MySQL 快 55 倍**：一次哈希表查找，没有 SQL 解析、执行计划、事务和锁。但每次请求都要经过网络往返、系统调用、协议解析，这一段在同一台机器的两个容器之间就要 18 微秒左右，把 55 倍的差距冲淡成了 1.8 倍。跨机器、跨机房时，往返更是动辄几百微秒到几毫秒。

这带来两个结论：

1. Redis 的单次请求很难比一次网络往返更快，**减少往返次数**往往比优化命令更有效，这就是下一篇的 Pipeline；
2. Redis 真正的优势在**服务器端的开销极小**：一个线程每秒能执行上百万条简单命令。很多客户端同时访问时，MySQL 先被 CPU 拖住，Redis 还很轻松（第 7 篇的实测：同样 8 个线程，加了 Redis 缓存以后吞吐量翻了一倍）。

## 6. 什么时候不该用 Redis

- **数据量远大于内存**。内存比磁盘贵得多，几百 GB 的历史订单应该放在 MySQL 中；
- **需要复杂查询**。「上个月消费超过 1000 元的北京用户」这种查询，Redis 只能自己维护各种索引，很容易出错；
- **不能丢的数据只存在 Redis 中**。即使开了持久化，也有丢数据的窗口（第 5、6 篇实测）；
- **需要多个操作一起成功或者一起失败**。`MULTI`/`EXEC` 保证一组命令连续执行，但其中一条失败了，前面的不会回滚。

## 7. 常见错误

1. **在生产环境中执行 `KEYS *`**、`FLUSHALL`、对大集合执行 `SMEMBERS`/`HGETALL`。单线程执行，一条慢命令会挡住所有的客户端（下一篇）。
2. **「先读再写」实现计数、扣库存**。两条命令之间会被别的客户端插入，用 `INCR`/`DECR` 或 Lua 脚本。
3. **把对象序列化成一个大字符串**，每次只改一个字段也要整体读出、整体写回。字段经常单独修改时用哈希。
4. **key 不加过期时间**，缓存越积越多，内存耗尽（第 4 篇）。
5. **把 Redis 当成主数据库**，又没有考虑持久化和复制的丢数据窗口。

## 8. 小结

- Redis 是内存中的 key-value 数据库，value 有字符串、列表、哈希、集合、有序集合等类型，每种类型一组命令；
- 典型用途：计数器和验证码（字符串）、队列（列表）、对象（哈希）、标签（集合）、排行榜（有序集合）；
- 单线程执行命令，每条命令都是原子的；「先 GET 再 SET」在 8 个线程下丢了 78% 的更新，INCR 一次都没丢；
- 服务器端 GET 只要 0.21 微秒，比 MySQL 主键查询快 55 倍；但客户端看到的 19 微秒大部分是网络往返。

## 9. 练习

1. 用 Redis 实现「每个用户每分钟最多发 5 条短信」，写出命令。
2. 一个文章网站要显示「热门文章前 10 名」，热度 = 点赞数 × 2 + 评论数，点赞和评论随时发生。用哪种类型、哪些命令？
3. 第 4 节的计数器，如果每次要「加一，并且结果不能超过 100」，只用 INCR 还够吗？

<details>
<summary>点击查看答案</summary>

1. key 用 `sms:用户id:当前分钟`（比如 `sms:42:202611071030`）。每次发送前执行 `INCR` 得到这一分钟内是第几条，如果结果是 1，说明是这一分钟的第一条，再执行 `EXPIRE key 60`；结果大于 5 就拒绝。`INCR` 是原子的，多个请求同时到达也不会超发。（更严谨的写法是把 INCR 和 EXPIRE 放进一个 Lua 脚本，避免 INCR 之后程序崩溃，key 永远不过期。）

2. 有序集合 `hot:article`，成员是文章 id，分数是热度。点赞时 `ZINCRBY hot:article 2 文章id`，评论时 `ZINCRBY hot:article 1 文章id`，取前 10 名用 `ZREVRANGE hot:article 0 9 WITHSCORES`。都是 O(log n)，不需要每次重新计算所有文章的热度。

3. 不够。`INCR` 本身不会检查上限，先 `GET` 判断再 `INCR` 又回到了两条命令之间被插入的问题：两个客户端同时读到 99，都判断可以加，结果变成 101。可以 INCR 之后检查结果，超过 100 就 `DECR` 回去（结果短暂地超过 100，但最终正确）；或者用一个 Lua 脚本，在服务器上一次完成「读、判断、加一」。

</details>
