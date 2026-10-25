---
title: EXPLAIN 与慢查询
date: 2026-10-25 10:20:00
updated: 2026-10-25 10:20:00
categories:
  - 数据库
  - 索引与查询优化
tags:
  - 数据库
  - MySQL
  - 索引
  - 性能
order: 3
description: 优化查询的流程：先找出慢的查询，再用 EXPLAIN 看它是怎样执行的，改进后再测量。本文在 100 万行的表上实测：慢查询日志记下了一条扫描 100 万行只返回 1 行的查询，加上索引后从 182 毫秒降到 0.003 毫秒；优化器在要回表 90 万行时放弃索引，强制使用索引反而慢了 10 倍；ORDER BY 一个没有索引的列要排序 100 万行；OFFSET 900000 的分页用了 69 毫秒，改成从上一页的最后一个 id 往后取只要 0.07 毫秒；连接的列没有索引时，要扫描 20 万行订单。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用慢查询日志和 `performance_schema` 找出需要优化的查询；能读懂 `EXPLAIN` 的输出，特别是 `type`、`key`、`rows`、`Extra`；会用 `EXPLAIN ANALYZE` 查看实际的执行情况；理解优化器为什么有时不用索引；会优化排序、分组、深分页和连接。
>
> **前置知识**：上一篇《B+ 树索引》，以及它的测试表 `t`（100 万行，主键 `id`，二级索引 `idx_k`）。
>
> **实验环境**：MySQL 8.4.13（Docker，2 个 CPU 核），缓冲池 128 MB。本文的耗时都是**服务器端的执行时间**，从 `performance_schema.events_statements_history` 中读取，不包括客户端启动、网络传输的时间。每条查询连续执行 3 次。

## 1. 找出慢查询

优化之前要先知道哪些查询慢。凭感觉猜往往会猜错，要让数据库自己记录。

### 1.1 慢查询日志

**慢查询日志**记录执行时间超过 `long_query_time` 秒的语句：

```sql
SET GLOBAL slow_query_log = ON;
SET GLOBAL long_query_time = 0.1;                       -- 超过 0.1 秒就记录
SET GLOBAL slow_query_log_file = '/var/lib/mysql/slow.log';
```

（`SET GLOBAL` 在重启后失效，要长期生效写在配置文件中，或者用 `SET PERSIST`。）按名字查找一个用户：

```sql
SELECT id, k FROM t WHERE name = 'user_0500000';
```

```
1 row in set (0.15 sec)
```

慢查询日志中多了一条记录：

```
# Time: 2026-10-25T03:03:12.486991Z
# User@Host: root[root] @ localhost []  Id:   102
# Query_time: 0.122914  Lock_time: 0.000001 Rows_sent: 1  Rows_examined: 1000000
SET timestamp=1792897392;
SELECT id, k FROM t WHERE name = "user_0500000";
```

最值得看的是最后两个数：**返回了 1 行，却检查了 100 万行**。`Rows_examined` 远大于 `Rows_sent`，通常说明缺少合适的索引。

### 1.2 performance_schema 的统计

慢查询日志只记录超过阈值的语句。一条 2 毫秒的查询每秒执行 1000 次，对数据库的压力远比一条每天执行一次的 10 秒的查询大，但它不会出现在慢查询日志中。

`performance_schema.events_statements_summary_by_digest` 把同一「形状」的语句（只是常量不同的语句，比如 `WHERE name = 'a'` 和 `WHERE name = 'b'`）归为一类，统计执行次数、总时间、扫描的行数等。模拟一段时间的负载：1000 次按名字查询（已经建了索引）、20 次按 `note` 查询（没有索引）、200 次翻页，然后按总时间排序：

```sql
SELECT LEFT(DIGEST_TEXT, 70) AS 语句, COUNT_STAR AS 次数,
       ROUND(SUM_TIMER_WAIT / 1e12, 2) AS 总秒数,
       ROUND(AVG_TIMER_WAIT / 1e9, 2) AS 平均毫秒,
       ROUND(SUM_ROWS_EXAMINED / COUNT_STAR) AS 平均扫描行数,
       SUM_NO_INDEX_USED AS 未用索引
FROM performance_schema.events_statements_summary_by_digest
WHERE SCHEMA_NAME = 'idx'
ORDER BY SUM_TIMER_WAIT DESC LIMIT 3;
```

