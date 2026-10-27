---
title: 备份与恢复
date: 2026-10-25 11:30:00
updated: 2026-10-25 11:30:00
categories:
  - 数据库
  - 应用与运维
tags:
  - 数据库
  - MySQL
  - 备份
order: 2
description: 一条忘了写 WHERE 的 DELETE 删掉了全部订单明细，之后又有新的订单进来。本文完整地演练一次恢复：用 mysqldump 做的全量备份恢复到备份的时刻，再用 mysqlbinlog 从 binlog 中重放备份之后的操作，精确地跳过那条 DELETE，最后订单一条不少。过程中踩了一个坑：在同一台服务器上重放 binlog，重放的操作又被写进了 binlog。另外实测 100 万行的导出只要 0.23 秒、导入却要 3.6 秒，以及不加 --single-transaction 时，备份中两个账户的余额之和凭空多出了 2 元。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解逻辑备份和物理备份；会用 `mysqldump` 做一致的全量备份；会用 binlog 做按时间点恢复，能在 binlog 中找到误操作的位置并跳过它；理解备份的一致性；知道备份策略的要点。
>
> **前置知识**：第 4 章的事务、MVCC 和 binlog（《日志与崩溃恢复》）；第 1 章的示例数据 `shop.sql`。
>
> **实验环境**：服务器是 MySQL 8.4.13 容器（172.17.0.4），`mysqldump` 在服务器容器中运行；`mysqlbinlog` 在另一个 Ubuntu 容器中运行（`apt install mysql-server-core-8.0`，版本 8.0.46），通过网络读取服务器的 binlog。

## 1. 为什么需要备份

第 4 章的 redo 日志能在崩溃后恢复已提交的数据，但它防不住这些：

- **误操作**：`DELETE` 忘了写 `WHERE`，`DROP TABLE` 删错了表。这些操作本身就是「成功提交的事务」，redo 日志会忠实地保证它们生效；
- **磁盘损坏、机器丢失**：数据文件和日志文件一起没了；
- **软件缺陷、被攻击**：数据被错误地修改了，而且可能过了几天才发现。

这些情况只能靠**备份**：在另一个地方保存一份过去某个时刻的数据。

## 2. 两种备份

| | 逻辑备份 | 物理备份 |
|---|---|---|
| 内容 | SQL 语句（`CREATE TABLE`、`INSERT`） | 数据文件本身（`.ibd`、redo 日志等） |
| 工具 | `mysqldump`、`mysqlpump`、MySQL Shell 的 `util.dumpInstance` | Percona XtraBackup、MySQL Enterprise Backup、文件系统快照 |
| 恢复 | 重新执行 SQL，要重建索引，慢 | 复制回文件，快 |
| 优点 | 可读、可以只恢复一个表、跨版本 | 速度快，适合大数据量 |

几十 GB 以内的数据库，`mysqldump` 通常就够用。本文用它。

**不能在数据库运行时直接复制数据目录**（第 1 章说过）：缓冲池中的脏页还没写回，复制过程中文件又在变化，复制出的文件很可能无法使用。物理备份工具会配合 redo 日志处理这个问题。

## 3. 用 mysqldump 做全量备份

```bash
mysqldump --single-transaction --source-data=2 --databases shop > shop-full.sql
```

- `--single-transaction`：在一个可重复读的事务中导出所有的表，所有的表来自**同一个快照**（第 4 章的 MVCC），而且导出时不加锁，不影响业务（练习 2）；
- `--source-data=2`：在备份文件中以注释的形式记下**备份时 binlog 的位置**。按时间点恢复时，就从这个位置开始重放；
- `--databases shop`：备份 `shop` 库，文件中包含 `CREATE DATABASE` 语句。

备份文件的开头：

