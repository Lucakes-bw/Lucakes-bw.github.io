---
title: 隔离级别与 MVCC
date: 2026-10-25 10:50:00
updated: 2026-10-25 10:50:00
categories:
  - 数据库
  - 事务与锁
tags:
  - 数据库
  - MySQL
  - 事务
  - MVCC
order: 2
description: 两个事务同时读写同一行，彼此能看到什么？本文用两个会话逐一重现：读未提交下读到了后来被回滚的数据；读已提交下同一个事务两次读到不同的值；可重复读下读到的始终是第一次的值，但 UPDATE 却修改了「看不见」的新行。再看 MVCC 怎样实现这一切：每行的多个版本连成一条链，配有沿版本链判断可见性的演示。最后实测两个坑：先读后写的丢失更新，以及一个不结束的事务让 undo 历史涨到 1 万、让读一行慢了 260 倍。
top_group_list: true
disableNunjucks: true
viz: [db-mvcc]
---

> **本文目标**：理解脏读、不可重复读、幻读；理解四种隔离级别，能用实验区分它们；理解 MVCC 的版本链和读视图；区分快照读和当前读；会用原子的 `UPDATE` 和 `SELECT … FOR UPDATE` 避免丢失更新；知道长事务的危害。
>
> **前置知识**：上一篇《事务与 ACID》，以及它的两个会话的实验方法；第 3 章《InnoDB 的存储结构》中每行的隐藏列 `DB_TRX_ID` 和 `DB_ROLL_PTR`。
>
> **实验环境**：MySQL 8.4.13（Docker），默认隔离级别为可重复读。每个实验开始前，`account` 表都恢复为张三 100 元、李四 50 元。为了简洁，输出中省略了 `Query OK, 0 rows affected` 等无关的行。

## 1. 并发事务的三种问题

上一篇看到，会话 B 看不到会话 A 未提交的修改。但「什么时候能看到」有不同的规定，对应着不同的问题。

| 问题 | 含义 |
|---|---|
| **脏读** | 读到了别的事务**未提交**的数据，而它之后可能被回滚 |
| **不可重复读** | 同一个事务中，两次读同一行，结果不同（中间别人修改并提交了） |
| **幻读** | 同一个事务中，两次按同样的条件查询，第二次多出了（或少了）行（中间别人插入并提交了） |

SQL 标准定义了四种**隔离级别**，按允许的问题划分：

| 隔离级别 | 脏读 | 不可重复读 | 幻读 |
|---|---|---|---|
| 读未提交 READ UNCOMMITTED | 可能 | 可能 | 可能 |
| 读已提交 READ COMMITTED | 不会 | 可能 | 可能 |
| 可重复读 REPEATABLE READ | 不会 | 不会 | 可能（InnoDB 的普通读不会，见 3.4 节） |
| 串行化 SERIALIZABLE | 不会 | 不会 | 不会 |

隔离越严格，并发事务之间的影响越小，但并发性能也越低。MySQL 的默认级别是**可重复读**，Oracle、PostgreSQL 的默认级别是读已提交。用 `SET SESSION TRANSACTION ISOLATION LEVEL …` 设置当前会话的级别。

## 2. 逐一重现

### 2.1 读未提交：脏读

```
  0.0s B> SET SESSION TRANSACTION ISOLATION LEVEL READ UNCOMMITTED;
  0.6s A> START TRANSACTION;
  1.2s A> UPDATE account SET balance = balance - 30 WHERE id = 1;
  1.8s B> SELECT balance FROM account WHERE id = 1;
        B| +---------+
        B| | balance |
        B| +---------+
        B| |   70.00 |
        B| +---------+
  2.4s A> ROLLBACK;
  3.0s B> SELECT balance FROM account WHERE id = 1;
        B| |  100.00 |
```

B 读到了 A 还没提交的 70。随后 A 回滚了，70 从来没有真正存在过。如果 B 根据 70 做了决定（比如判断余额不足，拒绝了一笔付款），这个决定就是错的。读未提交几乎没有使用的理由。

### 2.2 读已提交：不可重复读

```
  0.0s B> SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED;
  0.6s B> START TRANSACTION;
  1.2s B> SELECT balance FROM account WHERE id = 1;
        B| |  100.00 |
  1.8s A> UPDATE account SET balance = balance - 30 WHERE id = 1;
  2.4s B> SELECT balance FROM account WHERE id = 1;
        B| |   70.00 |
  3.0s B> COMMIT;
```