```
+------------------------------------------------------------+------+--------+----------+--------------+----------+
| 语句                                                       | 次数 | 总秒数 | 平均毫秒 | 平均扫描行数 | 未用索引 |
+------------------------------------------------------------+------+--------+----------+--------------+----------+
| SELECT `id` , NAME FROM `t` ORDER BY `id` LIMIT ? OFFSET ? |  200 |   3.28 |    16.41 |       402010 |        0 |
| SELECT `id` FROM `t` WHERE `note` = ?                      |   20 |   1.92 |    95.78 |      1000000 |       20 |
| SELECT `id` , `k` FROM `t` WHERE NAME = ?                  | 1000 |   0.02 |     0.02 |            1 |        0 |
+------------------------------------------------------------+------+--------+----------+--------------+----------+
```

（`TIMER_WAIT` 的单位是皮秒，除以 10¹² 得到秒。）排在第一的是翻页的语句：它用了索引（`未用索引` 是 0），每次却平均扫描了 40 万行，第 6 节会解决它。第二是按 `note` 查询，每次扫描全表 100 万行。按名字查询执行了 1000 次，总共才 0.02 秒，不需要优化。

`sys` 库中还有一些基于它的现成视图，例如 `sys.statements_with_full_table_scans` 列出所有做了全表扫描的语句。

## 2. EXPLAIN

找到慢的语句后，用 `EXPLAIN` 看数据库**打算**怎样执行它。在语句前面加上 `EXPLAIN`：

```sql
EXPLAIN SELECT id, k FROM t WHERE name = 'user_0500000';
```

```
+----+-------------+-------+------+---------------+------+---------+------+--------+----------+-------------+
| id | select_type | table | type | possible_keys | key  | key_len | ref  | rows   | filtered | Extra       |
+----+-------------+-------+------+---------------+------+---------+------+--------+----------+-------------+
|  1 | SIMPLE      | t     | ALL  | NULL          | NULL | NULL    | NULL | 990820 |    10.00 | Using where |
+----+-------------+-------+------+---------------+------+---------+------+--------+----------+-------------+
```

（为了排版，本文省略了总是 `NULL` 的 `partitions` 列。）每个表一行，主要的列：

| 列 | 含义 |
|---|---|
| `type` | 访问方式，见下一节；`ALL` 是全表扫描 |
| `possible_keys` | 可以使用的索引 |
| `key` | 实际选择的索引，`NULL` 表示没有用索引 |
| `key_len` | 使用了索引中多少字节，可以看出联合索引用到了前几列 |
| `ref` | 和索引比较的是什么：常量 `const`，或者另一个表的列 |
| `rows` | **估计**要检查的行数（根据统计信息，不一定准确） |
| `filtered` | 估计这些行中有百分之几满足其余的条件 |
| `Extra` | 其他信息，见 2.2 节 |

这里 `type` 是 `ALL`，`key` 是 `NULL`，`rows` 约 99 万：要扫描整个表。

### 2.1 访问方式：type

从好到差，常见的有：

| type | 含义 | 例子 |
|---|---|---|
| `const` | 按主键或唯一索引等值查找，最多一行 | `WHERE id = 10` |
| `eq_ref` | 连接时，对前一个表的每一行，按主键或唯一索引在这个表中找一行 | `JOIN t ON t.id = o.t_id` |
| `ref` | 按非唯一索引等值查找，可能多行 | `WHERE k = 10` |
| `range` | 索引的范围扫描 | `WHERE k BETWEEN 10 AND 2000` |
| `index` | 扫描整个索引（按索引的顺序，但要读完全部） | `SELECT COUNT(*) FROM t` |
| `ALL` | 扫描整个表 | 没有可用的索引 |

在测试表上实际的输出（省略了前三列）：

