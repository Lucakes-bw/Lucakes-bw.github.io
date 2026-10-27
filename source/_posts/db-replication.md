---
title: 主从复制
date: 2026-10-25 11:40:00
updated: 2026-10-25 11:40:00
categories:
  - 数据库
  - 应用与运维
tags:
  - 数据库
  - MySQL
  - 复制
order: 3
description: 用两个容器搭建一主一从：从主库导出数据，在从库上导入，指定 binlog 的位置，启动复制。实测：小事务 2～4 毫秒就到了从库；一条更新 100 万行的语句，从库在主库提交 4 秒后才看到结果，刚写完就去从库读，读到的是旧值；主库的误删立即被复制到从库；在从库上偷偷插入一行，复制就因为主键冲突停了下来。搭建时还犯了一个错误：改了 SOURCE_HOST，复制的起点被重置到了最早的 binlog。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解主从复制的原理：binlog、接收线程、中继日志、应用线程；会搭建一主一从；会查看复制的状态和延迟；理解复制延迟带来的读写不一致；会处理复制中断；知道复制能解决和不能解决的问题。
>
> **前置知识**：第 4 章的 binlog 和两阶段提交；上一篇的 `mysqldump --source-data`。
>
> **实验环境**：两个 MySQL 8.4.13 容器：主库 172.17.0.4（`server_id = 1`），从库 172.17.0.5（`server_id = 2`）。它们在同一台虚拟机中（2 GB 内存），网络延迟可以忽略，所以本文测到的延迟主要来自从库执行事务所需的时间。

## 1. 为什么要复制

把一台数据库（**主库**，source）的修改，实时地复制到另一台或几台数据库（**从库**，replica）上：

- **高可用**：主库所在的机器坏了，可以把一个从库提升为新的主库，几分钟甚至几秒钟内恢复服务；
- **读写分离**：写都发给主库，读分散到多个从库上，读的能力可以随从库的数量扩展；
- **其他用途**：在从库上做备份、跑耗时的报表，不影响主库的业务。

## 2. 原理

<figure class="diagram">
<svg viewBox="0 0 660 200" role="img" aria-label="MySQL 复制：主库把事务写入 binlog，从库的接收线程通过网络读取 binlog 并写入中继日志，应用线程读取中继日志并执行，修改从库的数据">
  <defs>
    <marker id="rep-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/>
    </marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <rect x="10" y="20" width="200" height="160" rx="8" style="fill:none;stroke:var(--fg-2);stroke-dasharray:4,3"/>
    <text x="22" y="40" style="fill:var(--muted)">主库 172.17.0.4</text>
    <rect x="30" y="55" width="160" height="34" rx="5" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="42" y="77" style="fill:var(--accent)">事务提交</text>
    <rect x="30" y="125" width="160" height="34" rx="5" style="fill:var(--panel-2);stroke:var(--warn)"/>
    <text x="42" y="147" style="fill:var(--warn)">binlog</text>
    <line x1="110" y1="91" x2="110" y2="123" style="stroke:var(--fg-2)" marker-end="url(#rep-arrow)"/>
    <rect x="300" y="20" width="350" height="160" rx="8" style="fill:none;stroke:var(--fg-2);stroke-dasharray:4,3"/>
    <text x="312" y="40" style="fill:var(--muted)">从库 172.17.0.5</text>
    <rect x="320" y="125" width="140" height="34" rx="5" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="332" y="147" style="fill:var(--accent)">接收线程（IO）</text>
    <rect x="490" y="125" width="140" height="34" rx="5" style="fill:var(--panel-2);stroke:var(--warn)"/>
    <text x="502" y="147" style="fill:var(--warn)">中继日志</text>
    <rect x="490" y="55" width="140" height="34" rx="5" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="502" y="77" style="fill:var(--accent)">应用线程（SQL）</text>
    <rect x="320" y="55" width="140" height="34" rx="5" style="fill:var(--panel-2);stroke:var(--fg-2)"/>
    <text x="332" y="77" style="fill:var(--fg)">从库的数据</text>
    <line x1="192" y1="142" x2="318" y2="142" style="stroke:var(--fg-2)" marker-end="url(#rep-arrow)"/>
    <text x="215" y="134" style="fill:var(--muted)">TCP 3306</text>
    <line x1="462" y1="142" x2="488" y2="142" style="stroke:var(--fg-2)" marker-end="url(#rep-arrow)"/>
    <line x1="560" y1="123" x2="560" y2="91" style="stroke:var(--fg-2)" marker-end="url(#rep-arrow)"/>
    <line x1="488" y1="72" x2="462" y2="72" style="stroke:var(--fg-2)" marker-end="url(#rep-arrow)"/>
  </g>