（A 没有开始事务，它的 `UPDATE` 自动提交了。）B 只读到已提交的数据，没有脏读；但同一个事务中两次读同一行，结果不同。如果 B 在生成一份报表，前半部分用的是 100，后半部分用的是 70，报表就自相矛盾了。

### 2.3 可重复读

```
  0.0s B> START TRANSACTION;
  0.6s B> SELECT balance FROM account WHERE id = 1;
        B| |  100.00 |
  1.2s A> UPDATE account SET balance = balance - 30 WHERE id = 1;
  1.8s B> SELECT balance FROM account WHERE id = 1;
        B| |  100.00 |
  2.4s B> COMMIT;
  3.0s B> SELECT balance FROM account WHERE id = 1;
        B| |   70.00 |
```

A 已经提交了，B 在事务中读到的仍然是 100。B 看到的是事务开始时数据库的一个**快照**，事务结束后，才看到最新的 70。

### 2.4 可重复读下的幻读

A 插入一个新账户，B 的事务前后各查一次全表，然后修改所有账户：

```
  0.0s B> START TRANSACTION;
  0.6s B> SELECT * FROM account;
        B| +----+------+---------+
        B| | id | name | balance |
        B| +----+------+---------+
        B| |  1 | 张三 |  100.00 |
        B| |  2 | 李四 |   50.00 |
        B| +----+------+---------+
  1.2s A> INSERT INTO account VALUES (3, '王五', 10.00);
  1.8s B> SELECT * FROM account;
        B| |  1 | 张三 |  100.00 |
        B| |  2 | 李四 |   50.00 |
  2.4s B> UPDATE account SET balance = balance + 1;
        B| Query OK, 3 rows affected (0.00 sec)
  3.0s B> SELECT * FROM account;
        B| +----+------+---------+
        B| | id | name | balance |
        B| +----+------+---------+
        B| |  1 | 张三 |  101.00 |
        B| |  2 | 李四 |   51.00 |
        B| |  3 | 王五 |   11.00 |
        B| +----+------+---------+
  3.6s B> COMMIT;
```

第二次 `SELECT` 没有看到王五，快照中没有他，普通的查询不会出现幻读。但 `UPDATE` 显示 `3 rows affected`：它修改了快照中「不存在」的王五，之后 B 自己的 `SELECT` 也看到了王五。

原因是 InnoDB 有两种读：

- **快照读**：普通的 `SELECT`，读事务的快照，不加锁；
- **当前读**：`UPDATE`、`DELETE`、`INSERT`，以及 `SELECT … FOR UPDATE`、`SELECT … FOR SHARE`，读的是这一行**最新的已提交版本**，并且加锁。修改数据当然必须基于最新的值，不能基于快照。

B 的 `UPDATE` 是当前读，看到了 A 已提交的王五，把他也改了；改过之后，这一行的最新版本是 B 自己写的，B 的快照读也就能看到它了。所以 InnoDB 的可重复读并没有完全消除幻读，它保证的是：只用快照读的事务，不会看到幻影。

### 2.5 快照什么时候建立

```
  0.0s B> START TRANSACTION;
  0.6s A> UPDATE account SET balance = balance - 30 WHERE id = 1;
  1.2s B> SELECT balance FROM account WHERE id = 1;
        B| |   70.00 |
  1.8s B> COMMIT;
  2.4s B> START TRANSACTION WITH CONSISTENT SNAPSHOT;
  3.0s A> UPDATE account SET balance = balance - 30 WHERE id = 1;
  3.6s B> SELECT balance FROM account WHERE id = 1;
        B| |   70.00 |
  4.3s B> COMMIT;
```

第一个事务中，B 先 `START TRANSACTION`，A 再修改，B 却读到了 A 修改后的 70：可重复读的快照不是在 `START TRANSACTION` 时建立的，而是在事务中**第一次读**的时候。`START TRANSACTION WITH CONSISTENT SNAPSHOT` 则立即建立快照，所以第二个事务中，A 把 70 改成了 40，B 读到的仍然是 70。

### 2.6 串行化

```
  0.0s B> SET SESSION TRANSACTION ISOLATION LEVEL SERIALIZABLE;
  0.6s B> START TRANSACTION;
  1.2s B> SELECT balance FROM account WHERE id = 1;
        B| |  100.00 |
  1.8s A> UPDATE account SET balance = balance - 30 WHERE id = 1;
  3.9s B> COMMIT;
        A| Query OK, 1 row affected (2.13 sec)
```

