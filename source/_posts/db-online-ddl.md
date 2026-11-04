---
title: 表结构的变更：在线 DDL 与元数据锁
date: 2026-11-08 10:00:00
updated: 2026-11-08 10:00:00
categories:
  - 数据库
  - 数据库设计
tags:
  - 数据库
  - MySQL
  - 表设计
order: 3
description: 表设计好了也总要改：加列、加索引、改类型。在 400 万行的订单表上实测：加一列瞬间完成，加索引 1.6 秒且期间写入照常；但把 DECIMAL(10,2) 改成 DECIMAL(12,2) 要重建整张表，同时到来的 UPDATE 等了 7 秒。最危险的是元数据锁：一个 15 秒没提交的事务，让一条瞬间就能完成的加列语句等了 14 秒，排在它后面的普通查询也全部卡住。而 lock_wait_timeout 的默认值是一年。
top_group_list: true
disableNunjucks: true
viz: [db-ddl]
---

> **本文目标**：知道 `ALTER TABLE` 的三种算法（INSTANT、INPLACE、COPY）分别做了什么，哪些操作用哪种；会判断一个改表操作会不会阻塞读写；理解元数据锁，能避免「改表把整个库卡死」；知道大表改表的常用做法。
>
> **前置知识**：前两篇；第 3 章的 InnoDB 存储结构（聚簇索引、二级索引）；第 4 章的事务和锁。
>
> **实验环境**：MySQL 8.4.13（Docker），`innodb_buffer_pool_size` 128 MB。订单表 `orders`：自增主键、用户 id、金额、状态、下单时间、一个 60 字节的备注，先用 100 万行，后面用 400 万行（407 MB）。

## 1. 表结构总会变

前两篇设计的表，上线以后几乎一定会改：

- 新需求：订单要加一个「优惠券 id」列；
- 新查询：要按用户查订单，需要一个 `user_id` 上的索引；
- 估计错了：`DECIMAL(10,2)` 最大 99999999.99，大客户的订单超出了范围；
- 改名：`note` 其实是给客服看的，应该叫 `remark`。

表小的时候，`ALTER TABLE` 一眨眼就完成了，没人在意。表有几亿行、每秒有几千次读写时，改表就成了一个需要计划的操作：**它要多久？期间读写会不会被挡住？**

## 2. 三种算法

MySQL 8 的 `ALTER TABLE` 按操作的不同，选择三种方式之一执行：

<figure class="diagram">
<svg viewBox="0 0 700 230" role="img" aria-label="ALTER TABLE 的三种算法。INSTANT 只改数据字典，瞬间完成；INPLACE 在引擎内部扫描数据，期间读写照常，修改记在在线日志中最后补上；COPY 把每一行复制到新表，期间只能读，写入要等到结束。实测 400 万行：INSTANT 0.00 秒，INPLACE 加索引 1.55 秒，COPY 改列类型 8.14 秒">
  <defs><marker id="dal-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="dal-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="14.0" y="46.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">INSTANT</text><rect x="90" y="24" width="60" height="32" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="120.0" y="44.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">改字典</text><text x="90.0" y="74.0" text-anchor="start" style="fill:var(--accent);font-size:11px">0.00 秒，读写不受影响</text><text x="14.0" y="112.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">INPLACE</text><rect x="90" y="90" width="24" height="32" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="102.0" y="110.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">锁</text><rect x="116" y="90" width="300" height="32" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="266.0" y="110.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">扫描数据、建新索引（读写照常）</text><rect x="418" y="90" width="60" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="448.0" y="110.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">补日志</text><rect x="480" y="90" width="24" height="32" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="492.0" y="110.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">锁</text><text x="90.0" y="140.0" text-anchor="start" style="fill:var(--accent);font-size:11px">1.55 秒（加索引），写入照常</text><text x="14.0" y="178.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">COPY</text><rect x="90" y="156" width="24" height="32" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="102.0" y="176.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">锁</text><rect x="116" y="156" width="440" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="336.0" y="176.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">逐行复制到新表（写入等待）</text><rect x="558" y="156" width="40" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="578.0" y="176.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">换名</text><text x="90.0" y="206.0" text-anchor="start" style="fill:var(--danger);font-size:11px">8.14 秒，UPDATE 等了 7.14 秒</text><text x="14.0" y="222.0" text-anchor="start" style="fill:var(--muted);font-size:11px">不指定 ALGORITHM 时，MySQL 选当前操作支持的最快的一种；指定了不支持的算法会直接报错</text>
  </g>