</svg>
</figure>

1. 主库执行事务，提交时把它写进 binlog（第 4 章）；
2. 从库的**接收线程**（receiver，也叫 IO 线程）像一个普通的客户端一样连接主库，请求「从某个文件的某个位置开始的 binlog」，主库把 binlog 事件源源不断地发过来。接收线程把它们写进从库本地的**中继日志**（relay log）；
3. 从库的**应用线程**（applier，也叫 SQL 线程，可以有多个 worker）读取中继日志，执行其中的事务，修改从库的数据。

默认是**异步复制**：主库提交时不等从库，写完自己的 binlog 就告诉客户端成功了。所以从库总是比主库稍晚一点，这就是**复制延迟**。

## 3. 搭建

### 3.1 准备

- 每台服务器的 `server_id` 必须不同（主库是默认的 1，从库启动时指定 `--server-id=2`）。从库靠它识别哪些事件是自己产生的；
- 主库开启 binlog（MySQL 8.0 起默认开启）；
- 从库设置 `super_read_only = ON`，防止有人在从库上写入（第 5 节会看到在从库上写入的后果）。复制线程不受这个限制。

在主库上创建一个复制专用的账号，只给它复制的权限：

```sql
CREATE USER 'repl'@'%' IDENTIFIED BY 'repl-pass';
GRANT REPLICATION SLAVE ON *.* TO 'repl'@'%';
```

### 3.2 从一致的起点开始

从库必须从主库的某个**一致的快照**开始，并且知道这个快照对应 binlog 的哪个位置，这样才能接着复制之后的修改。上一篇的 `mysqldump` 正好提供这两样：

```bash
mysqldump --single-transaction --source-data=1 --databases shop app idx > seed.sql
```

`--source-data=1` 和上一篇的 `=2` 不同，它把 binlog 的位置写成一条**可执行**的语句，而不是注释：

```
CHANGE REPLICATION SOURCE TO SOURCE_LOG_FILE='binlog.000016', SOURCE_LOG_POS=144170248;
```

在从库上导入（导入时临时关闭 `super_read_only`），这条语句会把复制的起点设为这个位置：

```
real	0m10.132s
```

（这个文件 125 MB，主要是 3.1 篇的 100 万行。）

### 3.3 一个错误：位置被重置了

导入后，我执行了：

```sql
CHANGE REPLICATION SOURCE TO SOURCE_HOST = '172.17.0.4', SOURCE_USER = 'repl',
    SOURCE_PASSWORD = 'repl-pass', GET_SOURCE_PUBLIC_KEY = 1;
START REPLICA;
```

```
              Source_Log_File: binlog.000010
           Replica_IO_Running: Yes
          Replica_SQL_Running: Yes
        Seconds_Behind_Source: 191149
```

复制从 `binlog.000010` 开始了，而不是 `binlog.000016` 的 144170248，显示落后主库 19 万秒（两天多）。原因是：**修改 `SOURCE_HOST` 时，MySQL 认为换了一个主库，会把复制的位置重置**，于是从主库现存最早的 binlog 开始复制，要把这几天在主库上做过的所有实验都重放一遍。

立即停止复制，清空从库，重新导入，这次在同一条语句中同时指定主库和位置：