串行化下，普通的 `SELECT` 也会对读到的行加共享锁。A 的 `UPDATE` 要等 B 的事务结束才能执行，等了 2.13 秒。并发的事务实际上被排成了队，结果和依次执行一样，但读写互相阻塞，性能最差。

## 3. MVCC

可重复读下，A 已经把余额改成了 70 并提交，B 却还能读到 100。100 存在哪里？

### 3.1 版本链

第 3 章解析 `.ibd` 文件时看到，每一行都有两个隐藏列：

- `DB_TRX_ID`：最后修改这一行的事务的编号；
- `DB_ROLL_PTR`：回滚指针，指向 undo 日志中这一行的**上一个版本**。

修改一行时，InnoDB 先把旧的内容写进 undo 日志，再修改聚簇索引中的行，让回滚指针指向这个旧版本。多次修改后，一行就有了一串版本：聚簇索引中是最新的，undo 日志中是一个个更旧的版本，用回滚指针连成一条**版本链**。回滚事务时，就是沿着它把旧值写回去；而 B 要读 100，也是沿着它找到旧版本。这就是**多版本并发控制**（MVCC，Multi-Version Concurrency Control）。

可以看到真实的事务编号。A 修改了一行、还没提交时，查询 `information_schema.innodb_trx`：

```
+-----------------+-----------+---------------------+-------------------+
| trx_id          | trx_state | trx_isolation_level | trx_rows_modified |
+-----------------+-----------+---------------------+-------------------+
| 542227479002328 | RUNNING   | REPEATABLE READ     |                 0 |
| 68077           | RUNNING   | REPEATABLE READ     |                 1 |
+-----------------+-----------+---------------------+-------------------+
```

A 的事务编号是 68077，张三那一行现在的 `DB_TRX_ID` 就是它。B 的事务只读不写，InnoDB 没有给它分配真正的编号，显示的是一个很大的临时值。

### 3.2 读视图

B 怎么知道该读哪个版本？建立快照时，InnoDB 为 B 创建一个**读视图**（read view），记录：

- 当时**正在进行**（还没提交）的事务编号列表；
- 其中最小的编号；
- 下一个将要分配的事务编号。

读一行时，从最新的版本开始，用这个版本的 `DB_TRX_ID` 判断：

- 比列表中最小的编号还小：这个事务在读视图建立前就提交了，**可见**；
- 大于等于「下一个编号」：这个事务在读视图建立之后才开始，**不可见**；
- 在列表中：读视图建立时它还没提交，**不可见**；
- 其他（在范围内但不在列表中）：建立读视图时已经提交了，**可见**。

不可见就沿着回滚指针看上一个版本，直到找到可见的为止。点按钮查看：

<div class="dsviz" data-viz="mvcc-read"></div>

两种隔离级别用的是同一套规则，区别只在于**什么时候建立读视图**：

- **可重复读**：事务中第一次读时建立，之后一直使用它，所以每次读到的都一样；
- **读已提交**：每条 `SELECT` 都重新建立，所以每次都能看到最新提交的数据。

读未提交不使用读视图，直接读最新的版本；串行化在可重复读的基础上，给读加锁。

MVCC 的好处是**读不阻塞写，写也不阻塞读**：B 读 100 时不需要等 A，A 修改时也不需要等 B。只有两个事务修改同一行时，才需要用锁排队（下一篇）。

### 3.3 长事务的代价

旧版本存在 undo 日志中，什么时候删除？只有当**没有任何读视图还可能用到它**时，后台的 purge 线程才能清理它。一个长时间不结束的事务，会让它开始之后的所有旧版本都无法清理。

实测：B 开始一个事务并建立快照，然后 C 调用一个存储过程，对李四的余额自动提交地加 1，共 1 万次（每次是一个独立的事务）：

```
  0.0s A> SELECT … FROM information_schema.innodb_metrics WHERE name = 'trx_rseg_history_len';
        A| | trx_rseg_history_len |          19 |
  0.6s B> START TRANSACTION WITH CONSISTENT SNAPSHOT;
  1.2s B> SELECT balance FROM account WHERE id = 2;
        B| |   50.00 |
        B| 这条 SELECT 用了 0.678 毫秒
  2.4s C> CALL bump(10000);
        C| Query OK, 1 row affected (2.39 sec)
  7.0s A> SELECT … 'trx_rseg_history_len';
        A| | trx_rseg_history_len |       10000 |
  7.7s A> SELECT balance FROM account WHERE id = 2;
        A| | 10050.00 |
  8.3s B> SELECT balance FROM account WHERE id = 2;
        B| |   50.00 |
        B| 这条 SELECT 用了 8.812 毫秒
  9.5s B> COMMIT;
 13.1s A> SELECT … 'trx_rseg_history_len';
        A| | trx_rseg_history_len |        1602 |
```

