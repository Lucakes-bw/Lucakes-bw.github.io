---
title: 分区与分库分表
date: 2026-11-08 10:10:00
updated: 2026-11-08 10:10:00
categories:
  - 数据库
  - 数据库设计
tags:
  - 数据库
  - MySQL
  - 表设计
  - 分库分表
order: 4
description: 一张表大到一定程度，就要拆。分区是在一个 MySQL 内部按月拆成多个物理文件：实测按月份统计从 0.85 秒降到 0.06 秒，删掉一个月的 34 万行从 3.2 秒、27 MB binlog 变成 0.00 秒、206 字节；但查询条件中没有分区键时，13 个分区都要查一遍，分区裁剪还总会带上第一个分区。分库分表把数据拆到多个库：带用户 id 的查询只访问一个分片，翻到第 10 万页时，4 个分片却要各返回 100 万行、合并 101 MB 数据，才能取出 10 行。
top_group_list: true
disableNunjucks: true
viz: [db-ddl]
---

> **本文目标**：理解分区的原理，会建按时间的 RANGE 分区，知道主键的限制和分区裁剪的条件；知道分区适合什么（删除旧数据、按时间范围扫描），不适合什么；理解分库分表的分片键、路由、跨分片查询、全局 ID 和扩容问题；知道在拆分之前应该先做什么。
>
> **前置知识**：本章前几篇；第 3 章的索引和 `EXPLAIN`；第 5 章的主从复制。
>
> **实验环境**：MySQL 8.4.13（Docker）。上一篇的订单表，400 万行，下单时间均匀分布在 2025 年全年。

## 1. 表太大以后

一张订单表每天新增 100 万行，一年就是 3.6 亿行。即使每个查询都有合适的索引，也会遇到这些问题：

- **删除旧数据很慢**：只保留一年的订单，每天要删掉 100 万行；
- **单机的容量和写入能力有上限**：磁盘装不下，写入的 QPS 超过一台机器的能力；
- **维护操作越来越慢**：备份、加索引、改表（上一篇）都和数据量成正比。

两种拆法：

- **分区**：还是一个 MySQL、一张逻辑上的表，在存储层按规则拆成多个物理文件；
- **分库分表**：拆到多张表、多个数据库甚至多台机器上，由应用或中间件决定每一行放在哪里。

## 2. 分区

### 2.1 按月分区

```sql
CREATE TABLE orders_p (
  id INT NOT NULL AUTO_INCREMENT,
  user_id INT NOT NULL,
  amount DECIMAL(10,2) NOT NULL,
  status TINYINT NOT NULL,
  created_at DATETIME NOT NULL,
  note VARCHAR(100) NOT NULL,
  PRIMARY KEY (id, created_at)
)
PARTITION BY RANGE (TO_DAYS(created_at)) (
  PARTITION p202501 VALUES LESS THAN (TO_DAYS('2025-02-01')),
  PARTITION p202502 VALUES LESS THAN (TO_DAYS('2025-03-01')),
  ...
  PARTITION p202512 VALUES LESS THAN (TO_DAYS('2026-01-01')),
  PARTITION pmax VALUES LESS THAN MAXVALUE
);
```

每个分区是一个独立的 `.ibd` 文件（有自己的聚簇索引和二级索引）：

```
41943040 orders_p#p#p202502.ibd
46137344 orders_p#p#p202503.ibd
...
46137344 orders_p#p#p202512.ibd
  114688 orders_p#p#pmax.ibd
```

插入时，MySQL 根据 `created_at` 算出这一行属于哪个分区。`pmax` 兜底：没有它，插入一行 2026 年的订单就会报错。实际使用时，要定期（比如每月）用 `REORGANIZE PARTITION` 从 `pmax` 中拆出下一个月的分区。

除了 `RANGE`，还有 `LIST`（按枚举值，比如地区）、`HASH`/`KEY`（按哈希均匀分布）。按时间的 `RANGE` 最常用。

### 2.2 主键必须包含分区键

主键只写 `id` 会怎样：

```
CREATE TABLE orders_bad (id INT AUTO_INCREMENT PRIMARY KEY, created_at DATETIME NOT NULL)
PARTITION BY RANGE (TO_DAYS(created_at)) (...);
ERROR 1503 (HY000): A PRIMARY KEY must include all columns in the table's partitioning function (prefixed columns are not considered).
```

**主键和唯一索引都必须包含分区键。** 因为每个分区的索引是独立的，MySQL 只能在一个分区内部检查唯一性；如果唯一键不包含分区键，同一个值可能出现在两个分区中，它无法发现。