```sql
STOP REPLICA;
RESET REPLICA ALL;
-- 删除导入的库，重新导入 seed.sql
CHANGE REPLICATION SOURCE TO
    SOURCE_HOST = '172.17.0.4', SOURCE_USER = 'repl', SOURCE_PASSWORD = 'repl-pass',
    GET_SOURCE_PUBLIC_KEY = 1,
    SOURCE_LOG_FILE = 'binlog.000016', SOURCE_LOG_POS = 144170248;
START REPLICA;
SHOW REPLICA STATUS\G
```

```
              Source_Log_File: binlog.000016
          Read_Source_Log_Pos: 144170248
           Replica_IO_Running: Yes
          Replica_SQL_Running: Yes
          Exec_Source_Log_Pos: 144170248
        Seconds_Behind_Source: 0
                Last_IO_Error:
               Last_SQL_Error:
```

`SHOW REPLICA STATUS` 中最重要的几项：

- `Replica_IO_Running`、`Replica_SQL_Running`：接收线程和应用线程是否在运行，正常时都是 `Yes`；
- `Read_Source_Log_Pos`：接收线程读到了主库 binlog 的哪个位置；`Exec_Source_Log_Pos`：应用线程执行到了哪里。两者的差距就是收到了但还没执行的部分；
- `Seconds_Behind_Source`：估计的延迟（秒）；
- `Last_IO_Error`、`Last_SQL_Error`：出错的原因。

`GET_SOURCE_PUBLIC_KEY = 1` 让从库在没有 TLS 的连接上获取主库的公钥，用于 `caching_sha2_password` 认证时加密密码（第 1 章看到的认证方式）。生产环境中应该配置 TLS。

> 生产环境中更推荐使用 **GTID**（全局事务标识符）：每个事务有一个全局唯一的编号，从库记录已经执行过哪些事务，用 `SOURCE_AUTO_POSITION = 1` 自动定位，不需要手工指定文件和位置，也就不会犯上面的错误。本文为了看清 binlog 的位置，用的是传统的文件加位置的方式。

## 4. 延迟

### 4.1 小事务

在主库上插入一个客户，1 秒后在从库上查询：

```
+----+--------+--------+------------+
| id | name   | city   | created_at |
+----+--------+--------+------------+
|  6 | 孙七   | 杭州   | 2026-10-25 |
|  5 | 赵六   | NULL   | 2026-09-30 |
+----+--------+--------+------------+
```

已经到了。`performance_schema.replication_applier_status_by_worker` 记录了从库最近应用的事务在**主库上提交的时间**和**在从库上应用完成的时间**：

```
+----------------------------+----------------------------+----------+
| 主库提交                   | 从库应用完成               | 延迟毫秒 |
+----------------------------+----------------------------+----------+
| 2026-10-27 15:22:02.261858 | 2026-10-27 15:22:02.264280 |   2.4220 |
+----------------------------+----------------------------+----------+
```

再连续修改 5 次，延迟分别是 2.76、3.79、1.72、2.97、1.97 毫秒。小事务几毫秒就到了从库。

### 4.2 大事务

在主库上执行一条更新 100 万行的语句，提交后立即在主库和从库上读同一行，然后每隔一段时间在从库上读一次：

```
主库和从库上 id = 1 的 k：7919，7919
主库：UPDATE 100 万行用了 2756 毫秒，提交后立即读：主库 7920，从库 7919
提交后 101 毫秒：从库 k = 7919
提交后 750 毫秒：从库 k = 7919
提交后 1475 毫秒：从库 k = 7919
提交后 2170 毫秒：从库 k = 7919
提交后 2841 毫秒：从库 k = 7919
提交后 3471 毫秒：从库 k = 7919
提交后 4090 毫秒：从库 k = 7920
```

从库最近应用的事务：延迟 3931 毫秒，其中应用用了 3895 毫秒。

- 主库**提交之后**，这个事务才写进 binlog，从库才开始接收、执行它（第 4 章的两阶段提交）。主库执行它用了 2.8 秒，从库执行它也需要差不多的时间（3.9 秒）；
- 在这 4 秒中，从库读到的一直是旧值。