```
-- MySQL dump 10.13  Distrib 8.4.13, for Linux (aarch64)
--
-- Host: localhost    Database: shop
-- ------------------------------------------------------
-- Server version	8.4.13
...
-- CHANGE REPLICATION SOURCE TO SOURCE_LOG_FILE='binlog.000016', SOURCE_LOG_POS=158;
```

备份对应的是 `binlog.000016` 的第 158 字节。之后就是每个表的 `DROP TABLE IF EXISTS`、`CREATE TABLE` 和 `INSERT` 语句。

### 3.1 规模

在第 3 章 100 万行的测试表上（表加上索引约 161 MB）：

```
导出：229 毫秒，33 MB
gzip：493 毫秒，8 MB
导入：3554 毫秒
```

导出的文件只有 33 MB，比表小得多：`CHAR(100)` 补齐的空格不在 SQL 中，索引也不在其中（只有定义），页中预留的空间更不在。导入却比导出慢了 15 倍：每一行都要重新插入，三个索引都要重新构建。导入后用 `CHECKSUM TABLE` 比较原表和恢复的表，校验和相同（3611632430），数据完全一致。

数据量更大时，导入的时间可能是几个小时。**备份的价值取决于能多快恢复**，所以要定期做恢复演练，知道恢复需要多长时间。

## 4. 一次事故

全量备份完成后，业务照常进行：

```sql
-- 14:00 一个新订单
INSERT INTO orders (customer_id, created_at) VALUES (5, '2026-10-25 14:00:00');
INSERT INTO order_item VALUES (7, 4, 1, 899.00);
```

然后，有人执行了一条忘了写 `WHERE` 的语句：

```sql
DELETE FROM order_item;
```

发现之前，又来了一个订单：

```sql
INSERT INTO orders (customer_id, created_at) VALUES (3, '2026-10-25 14:30:00');
INSERT INTO order_item VALUES (8, 9, 1, 399.00);
```

```
误删之后：orders 8 行，order_item 1 行
```

11 行订单明细只剩下最后插入的 1 行。目标是恢复成「除了那条 `DELETE`，其他操作都生效」的状态：8 个订单、12 行明细。

只恢复全量备份是不够的：它会回到备份的时刻，备份之后的两个订单也丢了。要用 **全量备份 + binlog**：先恢复备份，再重放备份之后的 binlog，跳过 `DELETE`。这叫**按时间点恢复**（point-in-time recovery）。

## 5. 恢复

### 5.1 在 binlog 中找到误操作

`mysqlbinlog` 把 binlog 转换成可读的文本。`-R` 表示从服务器上读取（不用复制 binlog 文件），`--base64-output=DECODE-ROWS -v` 把 ROW 格式的行修改显示成伪 SQL：

```bash
mysqlbinlog -R -h 172.17.0.4 -uroot -p --base64-output=DECODE-ROWS -v binlog.000016
```

找到 `Delete_rows` 事件所在的事务（节选）：

```
# at 756
#261027  7:14:37 server id 1  end_log_pos 835  	Anonymous_GTID	last_committed=2	sequence_number=3 …
# at 835
#261027  7:14:37 server id 1  end_log_pos 910  	Query	thread_id=…
BEGIN
# at 910
#261027  7:14:37 server id 1  end_log_pos 971  	Table_map: `shop`.`order_item` mapped to number 176
# at 971
#261027  7:14:37 server id 1  end_log_pos 1204 	Delete_rows: table id 176 flags: STMT_END_F
### DELETE FROM `shop`.`order_item`
### WHERE
###   @1=1
###   @2=1
###   @3=1
###   @4=299.00
### DELETE FROM `shop`.`order_item`
...
# at 1204
#261027  7:14:37 server id 1  end_log_pos 1235 	Xid = …
COMMIT/*!*/;
# at 1235
```