所以分区表的主键成了 `(id, created_at)`，「`id` 唯一」这个约束只能靠自增来保证了；邮箱这类唯一约束，在按时间分区的表上根本无法建立。

### 2.3 分区裁剪

查询条件中有分区键时，MySQL 只访问可能包含结果的分区，叫**分区裁剪**。`EXPLAIN` 的 `partitions` 列显示要访问哪些分区：

```
EXPLAIN SELECT COUNT(*), SUM(amount) FROM orders_p WHERE created_at >= '2025-03-01' AND created_at < '2025-04-01';
| table    | partitions      | type | key  | rows   |
| orders_p | p202501,p202503 | ALL  | NULL | 676107 |
```

<div class="dsviz" data-viz="part-prune"></div>

查 3 月，只扫描了 2 个分区，而不是 13 个。但为什么有 **1 月**？

`TO_DAYS(NULL)` 的结果是 `NULL`，而 `RANGE` 分区把 `NULL` 放进**第一个分区**。MySQL 不能确定第一个分区中没有 `NULL`，所以只要分区函数是 `TO_DAYS` 这类函数，裁剪时总会带上第一个分区。用一个小表验证：

```
CREATE TABLE t_null_probe (d DATETIME) PARTITION BY RANGE (TO_DAYS(d)) (
  PARTITION p0 VALUES LESS THAN (0), PARTITION p202501 ..., PARTITION p202502 ..., PARTITION p202503 ..., PARTITION pmax ...);
EXPLAIN SELECT * FROM t_null_probe WHERE d >= '2025-03-01' AND d < '2025-04-01';
| t_null_probe | p0,p202503 | ...
INSERT INTO t_null_probe VALUES (NULL);       -- 这一行进了 p0
```

**解决办法：第一个分区放一个永远为空的 `p0 VALUES LESS THAN (0)`**，被带上的就是这个空分区，几乎没有代价。

裁剪的条件要写成对分区键本身的比较。对它套上函数，裁剪就失效了：

```
EXPLAIN SELECT COUNT(*) FROM orders_p WHERE YEAR(created_at) = 2025 AND MONTH(created_at) = 3;
| orders_p | p202501,p202502,p202503,...,p202512,pmax | index | ...
```

这和第 3 章「对索引列使用函数，索引失效」是一回事。

### 2.4 按时间范围扫描

统计 3 月的订单数和总金额（34 万行，占全年的 8.5%），普通表 `orders` 和分区表 `orders_p` 比较：

| 表 | 执行方式 | 时间 |
|---|---|---|
| `orders`，`created_at` 上没有索引 | 全表扫描 400 万行 | 0.85 秒 |
| `orders_p` | 只扫描 3 月和 1 月两个分区（原因见上一节） | **0.06 秒** |
| `orders`，加上 `created_at` 的索引 | 优化器仍然选择全表扫描 | 0.85 秒 |
| `orders`，`FORCE INDEX` 强制走索引 | 索引找到 34 万个主键，逐个回表 | 2.96 秒 |
| `orders`，只 `COUNT(*)`（索引覆盖） | 只扫描索引 | 0.01 秒 |

普通表上加了索引，优化器却没有用：要取 `amount`，34 万行都要回表（第 3 章），34 万次随机读比顺序扫一遍全表还慢，强制走索引用了 2.96 秒，证明优化器是对的。

分区表快的原因是：**同一个月的数据物理上放在一起**，扫描一个分区就是顺序读这个月的全部数据。在「按时间范围统计、扫描大量的行」这种查询上，分区比二级索引有效得多。

### 2.5 删除旧数据：分区的最大用处

只保留最近 11 个月，删掉 1 月的数据（34 万行）：

```
SHOW BINARY LOG STATUS;                                  -- 879095793
DELETE FROM orders WHERE created_at < '2025-02-01';
Query OK, 339590 rows affected (3.22 sec)
SHOW BINARY LOG STATUS;                                  -- 907403339
ALTER TABLE orders_p DROP PARTITION p202501;
Query OK, 0 rows affected (0.00 sec)
SHOW BINARY LOG STATUS;                                  -- 907403545
```