```
| type  | possible_keys | key     | key_len | ref   | rows   | filtered | Extra                    |
| const | PRIMARY       | PRIMARY | 4       | const |      1 |   100.00 | NULL                     |  ← WHERE id = 10
| ref   | idx_k         | idx_k   | 4       | const |      1 |   100.00 | NULL                     |  ← WHERE k = 10
| range | PRIMARY       | PRIMARY | 4       | NULL  |     11 |   100.00 | Using where              |  ← WHERE id BETWEEN 10 AND 20
| range | idx_k         | idx_k   | 4       | NULL  |   1991 |   100.00 | Using index condition    |  ← SELECT * … WHERE k BETWEEN 10 AND 2000
| range | idx_k         | idx_k   | 4       | NULL  |   1991 |   100.00 | Using where; Using index  |  ← SELECT id … WHERE k BETWEEN 10 AND 2000
| index | NULL          | idx_k   | 4       | NULL  | 990820 |   100.00 | Using index              |  ← SELECT COUNT(*) FROM t
```

`idx_k` 是唯一的，但没有声明为 `UNIQUE`，所以是 `ref` 而不是 `const`。`COUNT(*)` 选择扫描 `idx_k` 而不是主键：要数的只是行数，二级索引比聚簇索引小得多（832 个叶子页对 9260 个）。

### 2.2 Extra

| Extra | 含义 |
|---|---|
| `Using index` | 覆盖索引，不需要回表（上一篇） |
| `Using where` | 从存储引擎取出行后，还要在服务器层用 `WHERE` 的条件过滤 |
| `Using index condition` | 索引条件下推：在索引中先用条件过滤，减少回表（3.4 篇） |
| `Using filesort` | 需要额外排序，不能直接按索引的顺序读出 |
| `Using temporary` | 需要建临时表，常见于 `GROUP BY`、`DISTINCT` |
| `Using MRR` | 回表前先把主键排序，按顺序读聚簇索引，减少随机读 |

`Using filesort` 和 `Using temporary` 出现在大表上时，通常值得关注（第 5 节）。

### 2.3 EXPLAIN ANALYZE

`EXPLAIN` 给出的是**估计**。`EXPLAIN ANALYZE`（MySQL 8.0.18 起）会**真正执行**这条语句，并给出每一步实际的行数和时间：

```sql
EXPLAIN ANALYZE SELECT id, k FROM t WHERE name = 'user_0500000'\G
```

```
EXPLAIN: -> Filter: (t.`name` = 'user_0500000')  (cost=102829 rows=99082) (actual time=105..182 rows=1 loops=1)
    -> Table scan on t  (cost=102829 rows=990820) (actual time=0.104..138 rows=1e+6 loops=1)
```

输出是一棵树，从最里层（缩进最多的）读起：

- `Table scan on t`：全表扫描，估计 99 万行，实际读了 100 万行（`rows=1e+6`），读完用了 138 毫秒；
- `Filter`：逐行检查 `name` 是否相等，估计有 99082 行满足（这是 10% 的默认估计，因为没有统计信息），实际只有 1 行，全部完成用了 182 毫秒。

`actual time=a..b` 中，a 是得到第一行的时间，b 是得到全部行的时间，单位毫秒。估计和实际相差很大时（这里 99082 对 1），说明优化器的统计信息不准，可能导致它选错执行方式。因为会真正执行，`EXPLAIN ANALYZE` 不要用在修改数据的语句上。

## 3. 加索引

```sql
CREATE INDEX idx_name ON t (name);
```

```
Query OK, 0 rows affected (0.70 sec)
```

再看执行计划：

```
| type | possible_keys | key      | key_len | ref   | rows | filtered | Extra |
| ref  | idx_name      | idx_name | 82      | const |    1 |   100.00 | NULL  |
```

```
EXPLAIN: -> Index lookup on t using idx_name (name='user_0500000')  (cost=0.693 rows=1) (actual time=0.003..0.00317 rows=1 loops=1)
```

从 182 毫秒降到 0.003 毫秒。`key_len` 是 82：`name` 是 `VARCHAR(20)`，utf8mb4 每个字符最多 4 字节，20 × 4 = 80，再加 2 字节的长度。`Extra` 不是 `Using index`，因为要返回的 `k` 不在 `idx_name` 中，需要回表（`idx_name` 中有 `name` 和 `id`）。

同样的查询，用 `IGNORE INDEX` 让优化器不使用这个索引，对比服务器端的执行时间：