</svg>
</figure>

| 算法 | 做了什么 | 耗时 | 期间的读写 |
|---|---|---|---|
| `INSTANT` | 只修改数据字典（表的元数据），不碰数据 | 和表的大小无关，几毫秒 | 不受影响 |
| `INPLACE` | 在存储引擎内部完成：建索引时扫描数据、排序、写入新的 B+ 树；有些操作会重建表 | 和数据量成正比 | 大多数允许并发读写（把期间的修改记在日志中，最后补上） |
| `COPY` | 建一张新结构的临时表，把所有的行一行行复制过去，最后换名 | 和数据量成正比，通常最慢 | **只能读，写入要等它完成** |

不指定时，MySQL 选择当前操作支持的最快的算法；也可以用 `ALGORITHM=…` 指定，如果不支持就直接报错，不会悄悄退回到更慢的算法。

### 2.1 实测

100 万行的表，依次执行：

```
ALTER TABLE orders ADD COLUMN c1 INT
Query OK, 0 rows affected (0.00 sec)
ALTER TABLE orders ADD COLUMN c2 INT, ALGORITHM=INPLACE
Query OK, 0 rows affected (0.80 sec)
ALTER TABLE orders ADD COLUMN c3 INT, ALGORITHM=COPY
Query OK, 1000000 rows affected (0.94 sec)
ALTER TABLE orders ADD INDEX idx_user (user_id)
Query OK, 0 rows affected (0.39 sec)
ALTER TABLE orders MODIFY amount DECIMAL(12,2), ALGORITHM=INPLACE
ERROR 1846 (0A000) at line 6: ALGORITHM=INPLACE is not supported. Reason: Cannot change column type INPLACE. Try ALGORITHM=COPY.
ALTER TABLE orders MODIFY amount DECIMAL(12,2)
Query OK, 1000000 rows affected (1.39 sec)
ALTER TABLE orders DROP COLUMN c1
Query OK, 0 rows affected (0.00 sec)
ALTER TABLE orders RENAME COLUMN note TO remark
Query OK, 0 rows affected (0.00 sec)
ALTER TABLE orders ALTER COLUMN status SET DEFAULT 0
Query OK, 0 rows affected (0.00 sec)
```

- **加列、删列、改名、改默认值：INSTANT，0.00 秒**。同样一列，强制用 INPLACE 要 0.8 秒（重建了表），用 COPY 要 0.94 秒（`1000000 rows affected`：每一行都复制了一遍）；
- **加索引：INPLACE**，扫描数据建一棵新的 B+ 树，0.39 秒；
- **改列的类型：只能 COPY**。指定 INPLACE 时，MySQL 直接报错并给出原因。

这张表只有 100 MB，都在一秒左右。耗时和数据量成正比：一张 100 GB 的表，INPLACE 和 COPY 要的就是十几分钟到几十分钟，而 INSTANT 仍然是几毫秒。

常见操作的算法（MySQL 8.0.29 以后）：

| 操作 | 算法 | 期间写入 |
|---|---|---|
| 加列、删列（任意位置） | INSTANT | ✓ |
| 列改名、改默认值、修改 `ENUM` 末尾的成员 | INSTANT | ✓ |
| 加、删二级索引；索引改名 | INPLACE（不重建表） | ✓ |
| 加、删主键；`OPTIMIZE TABLE`；改行格式 | INPLACE（重建表） | ✓ |
| `VARCHAR` 在 255 字节以内加长 | INPLACE（不重建表） | ✓ |
| 改列的类型（`INT` → `BIGINT`、改 `DECIMAL` 的精度）；改字符集 | COPY | ✗ |
| 把列改为 `NOT NULL` | INPLACE（重建表） | ✓ |

不确定的时候，在测试环境中加上 `ALGORITHM=INSTANT`（或 `INPLACE`, `LOCK=NONE`）试一下：不支持就会报错，比在线上猜好得多。

### 2.2 实测：改表期间的写入

把表扩大到 400 万行（407 MB）。改表开始 1 秒后，另一个会话更新一行，看它等了多久：