一个大事务会让复制延迟一次增加好几秒，而且这期间从库上的其他事务也要排在它后面。这是要避免大事务的又一个原因（第 4 章）：批量修改应该分成很多个小事务。

这个实验还有个插曲：第二次执行这条更新时，从库的容器被系统以内存不足（OOM）为由杀掉了。两个 MySQL 服务器挤在一台 2 GB 内存的虚拟机中，应用这个大事务又需要额外的内存。重启从库后，它从中继日志中上次的位置继续，几分钟后追上了主库（`Seconds_Behind_Source` 回到 0）。复制的位置是持久化的，从库崩溃后可以自动恢复。之后改用 20 万行的更新：主库用了 690 毫秒，从库延迟 799 毫秒。

### 4.3 读写不一致

延迟带来的直接问题是：用户刚写入的数据，马上去从库读，可能读不到。比如用户修改了昵称，页面刷新后显示的还是旧昵称（读请求被分到了从库上）。常见的处理方法：

- 写入之后的一小段时间内，这个用户的读请求发给主库；
- 对一致性要求高的读（例如支付后查询订单状态），总是读主库；
- 从库上等待追上某个位置之后再读（GTID 模式下用 `WAIT_FOR_EXECUTED_GTID_SET`）。

读写分离不是「把 `SELECT` 都发给从库」这么简单，要按业务区分哪些读可以容忍旧数据。

## 5. 复制的边界

### 5.1 从库只读

```
ERROR 1290 (HY000): The MySQL server is running with the --super-read-only option so it cannot execute this statement
```

从库上的写入被拒绝了。

### 5.2 误操作也会被复制

在主库上删除孙七（`DELETE FROM customer WHERE id = 6`），0.5 秒后在从库上查询，这一行也没有了：

```
从库中 id = 6 的行数：0
```

复制忠实地复制每一个提交的事务，包括错误的。所以**复制不是备份**，误删的恢复仍然要靠上一篇的全量备份加 binlog。（MySQL 支持**延迟复制**：`SOURCE_DELAY = 3600` 让从库故意晚一小时应用，误操作后有一小时的时间在从库上抢救数据。）

### 5.3 复制中断

如果从库的数据和主库不一致，复制可能会中断。模拟一下：临时关闭从库的只读，在从库上插入一个 `id = 7` 的客户；然后在主库上也插入一个 `id = 7` 的客户，再插入一个客户：

```
           Replica_IO_Running: Yes
          Replica_SQL_Running: No
        Seconds_Behind_Source: NULL
               Last_SQL_Errno: 1062
主库：7周八,8吴九
从库：7从库上的人
```

应用线程停止了（`Replica_SQL_Running: No`）。`performance_schema.replication_applier_status_by_worker` 中有详细的原因：

```
Worker 1 failed executing transaction 'ANONYMOUS' at source log binlog.000016, end_log_pos 254659636; Could not execute Write_rows event on table shop.customer; Duplicate entry '7' for key 'customer.PRIMARY', Error_code: 1062; …
```

从主库来的 `id = 7` 和从库上已有的行冲突了。应用线程停下来，之后的事务（吴九）也都不会执行，延迟会一直增加。接收线程还在运行，主库的 binlog 照常被接收到中继日志中。

修复的方法是让从库的数据恢复正确，然后继续复制。这里从库上的那一行本来就不该存在，删掉它：

```sql
SET GLOBAL super_read_only = OFF;
DELETE FROM customer WHERE id = 7;
SET GLOBAL super_read_only = ON;
START REPLICA;
```

```
           Replica_IO_Running: Yes
          Replica_SQL_Running: Yes
        Seconds_Behind_Source: 0
主库：7周八,8吴九
从库：7周八,8吴九
校验：主库 shop.customer 3188187408 shop.order_item 1850005927
校验：从库 shop.customer 3188187408 shop.order_item 1850005927
```

应用线程从停下的事务继续执行，追上了主库，两个表的校验和一致。