```
毫秒：0.4 0.07 0.09         ← SELECT id, name FROM t WHERE name = 'user_0500000'
毫秒：109.22 90.33 96.77    ← SELECT id, name FROM t IGNORE INDEX (idx_name) WHERE name = 'user_0500000'
```

这个索引占用了 1636 个页（约 26 MB），这是它的代价。

## 4. 优化器什么时候不用索引

`k` 上有索引，查 `k > 990000` 和 `k > 100000`：

```
| type  | possible_keys | key   | rows   | filtered | Extra                            |
| range | idx_k         | idx_k |  10001 |   100.00 | Using index condition; Using MRR |  ← k > 990000
| ALL   | idx_k         | NULL  | 990820 |    50.00 | Using where                      |  ← k > 100000
```

第二条明明可以用 `idx_k`（`possible_keys` 中有），优化器却选择了全表扫描。它对吗？分别强制使用和禁止使用索引，计时：

```
毫秒：79.56 68.91 66.09      ← SELECT SUM(LENGTH(name)) FROM t WHERE k > 100000
毫秒：701.28 699.39 723.42   ← SELECT SUM(LENGTH(name)) FROM t FORCE INDEX (idx_k) WHERE k > 100000
毫秒：44.49 21.72 17.26      ← SELECT SUM(LENGTH(name)) FROM t WHERE k > 990000
毫秒：71.28 86.21 71.49      ← SELECT SUM(LENGTH(name)) FROM t IGNORE INDEX (idx_k) WHERE k > 990000
```

优化器两次都选对了：

- `k > 100000` 有约 90 万行。用索引就要回表 90 万次，每次都在聚簇索引中随机地走 3 层；全表扫描则是按顺序读完所有的叶子页。强制用索引慢了 10 倍；
- `k > 990000` 只有约 1 万行，用索引回表 1 万次，比扫描全表快。

优化器根据统计信息**估计**每种执行方式的代价，选最小的那个。要回表的行占全表的比例较大时（经验上超过百分之几十），全表扫描往往更快。所以「有索引却不用」不一定是坏事。确实选错时，先用 `ANALYZE TABLE` 更新统计信息；`FORCE INDEX` 只是最后的手段，数据的分布变了以后，强制的选择可能变成错误的选择。

## 5. 排序和分组

`ORDER BY` 的列有索引时，可以按索引的顺序直接读出，不需要排序；没有索引时，就要先把所有行取出来排序（`Using filesort`）：

```
| type  | key   | rows   | Extra          |
| index | idx_k |     10 | NULL           |  ← SELECT id, k, name FROM t ORDER BY k LIMIT 10
| ALL   | NULL  | 990820 | Using filesort |  ← SELECT id, k, name FROM t ORDER BY note, k LIMIT 10
```

```
毫秒：0.23 0.03 0.02         ← SELECT id, k, name FROM t ORDER BY k LIMIT 10
毫秒：138.14 138 137.86      ← SELECT id, k, name FROM t ORDER BY note, k LIMIT 10
```

只要 10 行：按 `k` 排序时，沿着 `idx_k` 读前 10 条再回表 10 次；按 `note` 排序时，必须看过全部 100 万行，才知道最小的 10 行是哪些。

`GROUP BY` 没有索引时，要建临时表逐行累加：

```
| type | key  | rows   | Extra           |
| ALL  | NULL | 990820 | Using temporary |  ← SELECT note, COUNT(*) FROM t GROUP BY note
```

```
毫秒：161.22 160.6 161.06    ← SELECT note, COUNT(*) FROM t GROUP BY note
```

如果有 `note` 上的索引，相同的值在索引中是挨在一起的，按索引的顺序读一遍就能分组，不需要临时表。

## 6. 深分页

第 1.2 节中排第一的翻页语句：

```
毫秒：0.06 0.02 0.02     ← SELECT id, name FROM t ORDER BY id LIMIT 10 OFFSET 10
毫秒：69.43 69.11 69.04  ← SELECT id, name FROM t ORDER BY id LIMIT 10 OFFSET 900000
```

都用了主键索引，都只返回 10 行，翻到第 90001 页时却慢了 3000 多倍。`OFFSET 900000` 的意思是「跳过前 90 万行」，但数据库并不知道第 90 万行在哪里，只能沿着叶子页一行行数过去，再把它们丢掉。偏移量越大越慢。