```bash
mysql -e "$ALTER" &
sleep 1
mysql -e "UPDATE orders SET amount = amount + 1 WHERE id = 12345"
```

```
=== ALTER TABLE orders ADD INDEX idx_user (user_id)
  同时 UPDATE 一行：Query OK, 1 row affected (0.00 sec)
  ALTER：Query OK, 0 rows affected (1.55 sec)
=== ALTER TABLE orders ADD COLUMN c1 INT, ALGORITHM=INPLACE
  同时 UPDATE 一行：Query OK, 1 row affected (0.00 sec)
  ALTER：Query OK, 0 rows affected (3.23 sec)
=== ALTER TABLE orders MODIFY amount DECIMAL(12,2)
  同时 UPDATE 一行：Query OK, 1 row affected (7.14 sec)
  ALTER：Query OK, 4000000 rows affected (8.14 sec)
=== ALTER TABLE orders ADD COLUMN c2 INT
  同时 UPDATE 一行：Query OK, 1 row affected (0.01 sec)
  ALTER：Query OK, 0 rows affected (0.00 sec)
```

<div class="dsviz" data-viz="ddl-algo"></div>

- 加索引、INPLACE 重建表：UPDATE 立即完成。改表期间的修改被记在一个**在线日志**中，扫描完以后再应用到新的结构上；
- **COPY（改列类型）：UPDATE 等了 7.14 秒**，几乎等到改表结束。COPY 期间只允许读，所有的写入都要排队。线上的表，这意味着所有写这张表的请求在这段时间里全部超时；
- INSTANT：改表本身就是瞬间的，不存在「期间」。

## 3. 元数据锁

### 3.1 一条瞬间完成的语句卡死了整张表

INSTANT 加列只要几毫秒，是不是随时都可以执行？看这个实验：

1. 会话 1：开了一个事务，读了一行订单，然后 15 秒没有提交（模拟程序在事务中调用了一个很慢的外部接口）；
2. 1 秒后，会话 2：`ALTER TABLE orders ADD COLUMN c9 INT`，一个 INSTANT 操作；
3. 2 秒后，会话 3、4：普通的 `SELECT` 一行、`UPDATE` 一行。

```
1.01s 会话 2 开始 ALTER
2.03s 会话 3 开始 SELECT
3.54s SHOW PROCESSLIST：
    id	time	state	info
    32	4	User sleep	SELECT SLEEP(15)
    33	3	Waiting for table metadata lock	ALTER TABLE orders ADD COLUMN c9 INT
    34	2	Waiting for table metadata lock	SELECT id FROM orders WHERE id = 2
    35	1	Waiting for table metadata lock	UPDATE orders SET amount = amount + 1 WHERE id = 3
       会话 4：Query OK, 1 row affected (12.73 sec)
       会话 3：1 row in set (12.95 sec)
       会话 2：Query OK, 0 rows affected (13.96 sec)
```

**瞬间就能完成的加列等了 14 秒；按主键查一行的 `SELECT` 也等了 13 秒。** 这期间所有访问 `orders` 的请求都在 `Waiting for table metadata lock`。如果这是线上的订单表，连接池很快就会被占满，整个应用停止响应。

### 3.2 发生了什么