（`trx_rseg_history_len` 是等待清理的 undo 记录的数量，即 `SHOW ENGINE INNODB STATUS` 中的 History list length。每条 `SELECT` 的耗时从 `performance_schema` 中读取。）

- B 的快照还在，1 万个事务产生的旧版本一个都不能清理，历史长度从 19 涨到了 10000；
- A 读到的最新值是 10050，B 读到的仍然是 50。为了找到 50，B 要从最新的版本开始，沿着版本链往回走 1 万步，读一行用了 8.8 毫秒；同样的读，在没有长版本链时只要 0.034 毫秒，慢了约 260 倍；
- B 提交后，purge 线程开始清理，3 秒多后历史长度降到了 1602。

真实的系统中，一个忘了提交的事务（比如开着一个交互式客户端，执行完 `START TRANSACTION` 后就去吃饭了）可以存在几个小时，undo 日志不断膨胀，所有读到被频繁修改的行的查询都越来越慢。可以用这条查询找出运行时间很长的事务：

```sql
SELECT trx_id, trx_started, TIMESTAMPDIFF(SECOND, trx_started, NOW()) AS 秒, trx_mysql_thread_id
FROM information_schema.innodb_trx ORDER BY trx_started LIMIT 5;
```

## 4. 丢失更新

两个事务都想从张三的账户扣钱：A 扣 30，B 扣 20。程序的写法是「先读出余额，在程序中计算，再把结果写回去」：

```
  0.0s A> START TRANSACTION;
  0.6s A> SELECT balance FROM account WHERE id = 1;
        A| |  100.00 |
  1.2s B> START TRANSACTION;
  1.8s B> SELECT balance FROM account WHERE id = 1;
        B| |  100.00 |
  2.4s A> UPDATE account SET balance = 70.00 WHERE id = 1;
  3.0s A> COMMIT;
  3.6s B> UPDATE account SET balance = 80.00 WHERE id = 1;
  4.3s B> COMMIT;
  4.9s B> SELECT balance FROM account WHERE id = 1;
        B| |   80.00 |
```

两笔扣款之后余额应该是 50，结果是 80：A 的修改被 B 覆盖了，这叫**丢失更新**。这就是第 1 章计数器实验中文件计数出错的原因，用了事务、可重复读也没能避免，因为 B 读到的是快照中的 100，而这个 100 在 B 写回时已经过时了。

### 4.1 用原子的 UPDATE

让数据库在当前值上计算，而不是写回程序算好的结果：

```
  0.0s A> START TRANSACTION;
  0.6s A> SELECT balance FROM account WHERE id = 1;
        A| |  100.00 |
  1.2s B> START TRANSACTION;
  1.8s B> SELECT balance FROM account WHERE id = 1;
        B| |  100.00 |
  2.4s A> UPDATE account SET balance = balance - 30 WHERE id = 1;
  3.0s A> COMMIT;
  3.6s B> SELECT balance FROM account WHERE id = 1;
        B| |  100.00 |
  4.3s B> UPDATE account SET balance = balance - 20 WHERE id = 1;
  4.9s B> SELECT balance FROM account WHERE id = 1;
        B| |   50.00 |
  5.5s B> COMMIT;
```

B 的快照读仍然是 100，但 `UPDATE` 是当前读，在最新的 70 上减去 20，得到正确的 50。如果 A 还没提交，B 的 `UPDATE` 会等 A 的锁释放后再执行。第 1 章的 MySQL 计数器能得到 2000，靠的就是 `SET n = n + 1`。

### 4.2 用 SELECT … FOR UPDATE

如果计算比较复杂，必须先读出来在程序中处理，就在读的时候加锁：

```
  0.0s A> START TRANSACTION;
  0.6s A> SELECT balance FROM account WHERE id = 1 FOR UPDATE;
        A| |  100.00 |
  1.2s B> START TRANSACTION;
  1.8s B> SELECT balance FROM account WHERE id = 1 FOR UPDATE;
  2.4s A> UPDATE account SET balance = 70.00 WHERE id = 1;
  3.0s A> COMMIT;
        B| |   70.00 |
  3.6s B> UPDATE account SET balance = 50.00 WHERE id = 1;
  4.3s B> COMMIT;
  4.9s B> SELECT balance FROM account WHERE id = 1;
        B| |   50.00 |
```