改进的办法是**记住上一页的最后一个值**，下一页从它之后开始取（这叫键集分页，keyset pagination）：

```sql
SELECT id, name FROM t WHERE id > 900000 ORDER BY id LIMIT 10;
```

```
毫秒：0.18 0.07 0.06     ← SELECT id, name FROM t WHERE id > 900000 ORDER BY id LIMIT 10
```

`id > 900000` 可以用索引直接定位到起点，只读 10 行，不管翻到第几页，速度都一样。代价是不能直接跳到任意一页，只能「下一页」，这正好是很多场景（无限滚动、按时间线浏览）的需要。排序的列不唯一时，要把主键也加进去，写成 `WHERE (k, id) > (上一页最后的 k, 上一页最后的 id)`，否则 `k` 相同的行会被漏掉或者重复。

## 7. 连接

订单表 `o` 有 20 万行，`t_id` 引用 `t.id`，但 `t_id` 上没有索引。查 100 个用户的订单金额：

```sql
SELECT t.name, SUM(o.amount)
FROM t JOIN o ON o.t_id = t.id
WHERE t.name BETWEEN 'user_0500000' AND 'user_0500099'
GROUP BY t.name;
```

```
| table | type   | possible_keys    | key     | ref        | rows   | filtered | Extra           |
| o     | ALL    | NULL             | NULL    | NULL       | 200030 |   100.00 | Using temporary |
| t     | eq_ref | PRIMARY,idx_name | PRIMARY | idx.o.t_id |      1 |     5.00 | Using where     |
```

```
毫秒：127.61 125.91 129.42
```

`EXPLAIN` 中的行按执行的顺序排列：先全表扫描 `o` 的 20 万行，每一行再按主键到 `t` 中找对应的用户（`eq_ref`），检查名字是否在范围内。只为了 100 个用户，却把 20 万个订单都看了一遍。

在连接的列上加索引：

```sql
CREATE INDEX idx_tid ON o (t_id);
```

```
| table | type  | possible_keys    | key      | ref      | rows | filtered | Extra                    |
| t     | range | PRIMARY,idx_name | idx_name | NULL     |  100 |   100.00 | Using where; Using index |
| o     | ref   | idx_tid          | idx_tid  | idx.t.id |    1 |   100.00 | NULL                     |
```

```
毫秒：0.13 0.07 0.07
```

连接的顺序反过来了：先用 `idx_name` 找出 100 个用户（覆盖索引，`idx_name` 中有 `id`），再对每个用户，用 `idx_tid` 找到他的订单。从 128 毫秒降到 0.07 毫秒。

连接时，**被驱动的表（后访问的表）的连接列上要有索引**。外键会自动建索引，这也是第 1 章、第 2 章中的连接一直很快的原因。

## 8. 优化的流程

1. **找**：用慢查询日志和 `performance_schema` 找出总耗时最多的语句，而不是凭感觉；
2. **看**：用 `EXPLAIN` 看执行计划，关注 `type` 是否是 `ALL` 或 `index`、`rows` 是否很大、`Extra` 中是否有 `Using filesort`、`Using temporary`；需要时用 `EXPLAIN ANALYZE` 看实际的行数和时间；
3. **改**：加索引，改写语句（覆盖索引、键集分页、避免在大表上排序），或者调整表结构；
4. **测**：在相同的数据上重新测量，确认真的变快了，并且没有拖慢其他的查询（索引会让写入变慢）。

## 9. 常见错误

- **只看 `EXPLAIN` 的 `key` 有没有值**：翻页的语句用了索引，仍然每次扫描几十万行；
- **以为有索引就一定要用**：回表的行很多时，全表扫描更快；
- **把 `EXPLAIN` 的 `rows` 当作准确值**：它只是估计；
- **在小数据上测试优化效果**：100 行的表上，全表扫描和索引没有区别，要在接近真实规模的数据上测；
- **用 `OFFSET` 翻很深的页**：越往后越慢；
- **连接列上没有索引**。

## 10. 小结