<figure class="diagram">
<svg viewBox="0 0 700 200" role="img" aria-label="元数据锁的排队：会话 1 的事务持有共享锁不提交；会话 2 的 ALTER 要排他锁，只能等待；会话 3、4 的共享锁虽然和会话 1 兼容，但排在等待中的排他锁后面，也只能等。会话 1 提交后，ALTER 执行，然后才轮到会话 3、4">
  <defs><marker id="dml-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="dml-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="14.0" y="24.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">持有</text><rect x="20" y="36" width="220" height="40" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="130.0" y="52.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">会话 1：长事务</text><text x="130.0" y="67.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">共享锁，15 秒不提交</text><text x="280.0" y="24.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">等待队列（先来先排）</text><rect x="280" y="36" width="130" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="345.0" y="52.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">会话 2：ALTER</text><text x="345.0" y="67.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">要排他锁</text><rect x="420" y="36" width="130" height="40" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="485.0" y="52.3" text-anchor="middle" style="fill:var(--fg);font-size:11px">会话 3：SELECT</text><text x="485.0" y="67.3" text-anchor="middle" style="fill:var(--muted);font-size:10px">共享锁</text><rect x="560" y="36" width="130" height="40" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="625.0" y="52.3" text-anchor="middle" style="fill:var(--fg);font-size:11px">会话 4：UPDATE</text><text x="625.0" y="67.3" text-anchor="middle" style="fill:var(--muted);font-size:10px">共享锁</text><line x1="278" y1="56" x2="242" y2="56" style="stroke:var(--danger);stroke-width:1.5" marker-end="url(#dml-d)"/><line x1="418" y1="56" x2="412" y2="56" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#dml-a)"/><line x1="558" y1="56" x2="552" y2="56" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#dml-a)"/><text x="20.0" y="112.0" text-anchor="start" style="fill:var(--fg);font-size:12px">会话 3、4 和会话 1 本来兼容，却被等待中的 ALTER 挡住（防止排他锁饿死）</text><text x="20.0" y="136.0" text-anchor="start" style="fill:var(--danger);font-size:11px">实测：ALTER 等了 13.96 秒，SELECT 12.95 秒，UPDATE 12.73 秒</text><text x="20.0" y="158.0" text-anchor="start" style="fill:var(--muted);font-size:11px">lock_wait_timeout 默认 31536000 秒（一年）；设为 3 秒后 ALTER 报错放弃，后面的请求只多等了约 2 秒</text><text x="20.0" y="184.0" text-anchor="start" style="fill:var(--accent);font-size:12px">真正的问题：长时间不提交的事务</text>
  </g>
</svg>
</figure>

<div class="dsviz" data-viz="ddl-mdl"></div>

MySQL 用**元数据锁**（metadata lock，MDL）保护表结构：

- 任何访问一张表的语句（`SELECT`、`UPDATE`……）都要先拿这张表的**共享 MDL**，而且**一直持有到事务结束**。事务中读过的表，在事务提交之前，结构不能变；
- `ALTER TABLE` 在开始和结束时需要**排他 MDL**（INPLACE 在中间的长时间扫描期间会降级，允许读写；INSTANT 只需要短暂的一下）。

于是：

1. 会话 1 的事务持有共享 MDL，15 秒不提交；
2. 会话 2 的 `ALTER` 要排他 MDL，和共享锁冲突，开始等待；
3. 会话 3 的 `SELECT` 要共享 MDL。共享锁之间本来不冲突，但**等待队列中已经有一个排他锁的请求了**，为了不让 `ALTER` 永远等下去（锁饥饿），后来的共享锁请求也要排在它后面；
4. 会话 1 提交 → `ALTER` 拿到锁、瞬间完成、释放 → 后面排队的请求才依次执行。

罪魁祸首不是 `ALTER`，而是**那个长时间不提交的事务**；`ALTER` 只是把它的影响放大到了所有人。

### 3.3 lock_wait_timeout

`ALTER` 会等多久？由 `lock_wait_timeout` 决定。它的默认值：

```
lock_wait_timeout | 31536000
```

**31536000 秒，一年。** 也就是说，默认情况下改表会一直等下去，后面的请求也跟着一直排队。

改表之前，在这个会话中把它设短：

```sql
SET lock_wait_timeout = 3;
ALTER TABLE orders ADD COLUMN c9 INT;
```

同样的实验：

```
1.02s 会话 2 开始 ALTER
2.03s 会话 3 开始 SELECT
3.54s SHOW PROCESSLIST：
    33	3	Waiting for table metadata lock	ALTER TABLE orders ADD COLUMN c9 INT
    34	2	Waiting for table metadata lock	SELECT id FROM orders WHERE id = 2
    35	2	Waiting for table metadata lock	UPDATE orders SET amount = amount + 1 WHERE id = 3
       会话 3：1 row in set (1.98 sec)
       会话 2：ERROR 1205 (HY000) at line 1: Lock wait timeout exceeded; try restarting transaction
       会话 4：Query OK, 1 row affected (1.79 sec)
```

`ALTER` 等了 3 秒就放弃了，排在它后面的请求随即继续，只多等了 2 秒左右。改表失败了没关系，过一会儿重试；把整个应用卡住才是事故。

### 3.4 改表之前检查长事务