- `# at 756` 是事件在 binlog 中的起始位置。这个事务从 756 开始（`Anonymous_GTID`、`BEGIN`），到 1235 之前结束（`COMMIT`）；
- `### DELETE … WHERE @1=1 @2=1 @3=1 @4=299.00` 是被删除的第一行，`@1` 到 `@4` 是表的四列（订单 1、商品 1、数量 1、单价 299）。ROW 格式记录了每一行删除前的完整内容，共 11 行；
- `7:14:37` 是事件的时间（UTC）。

于是需要重放的是两段：从 158（备份的位置）到 756，以及从 1235 到当前 binlog 的末尾。当前的末尾是 1833（`SHOW BINARY LOG STATUS` 显示的位置）。

### 5.2 恢复全量备份

```bash
mysql < shop-full.sql
```

```
恢复全量备份：53 毫秒，orders 6 行，order_item 10 行
```

回到了备份的时刻：6 个订单，10 行明细。

### 5.3 重放 binlog

把两段 binlog 分别导出成 SQL 文件，再执行：

```bash
mysqlbinlog -R -h 172.17.0.4 -uroot -p --start-position=158 --stop-position=756 binlog.000016 > part1.sql
mysqlbinlog -R -h 172.17.0.4 -uroot -p --start-position=1235 --stop-position=1833 binlog.000016 > part2.sql
mysql -h 172.17.0.4 -uroot -p < part1.sql
mysql -h 172.17.0.4 -uroot -p < part2.sql
```

```sql
SELECT COUNT(*) FROM orders;
SELECT * FROM order_item WHERE order_id >= 6 ORDER BY order_id, product_id;
SELECT COUNT(*) FROM order_item;
```

```
+----------+
| COUNT(*) |
+----------+
|        8 |
+----------+
+----------+------------+----------+--------+
| order_id | product_id | quantity | price  |
+----------+------------+----------+--------+
|        6 |          1 |        2 | 279.00 |
|        7 |          4 |        1 | 899.00 |
|        8 |          9 |        1 | 399.00 |
+----------+------------+----------+--------+
+----------+
| COUNT(*) |
+----------+
|       12 |
+----------+
```

8 个订单、12 行明细，误删之前的 10 行回来了，误删前后的两个新订单也都在。

### 5.4 踩过的坑

第一次演练时，第二段写成了「从 1235 读到末尾」，没有指定 `--stop-position`，结果订单 8 没有恢复出来。重新导出这一段检查，文件有 240 MB，包含 72 万多个事件，而真正需要的只有 1235 到 1833 之间的两个事务：

- 这台实验服务器的 binlog 在误删之后还记录了很多与 `shop` 无关的操作（3.1 节导入的 100 万行就在其中）；
- 我是在**同一台服务器**上恢复的，恢复全量备份、重放第一段 binlog，这些操作本身也被写进了 binlog，追加在末尾。「读到末尾」就会把它们再执行一遍。

教训：

- **记下误操作被发现时 binlog 的末尾位置**，重放时用 `--stop-position` 明确地截止。也可以按时间截止（`--stop-datetime`），但同一秒内可能有多个事务，位置更精确；只想恢复一个库时，再加上 `--database=shop`；
- **先导出成文件，检查内容和大小，再执行**，不要把错误输出丢掉；
- 更稳妥的做法是在**另一台服务器**（或另一个容器）上恢复，确认无误后，再把需要的数据导回生产库。这样既不会把恢复的操作混进生产库的 binlog，也不会在恢复过程中影响线上的业务。

## 6. 备份策略

- **定期全量备份 + 持续保存 binlog**：例如每天凌晨一次全量备份，binlog 至少保存到下一次全量备份之后（`binlog_expire_logs_seconds`，默认 30 天）。这样可以恢复到这段时间内的任何时刻；
- **备份要放在另一台机器上**，最好在另一个机房或云存储中。和数据库在同一块磁盘上的备份，防不了磁盘损坏；
- **验证备份**：定期把备份恢复到测试环境，检查数据。一个从来没有恢复过的备份，不能指望它在需要时可用；
- **记录恢复需要的时间**：3.1 节中导入比导出慢 15 倍，要知道最坏情况下多久能恢复；
- **保护备份文件**：备份中有全部的数据，包括密码哈希等敏感信息，要像数据库本身一样控制访问权限，最好加密；
- **主从复制不是备份**：误删的 `DELETE` 会立即复制到从库（下一篇）。