`FOR UPDATE` 是当前读，并且给读到的行加上排他锁。B 的 `SELECT … FOR UPDATE` 在 1.8 秒发出，一直等到 A 在 3.0 秒提交后才返回，读到的是最新的 70，在它上面计算出 50。两个事务对这一行的「读、算、写」被排成了队。

## 5. 怎样选择隔离级别

- **可重复读**（MySQL 默认）：事务内的读一致，适合报表、对账这类需要一致快照的场景；
- **读已提交**：每条语句看到最新提交的数据，加锁更少（下一篇会看到，可重复读要加间隙锁），死锁更少。很多互联网公司把 MySQL 改成了读已提交；
- 无论哪个级别，「读出来、算好、写回去」都可能丢失更新，要用原子的 `UPDATE` 或者 `SELECT … FOR UPDATE`；
- 读未提交和串行化很少使用。

## 6. 常见错误

- **以为可重复读下 `UPDATE` 也基于快照**：`UPDATE` 是当前读，基于最新的数据；
- **以为用了事务就不会丢失更新**：先读后写的模式，任何隔离级别下都可能丢失更新；
- **以为快照在 `START TRANSACTION` 时建立**：是在第一次读时；
- **长事务**：旧版本无法清理，读越来越慢，undo 日志膨胀；
- **在不同的会话中使用不同的隔离级别却没有意识到**：同一段代码在不同的连接上行为不同。

## 7. 小结

- 并发事务的三种问题：脏读、不可重复读、幻读；四种隔离级别分别允许其中的几种
- 实测：读未提交读到了被回滚的 70；读已提交两次读到 100 和 70；可重复读两次都是 100；串行化下 `UPDATE` 等了 2.13 秒
- 快照读读事务的快照，当前读（`UPDATE`、`FOR UPDATE` 等）读最新的数据并加锁；可重复读下 `UPDATE` 修改了快照中看不到的新行
- MVCC：每行的旧版本在 undo 日志中，用回滚指针连成版本链；读视图根据事务编号判断哪个版本可见；可重复读在第一次读时建立读视图，读已提交每条语句建立
- 长事务让旧版本无法清理：实测历史长度涨到 1 万，读一行慢了约 260 倍
- 丢失更新：先读后写把 50 写成了 80；用 `SET balance = balance - x` 或 `SELECT … FOR UPDATE` 避免

## 8. 练习

1. 库存只剩 1 件，两个会话同时下单。用一条 `UPDATE` 实现「有库存才扣减」，怎样知道是否扣减成功？
2. 读已提交下，2.4 节的实验结果会有什么不同？
3. 在演示中，如果读视图建立时活跃事务列表是 [200, 201]（201 也已经开始了），可重复读下 B 读到的是多少？

<details>
<summary>点击查看答案</summary>

1. 把「有库存」作为 `UPDATE` 的条件，用影响的行数判断：

   ```sql
   UPDATE stock SET qty = qty - 1 WHERE id = 1 AND qty >= 1;
   ```

   实测：

   ```
     0.0s A> START TRANSACTION;
     0.6s A> UPDATE stock SET qty = qty - 1 WHERE id = 1 AND qty >= 1;
           A| Query OK, 1 row affected (0.00 sec)
     1.2s B> START TRANSACTION;
     1.8s B> UPDATE stock SET qty = qty - 1 WHERE id = 1 AND qty >= 1;
     2.4s A> COMMIT;
           B| Query OK, 0 rows affected (0.62 sec)
           B| Rows matched: 0  Changed: 0  Warnings: 0
   ```

   A 扣减成功（1 行）。B 的 `UPDATE` 等 A 提交后才执行（等了 0.62 秒），用当前读看到 `qty` 已经是 0，不满足条件，影响 0 行。程序根据影响的行数判断：1 行是成功，0 行是库存不足。检查和扣减在同一条语句中完成，不会有两个人都看到「还剩 1 件」的情况。
2. 读已提交下，每条 `SELECT` 都重新建立读视图。A 插入并提交王五后，B 的第二次 `SELECT` 就能看到王五（3 行），这就是幻读。之后的 `UPDATE` 同样修改 3 行。
3. 从最新版本开始：40 的版本由事务 201 写入，201 在活跃列表中，不可见；70 的版本由事务 200 写入，200 也在列表中，不可见；100 的版本由事务 100 写入，100 小于最小的活跃编号 200，可见。读到的仍然是 100。只要读视图建立时 200 还没提交，它和之后的修改都不可见。

</details>