```sql
-- 运行超过 10 秒的事务
SELECT trx_mysql_thread_id, trx_started, TIMESTAMPDIFF(SECOND, trx_started, NOW()) AS seconds, trx_query
FROM information_schema.innodb_trx
WHERE trx_started < NOW() - INTERVAL 10 SECOND;
```

`trx_query` 为空的长事务最可疑：它正处于「开了事务、什么也没在做」的状态，就像实验中的会话 1。`performance_schema.metadata_locks` 可以查看谁持有、谁在等待哪张表的 MDL。

## 4. 大表怎么改

### 4.1 能 INSTANT 就 INSTANT

加列、删列、改名、改默认值都是 INSTANT 的，表再大也只要几毫秒（前提是拿得到元数据锁）。设计时就要想到：**列的类型宁可一开始就留够**。`INT` 主键快用完了才改成 `BIGINT`，是一次要重建整张表、期间不能写入的 COPY。

### 4.2 INPLACE：注意资源

加索引、重建表虽然不阻塞写入，但会：

- 占用大量的 IO 和 CPU，影响线上查询；
- 需要额外的磁盘空间（重建表时两份数据同时存在）；
- 期间的修改记在在线日志中（大小由 `innodb_online_alter_log_max_size` 限制，默认 128 MB），写入太多时日志会溢出，改表在最后一步失败，前面的工作全部白做；
- **在从库上是串行重放的**：主库上 30 分钟的改表，从库要再执行 30 分钟，期间其他的复制都在它后面排队，从库延迟 30 分钟。

所以要选在低峰期执行，并观察复制延迟。

### 4.3 COPY：用工具代替

需要 COPY 的改表（改类型、改字符集），在大表上直接执行意味着长时间不能写入。常用的做法是用 **gh-ost** 或 **pt-online-schema-change**：

<figure class="diagram">
<svg viewBox="0 0 700 210" role="img" aria-label="gh-ost 和 pt-online-schema-change 的做法：建一张新结构的影子表，分批复制原表的数据，同时把原表上的修改（通过 binlog 或触发器）同步过去，追平以后用一次原子的 RENAME 交换两张表">
  <defs><marker id="dgh-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="dgh-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="40" width="150" height="44" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="95.0" y="58.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">orders</text><text x="95.0" y="73.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">原表，照常读写</text><rect x="360" y="40" width="170" height="44" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="445.0" y="58.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">_orders_gho</text><text x="445.0" y="73.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">影子表（新结构）</text><line x1="170" y1="54" x2="358" y2="54" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#dgh-a)"/><text x="264.0" y="48.0" text-anchor="middle" style="fill:var(--link);font-size:11px">② 分批复制（可暂停、限速）</text><line x1="170" y1="74" x2="358" y2="74" style="stroke:var(--accent);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#dgh-a)"/><text x="264.0" y="94.0" text-anchor="middle" style="fill:var(--accent);font-size:11px">③ 同步修改（binlog / 触发器）</text><text x="360.0" y="26.0" text-anchor="start" style="fill:var(--muted);font-size:11px">① 建影子表</text><rect x="560" y="40" width="120" height="44" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="620.0" y="58.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">RENAME</text><text x="620.0" y="73.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">④ 原子地交换</text><line x1="530" y1="62" x2="558" y2="62" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#dgh-a)"/><text x="20.0" y="140.0" text-anchor="start" style="fill:var(--fg);font-size:12px">整个过程可能几个小时，写入一直不受影响；只有第 ④ 步需要排他元数据锁，而且是瞬间的</text><text x="20.0" y="162.0" text-anchor="start" style="fill:var(--muted);font-size:11px">代价：需要两倍的磁盘空间；第 ③ 步追不上写入时可能永远完成不了</text>
  </g>
</svg>
</figure>

1. 创建一张新结构的**影子表**；
2. 分批把原表的数据复制到影子表，每批很小，不长时间占用资源，可以随时暂停；
3. 复制期间原表上的修改，通过触发器（pt-osc）或者读取 binlog（gh-ost）同步到影子表；
4. 数据追平以后，用一次原子的 `RENAME TABLE orders TO orders_old, orders_new TO orders` 切换。只有这一步需要排他的元数据锁，而且是瞬间的。