<figure class="diagram">
<svg viewBox="0 0 700 200" role="img" aria-label="删除一个月的订单（34 万行）：DELETE 用了 3.22 秒，写了 27 MB binlog，表空间文件大小不变；DROP PARTITION 用了 0.00 秒，binlog 只有 206 字节，分区文件直接被删除">
  <defs><marker id="pdr-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="pdr-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="20.0" y="24.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">DELETE FROM orders WHERE …</text><rect x="20" y="36" width="320" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="180.0" y="56.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">3.22 秒</text><rect x="20" y="74" width="320" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="180.0" y="94.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">binlog 27 MB（每一行）</text><rect x="20" y="112" width="320" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="180.0" y="132.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">逐行写 undo、redo、改索引</text><rect x="20" y="150" width="320" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="180.0" y="170.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">文件大小不变</text><text x="365.0" y="24.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">ALTER TABLE … DROP PARTITION</text><rect x="365" y="36" width="320" height="32" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="525.0" y="56.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">0.00 秒</text><rect x="365" y="74" width="320" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="525.0" y="94.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">binlog 206 字节（一条 DDL）</text><rect x="365" y="112" width="320" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="525.0" y="132.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">删掉一个 .ibd 文件</text><rect x="365" y="150" width="320" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="525.0" y="170.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">空间还给操作系统</text>
  </g>
</svg>
</figure>

| | 耗时 | 写入 binlog | 磁盘空间 |
|---|---|---|---|
| `DELETE` 34 万行 | 3.22 秒 | 28307546 字节（27 MB） | 不释放 |
| `DROP PARTITION` | **0.00 秒** | **206 字节** | 分区文件直接删除 |

- `DELETE` 要逐行删除，每行都要写 undo、redo，修改所有的索引，加行锁；binlog 中记下了被删的每一行（行格式），从库要再删一遍；
- `DROP PARTITION` 只是删掉一个文件，binlog 中只有一条 DDL 语句；
- `DELETE` 以后，空间留在 `.ibd` 文件内部，可以被以后的插入重用，但不会还给操作系统。又删掉 30.7 万行以后，`orders.ibd` 仍然是 641728512 字节，一个字节都没少。

日志、流水、订单这类「按时间写入、按时间过期」的数据，按时间分区、到期删分区，是分区最有价值的用法。

### 2.6 分区的代价

```
EXPLAIN SELECT * FROM orders_p WHERE id = 12345;
| orders_p | p202501,p202502,...,p202512,pmax | ref | PRIMARY | ... | rows 1 |
```

**查询条件中没有分区键，就要访问所有的分区**：按 `id` 查一个订单，在 13 个分区的主键索引中各查一次。加上 `created_at` 才能裁剪到一个分区：

```
EXPLAIN SELECT * FROM orders_p WHERE id = 12345 AND created_at = (SELECT created_at FROM orders WHERE id = 12345);
| orders_p | p202509 | const | PRIMARY | ...
```

按用户查订单（`user_id` 上的二级索引）也是一样，要在每个分区的索引中各查一次。分区越多，这类查询越慢。

总结一下分区：

- ✓ 按分区键删除、归档旧数据；按分区键范围扫描大量数据；
- ✗ 查询不带分区键时要访问所有分区；唯一约束必须包含分区键；
- ✗ 不能突破单机的容量和写入能力，所有分区仍然在同一个 MySQL 中。

## 3. 分库分表

### 3.1 按分片键拆分

单机扛不住的时候，就要把数据拆到多个数据库（通常在多台机器上）。最常见的是**水平拆分**：每个分片的表结构相同，存不同的行。按 `user_id % 4` 拆成 4 个分片：

```
| a       | s0     | s1     | s2     | s3     |
| 3660410 | 914091 | 915041 | 916954 | 914324 |
```

<figure class="diagram">
<svg viewBox="0 0 700 230" role="img" aria-label="按 user_id % 4 把订单拆到 4 个分片：带 user_id 的查询由路由算出分片，只访问一个；不带分片键的查询要发给所有分片，再合并结果">
  <defs><marker id="psh-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="psh-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="92" width="110" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="75.0" y="116.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">应用</text><rect x="170" y="92" width="150" height="40" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="245.0" y="108.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">路由</text><text x="245.0" y="123.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">user_id % 4</text><line x1="130" y1="112" x2="168" y2="112" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#psh-a)"/><rect x="420" y="14" width="150" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="495.0" y="30.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">分片 0</text><text x="495.0" y="45.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">914,091 行</text><line x1="320" y1="112" x2="418" y2="34" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#psh-a)"/><rect x="420" y="66" width="150" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="495.0" y="82.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">分片 1</text><text x="495.0" y="97.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">915,041 行</text><line x1="320" y1="112" x2="418" y2="86" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#psh-a)"/><rect x="420" y="118" width="150" height="40" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="495.0" y="134.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">分片 2</text><text x="495.0" y="149.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">916,954 行</text><line x1="320" y1="112" x2="418" y2="138" style="stroke:var(--accent);stroke-width:1.5" marker-end="url(#psh-a)"/><rect x="420" y="170" width="150" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="495.0" y="186.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">分片 3</text><text x="495.0" y="201.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">914,324 行</text><line x1="320" y1="112" x2="418" y2="190" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#psh-a)"/><text x="580.0" y="120.0" text-anchor="start" style="fill:var(--accent);font-size:11px">user_id = 42</text><text x="580.0" y="136.0" text-anchor="start" style="fill:var(--accent);font-size:11px">→ 只访问分片 2</text><text x="20.0" y="222.0" text-anchor="start" style="fill:var(--muted);font-size:11px">实线：带分片键，只访问一个分片；虚线：不带分片键，访问所有分片再合并</text>
  </g>