## 7. 常见错误

- **只有全量备份，没有 binlog**：只能回到备份的时刻，之后的数据全部丢失；
- **备份时不加 `--single-transaction`**：各个表不是同一时刻的数据（练习 2）；
- **备份和数据库放在一起**；
- **从来没有做过恢复演练**；
- **在生产库上直接重放 binlog，没有截止位置**；
- **执行危险的语句前不先 `SELECT`**：第 1 章的建议，比任何恢复手段都便宜。

## 8. 小结

- redo 日志防崩溃，备份防误操作和硬件故障
- 逻辑备份（`mysqldump`）可读、灵活、恢复慢；物理备份（XtraBackup）恢复快
- `--single-transaction` 让所有表来自同一个快照且不加锁，`--source-data=2` 记下 binlog 的位置
- 实测 100 万行：导出 0.23 秒、33 MB，导入 3.6 秒
- 按时间点恢复：恢复全量备份，用 `mysqlbinlog` 重放备份之后的 binlog，用 `--start-position`、`--stop-position` 跳过误操作。实测误删的 11 行和前后的新订单全部恢复
- 在同一台服务器上重放时，重放的操作也会写入 binlog，要明确地截止

## 9. 练习

1. 如果误操作是 `DROP TABLE order_item`，恢复过程有什么不同？
2. 一个表 `wallet_a` 中的钱不断地转到另一个表 `wallet_b`（每次转账是一个事务，两个表的总额始终是 10000）。备份时加与不加 `--single-transaction`，备份中两个表的总额分别是多少？
3. 只用 binlog（没有全量备份）能恢复数据吗？

<details>
<summary>点击查看答案</summary>

1. 过程相同：恢复全量备份，重放 binlog，跳过 `DROP TABLE` 那个事件。不同之处在于，`DROP TABLE` 是 DDL，在 binlog 中是一个 `Query` 事件（内容就是 `DROP TABLE …` 语句本身），而不是 `Delete_rows`，可以直接搜索语句文本找到它的位置。另外，如果只想恢复这一个表而不影响库中的其他表，可以从全量备份中只取出这个表的部分（逻辑备份的优势），在另一个库中恢复，再把数据导回来。
2. 让一个客户端不停地转账，同时用两种方式备份，从备份文件中取出两个表的余额（两个表之间还有一个 30 万行的表，让导出两个表的时间隔开一些）：

   ```
   --single-transaction：a = 7624，b = 2376，a + b = 10000
   --skip-lock-tables：a = 7561，b = 2440，a + b = 10001
   --single-transaction：a = 7778，b = 2222，a + b = 10000
   --skip-lock-tables：a = 7714，b = 2288，a + b = 10002
   ```

   加了 `--single-transaction`，两个表来自同一个快照，总额总是 10000。不加（并且用 `--skip-lock-tables` 不锁表），`wallet_a` 和 `wallet_b` 是在不同的时刻导出的：导出 `wallet_a` 之后、导出 `wallet_b` 之前发生的转账，在 `b` 中有、在 `a` 中却没有扣，凭空多出了 1 元、2 元。`mysqldump` 默认会在导出每个库时锁住所有表，这样也能一致，但锁表期间业务无法写入。
3. 理论上可以：如果从数据库创建的那一刻起所有的 binlog 都还在，从头重放就能得到全部数据。实际上做不到：binlog 会按 `binlog_expire_logs_seconds` 定期删除，从头重放的时间也长得无法接受。binlog 必须和全量备份配合使用，全量备份是起点，binlog 是从起点到某个时刻的增量。

</details>