整个过程可能要几个小时，但写入一直不受影响，也能控制对线上的压力。

### 4.4 应用和表结构要分步变化

改表和发布程序不可能在同一瞬间完成，中间总有一段时间，新旧两个版本的程序同时在运行。比如把 `note` 改名为 `remark`：如果先改名，旧版本的程序还在读写 `note`，立即报错。正确的做法是**扩展—迁移—收缩**：

1. 加新列 `remark`（INSTANT）；
2. 发布新版本程序：同时写两列，读新列（读不到时读旧列）；
3. 后台把旧数据从 `note` 复制到 `remark`（分批）；
4. 确认没有程序再使用 `note` 以后，删除旧列。

每一步都能单独回滚。删列放在最后，而且在删之前要确认真的没人用了（比如先把权限收回、观察几天）。

## 5. 常见错误

1. **在线上直接执行要 COPY 的改表**，表越大，写入被挡住的时间越长。先用 `ALGORITHM=INPLACE, LOCK=NONE` 在测试环境确认。
2. **改表时不设 `lock_wait_timeout`**，遇到长事务就一直等，后面的所有请求跟着排队。
3. **程序中有长事务**：事务里调用外部接口、等用户输入，或者忘了提交（连接池中的连接带着未提交的事务被复用）。
4. **在业务高峰期加索引**，IO 被占满，或者从库延迟几十分钟。
5. **先改表结构再发布程序**（或者反过来），新旧版本不兼容，中间出错。
6. **类型一开始给得太紧**，日后改类型只能 COPY 重建。

## 6. 小结

- `ALTER TABLE` 有三种算法：INSTANT 只改元数据，几毫秒；INPLACE 在引擎内部完成，大多不阻塞写入；COPY 复制整张表，期间不能写入；
- 实测 400 万行：加索引 1.55 秒、期间写入照常；改列类型 8.14 秒，同时到来的 UPDATE 等了 7.14 秒；
- 所有语句都持有元数据锁直到事务结束。一个 15 秒的长事务，让瞬间完成的加列等了 14 秒，后面的查询也全部排队；`lock_wait_timeout` 默认一年，改表前要设短；
- 大表改表：能 INSTANT 就 INSTANT；INPLACE 注意资源和从库延迟；COPY 用 gh-ost 等工具；应用和表结构按「扩展—迁移—收缩」分步变化。

## 7. 练习

1. 一张 2 亿行的表，主键是 `INT UNSIGNED AUTO_INCREMENT`，已经用到了 38 亿。怎样把它改成 `BIGINT`？
2. 执行 `ALTER TABLE … ADD INDEX` 时，`SHOW PROCESSLIST` 中看到它的状态是 `Waiting for table metadata lock`，应该怎么办？
3. 为什么实验中的会话 3 只是读一行数据，也会被 `ALTER` 挡住？读和读之间不是不冲突吗？

<details>
<summary>点击查看答案</summary>

1. 改类型是 COPY，直接执行会在几十分钟到几小时内无法写入，不可行。用 gh-ost 或 pt-online-schema-change：创建一张主键为 `BIGINT` 的影子表，分批复制数据，同步增量的修改，最后原子地换名。还要检查所有引用这个主键的外键列、应用程序中存放 id 的变量和其他表，它们也要改成 64 位，否则换完以后照样溢出。INT UNSIGNED 的上限约 42.9 亿，还剩约 4 亿的空间，要根据增长速度留出足够的时间完成这件事。

2. 说明有事务持有这张表的元数据锁，`ALTER` 正在等它，而后面所有访问这张表的请求都会排在 `ALTER` 后面。应该立即终止这个 `ALTER`（`KILL QUERY 线程id`），让业务恢复；然后用 `information_schema.innodb_trx` 和 `performance_schema.metadata_locks` 找出那个长事务，弄清楚它为什么不提交；之后设置 `lock_wait_timeout` 再重试。

3. 读和读（共享 MDL 和共享 MDL）确实不冲突。但等待队列中排着一个排他 MDL 的请求（`ALTER`），MySQL 让后来的共享锁请求排在它后面，否则只要不断有新的读请求，`ALTER` 就可能永远拿不到锁（锁饥饿）。代价就是一个等待中的 `ALTER` 会挡住之后所有的读写。

</details>