真实的情况往往更麻烦：不一致可能早就存在，只是到某个事务碰到它时才暴露；有时只能跳过出错的事务，或者用工具（例如 Percona Toolkit 的 `pt-table-checksum`、`pt-table-sync`）比较和修复主从的数据，严重时只能重新搭建从库。所以要从源头上避免：**从库一定要设为只读**。

## 6. 常见错误

- **两台服务器的 `server_id` 相同**：复制会出现各种奇怪的问题；
- **修改 `SOURCE_HOST` 后没有重新指定位置**：位置被重置，从最早的 binlog 开始复制（本文 3.3 节）；
- **从库没有设为只读**：有人或者程序误连到从库写入，数据就不一致了，迟早导致复制中断；
- **在主库上执行大事务**：从库延迟一次增加好几秒；
- **把复制当作备份**：误删会立即复制到所有从库；
- **读写分离后不考虑延迟**：刚写的数据读不到；
- **不监控复制状态**：应用线程停了几天才发现，从库的数据早就过时了。至少要监控 `Replica_SQL_Running`、`Replica_IO_Running` 和延迟。

## 7. 小结

- 复制：主库写 binlog，从库的接收线程把 binlog 拉到中继日志，应用线程执行中继日志；默认异步
- 搭建：不同的 `server_id`，复制账号，用 `mysqldump --source-data=1` 得到一致的数据和起点，`CHANGE REPLICATION SOURCE TO` 中同时指定主库和位置；生产环境推荐 GTID
- 实测延迟：小事务 2～4 毫秒；更新 100 万行的事务，从库在主库提交后约 4 秒才看到，期间读到的都是旧值
- 读写分离要处理延迟带来的读写不一致
- 误删也会被复制，复制不是备份；从库要设为只读，在从库上写入导致了主键冲突，复制中断
- `SHOW REPLICA STATUS` 和 `performance_schema` 中的复制表用来查看状态、延迟和错误

## 8. 练习

1. 主库上执行了 `DELETE FROM order_item`（忘了写 `WHERE`）。如果有一个设置了 `SOURCE_DELAY = 3600` 的延迟从库，怎样利用它恢复数据？
2. 第 4.2 节的大事务，如果拆成每次更新 1 万行、共 100 个事务，从库的延迟会有什么变化？
3. 读写分离的系统中，用户下单后立即跳转到「我的订单」页面，结果看不到刚下的订单。分析原因，给出两种解决办法。

<details>
<summary>点击查看答案</summary>

1. 延迟从库在 1 小时后才会执行这条 `DELETE`。发现误删后，立即在延迟从库上 `STOP REPLICA`，此时它的 `order_item` 还是完整的。然后有两种做法：从延迟从库上导出 `order_item`，导入主库（误删之后主库上新增的明细要和它合并）；或者让延迟从库执行到误删之前为止（`START REPLICA UNTIL SOURCE_LOG_FILE = …, SOURCE_LOG_POS = …`，位置用上一篇的方法在 binlog 中找到），再从它导出。延迟从库相当于一个「一小时前的实时备份」，恢复比全量备份加 binlog 快得多。
2. 每个小事务在主库上提交后，从库很快就能开始执行它，而不必等整个 100 万行都完成。从库的延迟会保持在执行一个小事务所需的时间附近（第 4.2 节中 20 万行的事务延迟约 0.8 秒，1 万行大约是它的二十分之一），而不是一次跳到 4 秒；其他的事务也不必在一个大事务后面等待几秒。代价是整个修改不再是原子的，中途失败时，要能从失败的地方继续。
3. 下单是写操作，发给了主库；「我的订单」是读操作，发给了从库。跳转后的查询到达从库时，下单的事务可能还没复制过去（延迟），所以看不到。解决办法：（1）下单后的一段时间内（例如几秒），这个用户的读请求都发给主库；（2）「我的订单」这类用户自己刚写入的数据，始终从主库读；（3）记下下单事务的 GTID，在从库上用 `WAIT_FOR_EXECUTED_GTID_SET` 等它复制过来再查询。

</details>