</svg>
</figure>

决定一行放在哪里的列叫**分片键**。查询带着分片键时，应用（或者中间件）算出分片，只访问那一个：

```
SELECT COUNT(*), SUM(amount) FROM orders_s2 WHERE user_id = 42;      -- 42 % 4 = 2
31	17205.82
```

和单表的结果一样，而其他 3 个分片完全不用参与。分片键要选**大多数查询都会带上的列**：电商的订单按用户 id 拆，「我的订单」只访问一个分片。

### 3.2 不带分片键的查询

卖家要看「所有订单按时间倒序，第 N 页」，查询中没有 `user_id`，只能**问所有的分片，再合并**：

- 第 1 页：每个分片取最新的 10 条，合并 40 条，排序，取前 10 条；
- 第 N 页：每个分片必须取前 `10 × N` 条，因为不知道第 N 页的这 10 条落在哪个分片上。

<div class="dsviz" data-viz="shard-query"></div>

实测（每页 10 条，按时间倒序），并把合并结果和单表的结果逐行比较：

```
== 第 1 页
合并的行数：40
合并后取第 1～10 条：和单表的结果一致
== 第 1001 页
合并的行数：40040
合并后取第 10001～10010 条：和单表的结果一致
== 第 10001 页
合并的行数：400040，大小 11090422 字节
合并后取第 100001～100010 条：和单表的结果一致
== 第 100001 页
单表：0.06 sec
分片 0 取前 1000010 条：0.11 sec
分片 1 取前 1000010 条：0.10 sec
分片 2 取前 1000010 条：0.11 sec
分片 3 取前 1000010 条：0.09 sec
合并的行数：3660410，大小 101474805 字节
合并后取第 1000001～1000010 条：和单表的结果一致
```

**翻到第 10 万页时，为了这 10 行，4 个分片一共返回了 366 万行、101 MB 的数据**（分片 3 只有 91 万行，全部返回了）。单表只需要在索引上跳过 100 万条，返回 10 行。分片越多、页码越深，代价越大。

办法：

- **不允许跳页**，只能「下一页」：记住上一页最后一条的时间和 id，下一页用 `WHERE (created_at, id) < (上次的值) ORDER BY … LIMIT 10`，每个分片只取 10 条（游标分页，第 3 章也推荐过）；
- 这类全局查询交给专门的系统：把订单同步到 Elasticsearch、ClickHouse 或者一个汇总库，按卖家维度再存一份（按卖家 id 分片）。

`COUNT`、`SUM` 可以在每个分片上算完再加起来；`AVG` 要分别求和与计数再相除；`COUNT(DISTINCT …)`、跨分片的 `JOIN` 就麻烦得多了。

### 3.3 全局唯一的 ID

每个分片的自增 id 是各自从 1 开始的，不同分片上会出现相同的 id。常见的方案：

| 方案 | 做法 | 优点 | 缺点 |
|---|---|---|---|
| 步长自增 | 分片 k 的 `auto_increment_offset = k`，`auto_increment_increment = 分片数` | 简单 | 分片数改变时很难调整 |
| 号段 | 一个中心表发放号段（每次取 1000 个），应用在内存中分配 | 递增、紧凑 | 依赖中心服务 |
| 雪花算法 | 64 位：时间戳（毫秒）+ 机器编号 + 序号 | 不依赖中心，大致递增 | 依赖时钟，时钟回拨会出错 |
| UUID | 128 位随机 | 无需协调 | 太长、随机，插入性能差（第 3 章实测二级索引大了近 4 倍） |

雪花算法这类「按时间大致递增」的 64 位整数最常用：它满足第 2 章说的主键要求（短、递增），又能在多台机器上独立生成。

### 3.4 其他代价

- **跨分片事务**：转账涉及两个用户，可能在两个分片上，单机的事务（第 4 章）不再适用，需要分布式事务或者最终一致的方案（消息、补偿）；
- **扩容**：`user_id % 4` 扩成 `% 8`，大约一半的数据要换分片。常见的做法是一开始就拆成很多个逻辑分片（比如 1024 个），多个逻辑分片放在一台机器上，扩容时只是把一部分逻辑分片整体搬到新机器上；
- **运维**：备份、改表、监控都要在每个分片上做，表结构必须保持一致；
- **中间件**：ShardingSphere、Vitess、MyCat 等负责路由、合并、全局 ID，但上面这些限制它们只能缓解，不能消除。