- 慢查询日志记录超过阈值的语句，`Rows_examined` 远大于 `Rows_sent` 是缺索引的信号；`performance_schema` 按语句的形状统计总耗时，能发现「单次不慢但次数很多」的语句
- `EXPLAIN` 的 `type` 从好到差：`const`、`eq_ref`、`ref`、`range`、`index`、`ALL`；`Extra` 中的 `Using index` 是好事，`Using filesort`、`Using temporary` 要关注
- `EXPLAIN ANALYZE` 实际执行并给出每一步的行数和时间
- 实测：加索引让按名字查询从 182 毫秒降到 0.003 毫秒；要回表 90 万行时，优化器放弃了索引，强制使用反而慢了 10 倍
- `ORDER BY`、`GROUP BY` 的列有索引，可以避免排序和临时表
- 深分页用键集分页：OFFSET 900000 要 69 毫秒，`WHERE id > 900000` 只要 0.07 毫秒
- 连接列上要有索引：128 毫秒降到 0.07 毫秒

## 11. 练习

1. `EXPLAIN SELECT * FROM t WHERE id = 10 OR k = 10` 会用索引吗？把 `k = 10` 换成 `note = 'x'`（`note` 上没有索引）呢？
2. 按 `k` 翻页：`SELECT id, k FROM t ORDER BY k LIMIT 10 OFFSET 900000` 怎样改写成键集分页？比较两者的执行时间。
3. 第 5 节的 `ORDER BY note, k LIMIT 10` 要排序 100 万行。建一个什么样的索引能消除 `Using filesort`？

<details>
<summary>点击查看答案</summary>

1. 两种情况的执行计划：

   ```
   | type        | possible_keys | key           | key_len | rows   | Extra                                   |
   | index_merge | PRIMARY,idx_k | PRIMARY,idx_k | 4,4     |      2 | Using union(PRIMARY,idx_k); Using where |  ← id = 10 OR k = 10
   | ALL         | PRIMARY       | NULL          | NULL    | 990820 | Using where                             |  ← id = 10 OR note = 'x'
   ```

   第一条用了**索引合并**（index merge）：分别用主键和 `idx_k` 找出满足各自条件的行，再求并集。第二条，`note = 'x'` 没有索引可用，要找出满足它的行只能扫描全表；既然无论如何都要扫描全表，`id = 10` 的索引也就没有意义了。`OR` 的每一个分支都有索引可用时，才可能用索引。
2. 记住上一页最后的 `k`，下一页从它之后取。`k` 在这个表中是唯一的，所以只用 `k` 就够了：

   ```sql
   SELECT id, k FROM t WHERE k > 900000 ORDER BY k LIMIT 10;
   ```

   ```
   毫秒：40.03 28.32 28.48   ← SELECT id, k FROM t ORDER BY k LIMIT 10 OFFSET 900000
   毫秒：0.66 0.32 0.32      ← SELECT id, k FROM t WHERE k > 900000 ORDER BY k LIMIT 10
   ```

   （这两条只查 `id` 和 `k`，`idx_k` 是覆盖索引，所以 `OFFSET` 的版本比第 6 节查 `name` 的快一些，但仍然要数过 90 万条索引记录。）如果 `k` 可能重复，要写成 `WHERE (k, id) > (上一页最后的 k, 上一页最后的 id) ORDER BY k, id`。
3. 建一个按 `(note, k)` 排序的联合索引：

   ```sql
   CREATE INDEX idx_note_k ON t (note, k);
   ```

   ```
   | type  | key        | key_len | rows | Extra |
   | index | idx_note_k | 404     |   10 | NULL  |
   ```

   ```
   毫秒：0.79 0.02 0.02   ← SELECT id, k, name FROM t ORDER BY note, k LIMIT 10
   ```

   `Using filesort` 消失了：索引中的记录本来就是先按 `note`、再按 `k` 排好的，沿着它读出前 10 条，回表 10 次就完成了，从 138 毫秒降到 0.02 毫秒。`key_len` 404 = `note` 的 400 字节（`CHAR(100)` × 4）加 `k` 的 4 字节。只建 `note` 上的单列索引不行：`note` 相同（这个表中都是 `x`）的行之间仍然要按 `k` 排序。联合索引的细节见下一篇。

</details>