## 4. 拆之前

分库分表的复杂度是长期的，而且很难撤回。在这之前，先确认这些手段都用过了：

1. **索引和 SQL**：慢查询大多是缺索引或者写法有问题（第 3 章）；
2. **归档**：一年前的订单很少有人看，移到归档库，或者用分区按月删除；
3. **缓存**：读多写少的数据放进 Redis（第 6 章）；
4. **读写分离**：读请求分给从库（第 5 章）；
5. **升级硬件**：一台 64 核、512 GB 内存、NVMe 磁盘的服务器能支撑的量，往往比想象的大得多；
6. **分区**：解决删除旧数据和按时间扫描的问题。

当数据量或写入量确实超过了一台机器的能力，再按查询模式选好分片键，分库分表。

## 5. 常见错误

1. **以为分区能让所有的查询变快**。查询不带分区键时要访问所有分区，反而更慢。
2. **分区条件中对分区键套函数**（`YEAR(created_at) = 2025`），裁剪失效。
3. **忘了第一个分区总会被扫描**，没有放一个空的 `p0`。
4. **忘了预先建好下个月的分区**，数据全部进了 `pmax`，按月删除也就失效了；没有 `pmax` 时则直接插入失败。
5. **用 `DELETE` 清理按时间过期的大量数据**，又慢又产生大量的 binlog 和从库延迟。
6. **分片键选得不对**，大部分查询都要访问所有的分片。
7. **在分库分表之后还支持任意跳页**，深分页让每个分片返回大量的数据。
8. **过早地分库分表**，承担了长期的复杂度，换来的性能其实用索引、缓存、读写分离就能解决。

## 6. 小结

- 分区在一个 MySQL 内部把表拆成多个物理文件；主键和唯一键必须包含分区键；条件中有分区键（且没有套函数）时才能裁剪；第一个分区总会被带上，放一个空的 `p0`；
- 实测：按月统计 0.85 秒 → 0.06 秒；删一个月从 `DELETE` 的 3.22 秒、27 MB binlog 变成 `DROP PARTITION` 的 0.00 秒、206 字节；但按 id 查询要访问全部 13 个分区；
- 分库分表按分片键把数据拆到多个库：带分片键的查询只访问一个分片，不带的要访问所有分片再合并，第 10 万页要合并 366 万行、101 MB；
- 还要解决全局 ID、跨分片事务、扩容等问题。先用尽索引、归档、缓存、读写分离、分区，再考虑分库分表。

## 7. 练习

1. 一张访问日志表，每天 5000 万行，只保留 30 天，查询几乎都是「某一天某个接口的错误数」。怎样设计分区和索引？
2. 订单按 `user_id` 分了 16 个分片。客服要按订单号查订单（只知道订单号），怎么办？
3. 为什么按时间分区的表上，不能保证 `order_no`（订单号）唯一？有什么办法？

<details>
<summary>点击查看答案</summary>

1. 按天 `RANGE` 分区（`TO_DAYS(created_at)`），第一个分区放一个空的 `p0`，每天由定时任务新建明天的分区、删除 30 天前的分区（`DROP PARTITION`，瞬间完成、几乎不产生 binlog）。查询条件写成 `created_at >= '某天' AND created_at < '某天 + 1'`，裁剪到一个分区；分区内建 `(api, status)` 的索引（或者 `(api, status, created_at)`），统计某个接口的错误数只扫描一个分区中的一小段索引。

2. 方案一：订单号中编码进 `user_id` 的分片信息（比如订单号的末几位是 `user_id % 1024`），从订单号就能算出分片，这是最常用的做法。方案二：另建一张「订单号 → 用户 id」的映射表（按订单号分片），先查映射，再去对应的分片查订单，多一次查询。方案三：同时查 16 个分片，只适合很少使用的后台功能。

3. 唯一索引必须包含分区键，`UNIQUE (order_no)` 无法建立，只能建 `UNIQUE (order_no, created_at)`，它只保证同一个时间的订单号不重复，两个不同时间的订单仍然可以有相同的订单号。办法：订单号由全局唯一的 ID 生成器产生（雪花算法、号段），从源头保证不重复；或者另建一张不分区的表，只存 `order_no` 并在其上建唯一索引，插入订单时在同一个事务中先插入它（代价是每个订单多写一行）。

</details>
