---
title: 锁与死锁
date: 2026-10-25 11:00:00
updated: 2026-10-25 11:00:00
categories:
  - 数据库
  - 事务与锁
tags:
  - 数据库
  - MySQL
  - 锁
order: 3
description: 两个事务修改同一行时要排队，靠的是锁。本文用 performance_schema.data_locks 直接看到每个事务持有的锁：按主键修改只锁一行；条件列上没有索引时，一条一行都没改的 UPDATE 锁住了全表；查 k = 10 时，可重复读还锁住了 10 前后的两个间隙，插入 7 和 12 都要等待，读已提交下则不会。然后重现两种死锁，读懂 InnoDB 的死锁日志，其中一种来自很常见的「先查一下不存在，再插入」的写法。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解共享锁和排他锁；会用 `performance_schema.data_locks` 和 `sys.innodb_lock_waits` 查看锁和锁等待；理解记录锁、间隙锁、临键锁，以及加锁和索引的关系；理解死锁的成因，能读懂死锁日志，会用固定的加锁顺序等方法减少死锁。
>
> **前置知识**：上一篇《隔离级别与 MVCC》，特别是快照读和当前读的区别。
>
> **实验环境**：MySQL 8.4.13（Docker），隔离级别为可重复读（另有说明的除外）。实验方法同前两篇；会话 C 用来查看锁的情况。

## 1. 为什么需要锁

上一篇看到，MVCC 让读和写互不阻塞：普通的 `SELECT` 读快照，不加锁。但两个事务**同时修改**同一行时，不能都基于旧的版本各改各的，否则就会丢失更新。所以当前读（`UPDATE`、`DELETE`、`INSERT`、`SELECT … FOR UPDATE`、`SELECT … FOR SHARE`）都要加锁：一个事务修改一行时，其他想修改这一行的事务必须等它结束。

锁在事务结束（提交或回滚）时才释放，而不是语句执行完就释放。这叫**两阶段锁**：事务进行中只加锁，结束时一起释放。

## 2. 共享锁和排他锁

| | 共享锁（S） | 排他锁（X） |
|---|---|---|
| 共享锁（S） | 兼容 | 冲突 |
| 排他锁（X） | 冲突 | 冲突 |

- **共享锁**：「我要读，别人不要改」，`SELECT … FOR SHARE` 加共享锁；
- **排他锁**：「我要改，别人既不要改，也不要加共享锁」，`UPDATE`、`DELETE` 和 `SELECT … FOR UPDATE` 加排他锁。

测试表有 4 行，`k` 上有索引：

```sql
CREATE TABLE lk (id INT PRIMARY KEY, k INT NOT NULL, v INT NOT NULL DEFAULT 0, KEY idx_k (k));
INSERT INTO lk (id, k) VALUES (5, 5), (10, 10), (15, 15), (20, 20);
```

A 和 B 都对 `id = 10` 加共享锁，然后 C 要修改它：

```
  0.6s A> SELECT * FROM lk WHERE id = 10 FOR SHARE;
  1.8s B> SELECT * FROM lk WHERE id = 10 FOR SHARE;
  2.4s C> SELECT INDEX_NAME AS idx, LOCK_TYPE AS type, LOCK_MODE AS mode, LOCK_STATUS AS status, LOCK_DATA AS data
          FROM performance_schema.data_locks WHERE OBJECT_NAME = 'lk' …;
        C| +---------+--------+---------------+---------+------+
        C| | idx     | type   | mode          | status  | data |
        C| +---------+--------+---------------+---------+------+
        C| | NULL    | TABLE  | IS            | GRANTED | NULL |
        C| | PRIMARY | RECORD | S,REC_NOT_GAP | GRANTED | 10   |
        C| | NULL    | TABLE  | IS            | GRANTED | NULL |
        C| | PRIMARY | RECORD | S,REC_NOT_GAP | GRANTED | 10   |
        C| +---------+--------+---------------+---------+------+
  3.7s C> UPDATE lk SET v = 3 WHERE id = 10;
        C| ERROR 1205 (HY000): Lock wait timeout exceeded; try restarting transaction
```

`performance_schema.data_locks` 列出了当前所有的锁，每个事务两行：

- 一个**表级**的 `IS` 锁（意向共享锁），表示「我要在这个表的某些行上加共享锁」。意向锁的作用是让「锁整个表」的操作（例如 `LOCK TABLES`、某些 `ALTER TABLE`）能快速知道表中有没有行被锁，而不必逐行检查。行锁之间的冲突与意向锁无关；
- 一个**行级**的 `S,REC_NOT_GAP` 锁：主键索引上 `id = 10` 这条记录的共享锁，只锁记录本身，不锁间隙（第 4 节）。

两个共享锁兼容，A 和 B 都拿到了（`GRANTED`）。C 的 `UPDATE` 要加排他锁，和它们冲突，等待了 1 秒（C 把超时设成了 1 秒，默认 50 秒）后报错。

## 3. 锁加在索引上

### 3.1 按主键修改

```
  0.6s A> UPDATE lk SET v = 1 WHERE id = 10;
  1.2s C> SELECT … FROM performance_schema.data_locks …;
        C| | NULL    | TABLE  | IX            | GRANTED | NULL |
        C| | PRIMARY | RECORD | X,REC_NOT_GAP | GRANTED | 10   |
  2.4s B> UPDATE lk SET v = 2 WHERE id = 15;
        B| Query OK, 1 row affected (0.00 sec)
  3.0s B> UPDATE lk SET v = 2 WHERE id = 10;
        B| ERROR 1205 (HY000): Lock wait timeout exceeded; try restarting transaction
```

A 只锁了 `id = 10` 这一行，B 修改 15 不受影响，修改 10 才要等待。这是行锁的意义：不同的行可以被不同的事务同时修改。

### 3.2 条件列上没有索引

`v` 上没有索引。A 执行一条什么都没修改的 `UPDATE`（没有 `v = 99` 的行）：

```
  2.4s A> UPDATE lk SET k = k WHERE v = 99;
  3.0s C> SELECT … FROM performance_schema.data_locks …;
        C| +---------+--------+------+---------+------------------------+
        C| | idx     | type   | mode | status  | data                   |
        C| +---------+--------+------+---------+------------------------+
        C| | NULL    | TABLE  | IX   | GRANTED | NULL                   |
        C| | PRIMARY | RECORD | X    | GRANTED | 10                     |
        C| | PRIMARY | RECORD | X    | GRANTED | 15                     |
        C| | PRIMARY | RECORD | X    | GRANTED | 20                     |
        C| | PRIMARY | RECORD | X    | GRANTED | 5                      |
        C| | PRIMARY | RECORD | X    | GRANTED | supremum pseudo-record |
        C| +---------+--------+------+---------+------------------------+
  4.3s B> UPDATE lk SET v = 2 WHERE id = 20;
        B| ERROR 1205 (HY000): Lock wait timeout exceeded; try restarting transaction
```

A 锁住了**每一行**，还有 `supremum`（页中「最大」的虚拟记录，锁它表示锁住最后一行之后的间隙）。B 修改一个和 A 毫无关系的行，也要等待。

InnoDB 的行锁是加在**索引记录**上的。没有索引可以定位 `v = 99` 的行，只能扫描整个聚簇索引，扫描到的每一行都要加锁（可重复读下，加锁时还不知道这一行是否满足条件，之后也不会释放）。结果和锁表一样。表有几百万行时，一条没有用上索引的 `UPDATE` 就能让所有修改这个表的操作排队。所以 `UPDATE`、`DELETE` 的条件列要有索引，这不只是为了快，更是为了少加锁。

## 4. 间隙锁和临键锁

### 4.1 锁住「不存在的行」

在 `k` 上查询一个值并加锁：

```
  0.6s A> SELECT * FROM lk WHERE k = 10 FOR UPDATE;
        A| | 10 | 10 | 0 |
  1.2s C> SELECT … FROM performance_schema.data_locks …;
        C| +---------+--------+---------------+---------+--------+
        C| | idx     | type   | mode          | status  | data   |
        C| +---------+--------+---------------+---------+--------+
        C| | NULL    | TABLE  | IX            | GRANTED | NULL   |
        C| | idx_k   | RECORD | X             | GRANTED | 10, 10 |
        C| | idx_k   | RECORD | X,GAP         | GRANTED | 15, 15 |
        C| | PRIMARY | RECORD | X,REC_NOT_GAP | GRANTED | 10     |
        C| +---------+--------+---------------+---------+--------+
  2.4s B> INSERT INTO lk (id, k) VALUES (12, 12);
        B| ERROR 1205 (HY000): Lock wait timeout exceeded; try restarting transaction
  4.1s B> INSERT INTO lk (id, k) VALUES (7, 7);
        B| ERROR 1205 (HY000): Lock wait timeout exceeded; try restarting transaction
  5.7s B> INSERT INTO lk (id, k) VALUES (17, 17);
        B| Query OK, 1 row affected (0.00 sec)
  6.3s B> INSERT INTO lk (id, k) VALUES (3, 3);
        B| Query OK, 1 row affected (0.01 sec)
```

（`LOCK_DATA` 中的 `10, 10` 是二级索引记录的内容：`k` 的值和主键。）A 加了三个锁：

- `idx_k` 上的 `X`：没有 `REC_NOT_GAP`，是**临键锁**（next-key lock），锁住记录 `k = 10` **和它前面的间隙** (5, 10)；
- `idx_k` 上的 `X,GAP`：**间隙锁**，锁住记录 15 前面的间隙 (10, 15)，但不锁 15 本身；
- `PRIMARY` 上的 `X,REC_NOT_GAP`：回表找到的那一行，只锁记录。

<figure class="diagram">
<svg viewBox="0 0 640 120" role="img" aria-label="k = 10 FOR UPDATE 在 idx_k 上加的锁：临键锁覆盖间隙 5 到 10 和记录 10，间隙锁覆盖间隙 10 到 15；插入 7 和 12 被阻塞，插入 3 和 17 不受影响">
  <g style="font-family:var(--mono);font-size:12px">
    <line x1="20" y1="60" x2="620" y2="60" style="stroke:var(--fg-2)"/>
    <rect x="160" y="44" width="130" height="32" style="fill:var(--panel-2);stroke:var(--warn);stroke-dasharray:4,3"/>
    <rect x="290" y="44" width="150" height="32" style="fill:var(--panel-2);stroke:var(--warn);stroke-dasharray:4,3"/>
    <circle cx="160" cy="60" r="14" style="fill:var(--panel-2);stroke:var(--fg-2)"/>
    <text x="160" y="64" text-anchor="middle" style="fill:var(--fg)">5</text>
    <circle cx="290" cy="60" r="14" style="fill:var(--panel-2);stroke:var(--warn);stroke-width:2"/>
    <text x="290" y="64" text-anchor="middle" style="fill:var(--warn)">10</text>
    <circle cx="440" cy="60" r="14" style="fill:var(--panel-2);stroke:var(--fg-2)"/>
    <text x="440" y="64" text-anchor="middle" style="fill:var(--fg)">15</text>
    <circle cx="570" cy="60" r="14" style="fill:var(--panel-2);stroke:var(--fg-2)"/>
    <text x="570" y="64" text-anchor="middle" style="fill:var(--fg)">20</text>
    <text x="160" y="30" style="fill:var(--warn)">临键锁：(5, 10]</text>
    <text x="330" y="30" style="fill:var(--warn)">间隙锁：(10, 15)</text>
    <text x="90" y="104" style="fill:var(--muted)">插入 3：可以</text>
    <text x="200" y="104" style="fill:var(--warn)">插入 7：等待</text>
    <text x="340" y="104" style="fill:var(--warn)">插入 12：等待</text>
    <text x="480" y="104" style="fill:var(--muted)">插入 17：可以</text>
  </g>
</svg>
</figure>

所以 B 插入 `k = 12`（在 10 和 15 之间）和 `k = 7`（在 5 和 10 之间）都被阻塞，插入 17 和 3 不受影响。

为什么要锁间隙？`k` 不是唯一索引，别的事务可能再插入一个 `k = 10` 的行。新的 `k = 10` 只能插在 (5, 10] 或 (10, 15) 这两个间隙中（索引按 `k` 和主键排序），把它们锁住，A 再执行 `SELECT * FROM lk WHERE k = 10 FOR UPDATE` 就不会多出一行，**当前读也不会出现幻读**。这是 InnoDB 在可重复读下防止幻读的另一半（上一篇是快照读的那一半）。

### 4.2 读已提交下没有间隙锁

同样的操作，A 改用读已提交：

```
  0.0s A> SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED;
  0.6s A> START TRANSACTION;
  1.2s A> SELECT * FROM lk WHERE k = 10 FOR UPDATE;
  1.8s C> SELECT … FROM performance_schema.data_locks …;
        C| | NULL    | TABLE  | IX            | GRANTED | NULL   |
        C| | idx_k   | RECORD | X,REC_NOT_GAP | GRANTED | 10, 10 |
        C| | PRIMARY | RECORD | X,REC_NOT_GAP | GRANTED | 10     |
  3.0s B> INSERT INTO lk (id, k) VALUES (12, 12);
        B| Query OK, 1 row affected (0.00 sec)
```

只锁了记录，没有间隙锁，B 的插入立即成功。读已提交本来就允许幻读，所以不需要间隙锁。锁少了，并发更高，死锁也更少（下一节的第二种死锁就是间隙锁造成的），这是很多系统选择读已提交的主要原因。

### 4.3 锁的范围取决于条件和索引

几条经验（可重复读下）：

- 唯一索引上的等值查询，并且记录存在：只锁这条记录（3.1 节）；
- 非唯一索引上的等值查询：锁记录和它两边的间隙（4.1 节）；
- 范围查询：锁扫描过的所有记录和间隙；
- 查询的值不存在：锁它所在的间隙（练习 3）；
- 没有用上索引：锁全表（3.2 节）。

实际加了哪些锁，最可靠的办法是像本文这样查 `performance_schema.data_locks`。

## 5. 死锁

### 5.1 交叉加锁

A 先改 5 再改 10，B 先改 10 再改 5：

```
  0.6s A> UPDATE lk SET v = 1 WHERE id = 5;
  1.8s B> UPDATE lk SET v = 2 WHERE id = 10;
  3.0s A> UPDATE lk SET v = 1 WHERE id = 10;
  3.7s C> SELECT waiting_trx_id, waiting_query, blocking_trx_id, blocking_query FROM sys.innodb_lock_waits;
        C| +----------------+-----------------------------------+-----------------+----------------+
        C| | waiting_trx_id | waiting_query                     | blocking_trx_id | blocking_query |
        C| +----------------+-----------------------------------+-----------------+----------------+
        C| |          68265 | UPDATE lk SET v = 1 WHERE id = 10 |           68266 | NULL           |
        C| +----------------+-----------------------------------+-----------------+----------------+
  4.3s B> UPDATE lk SET v = 2 WHERE id = 5;
        A| Query OK, 1 row affected (1.24 sec)
        B| ERROR 1213 (40001): Deadlock found when trying to get lock; try restarting transaction
```

- 3.0 秒：A 要改 10，但 10 被 B 锁着，A 开始等待。`sys.innodb_lock_waits` 显示了谁在等谁：事务 68265（A）在等 68266（B）。`blocking_query` 是 `NULL`，因为 B 此时没有在执行语句，它只是持有锁；
- 4.3 秒：B 要改 5，但 5 被 A 锁着。现在 A 等 B，B 等 A，谁也无法继续，这就是**死锁**。

InnoDB 会检测死锁（在等待关系中找环），发现后**立即**选择一个事务回滚（通常是修改的行较少、回滚代价小的那个），而不是等到超时。B 收到 `ERROR 1213`，它的整个事务被回滚；A 得到了锁，`UPDATE` 完成（总共等了 1.24 秒）。

### 5.2 读懂死锁日志

最近一次死锁的详细信息在 `SHOW ENGINE INNODB STATUS` 的 `LATEST DETECTED DEADLOCK` 部分（节选）：

```
*** (1) TRANSACTION:
TRANSACTION 68265, ACTIVE 4 sec starting index read
UPDATE lk SET v = 1 WHERE id = 10

*** (1) HOLDS THE LOCK(S):
RECORD LOCKS … index PRIMARY of table `tx`.`lk` trx id 68265 lock_mode X locks rec but not gap
 0: len 4; hex 80000005; asc     ;;

*** (1) WAITING FOR THIS LOCK TO BE GRANTED:
RECORD LOCKS … index PRIMARY of table `tx`.`lk` trx id 68265 lock_mode X locks rec but not gap waiting
 0: len 4; hex 8000000a; asc     ;;

*** (2) TRANSACTION:
TRANSACTION 68266, ACTIVE 2 sec starting index read
UPDATE lk SET v = 2 WHERE id = 5

*** (2) HOLDS THE LOCK(S):
… lock_mode X locks rec but not gap
 0: len 4; hex 8000000a; asc     ;;

*** (2) WAITING FOR THIS LOCK TO BE GRANTED:
… lock_mode X locks rec but not gap waiting
 0: len 4; hex 80000005; asc     ;;

*** WE ROLL BACK TRANSACTION (2)
```

对每个事务，日志给出了正在执行的语句、持有的锁、在等的锁。锁住的记录以字节的形式显示：`0:` 是第一个字段，即主键。`80000005` 就是第 3 章解析 `.ibd` 文件时看到的有符号整数的存储方式（符号位取反），也就是 5；`8000000a` 是 10。于是可以读出：事务 1 持有 5、等待 10，事务 2 持有 10、等待 5，最后回滚了事务 2。

日志只记录了**正在执行**的那条语句，不记录事务之前执行过的语句（是哪条语句锁住了 5，要从程序的逻辑中去找）。`innodb_print_all_deadlocks = ON` 可以把每一次死锁都写进错误日志，而不只是保留最近一次。

### 5.3 「先查一下，不存在再插入」

一段很常见的逻辑：检查 `id = 12` 是否存在，不存在就插入。为了防止两个人同时插入，检查时加了 `FOR UPDATE`：

```
  0.6s A> SELECT * FROM lk WHERE id = 12 FOR UPDATE;
        A| Empty set (0.00 sec)
  1.8s B> SELECT * FROM lk WHERE id = 12 FOR UPDATE;
        B| Empty set (0.00 sec)
  2.4s A> INSERT INTO lk (id, k) VALUES (12, 12);
  3.0s B> INSERT INTO lk (id, k) VALUES (12, 12);
        A| Query OK, 1 row affected (0.62 sec)
        B| ERROR 1213 (40001): Deadlock found when trying to get lock; try restarting transaction
```

两个 `SELECT … FOR UPDATE` 都没有被阻塞，然后两个 `INSERT` 发生了死锁。死锁日志中的锁：

```
*** (1) HOLDS THE LOCK(S):
… lock_mode X locks gap before rec
 0: len 4; hex 8000000f; asc     ;;
*** (1) WAITING FOR THIS LOCK TO BE GRANTED:
… lock_mode X locks gap before rec insert intention waiting
 0: len 4; hex 8000000f; asc     ;;
*** (2) HOLDS THE LOCK(S):
… lock_mode X locks gap before rec
 0: len 4; hex 8000000f; asc     ;;
*** (2) WAITING FOR THIS LOCK TO BE GRANTED:
… lock_mode X locks gap before rec insert intention waiting
 0: len 4; hex 8000000f; asc     ;;
```

- `id = 12` 不存在，`FOR UPDATE` 锁住的是它所在的间隙：记录 15（`8000000f`）前面的间隙 (10, 15)；
- **间隙锁之间不冲突**：间隙锁只是为了阻止插入，两个事务都锁住同一个间隙没有问题，所以 B 的 `SELECT` 没有被阻塞；
- 插入时要在这个间隙中加**插入意向锁**（`insert intention`），它和别的事务的间隙锁冲突。A 的插入等 B 的间隙锁，B 的插入等 A 的间隙锁，死锁。

正确的写法是**不要先查再插**，直接插入，靠主键或唯一约束判断：

```
  0.6s A> INSERT INTO lk (id, k) VALUES (12, 12);
        A| Query OK, 1 row affected (0.00 sec)
  1.8s B> INSERT INTO lk (id, k) VALUES (12, 12);
  2.4s A> COMMIT;
        B| ERROR 1062 (23000): Duplicate entry '12' for key 'lk.PRIMARY'
```

B 的插入等 A 提交后，得到「重复」的错误，程序据此知道已经存在。也可以用 `INSERT IGNORE` 或 `INSERT … ON DUPLICATE KEY UPDATE`（第 1 章）。

### 5.4 减少死锁

死锁无法完全避免，但可以减少：

- **按固定的顺序加锁**：例如转账时总是先锁 `id` 小的账户，再锁 `id` 大的。两个事务以相同的顺序请求锁，后来的只会等待，不会形成环（练习 2）；
- **事务尽量短**，持有锁的时间越短，冲突越少；
- **条件列上要有索引**，少锁不相关的行；
- **用读已提交**，没有间隙锁，避免 5.3 节这类死锁；
- **出现死锁时重试**：被回滚的事务收到 1213 错误，整个事务已经撤销，程序应该从头重新执行这个事务。

## 6. 常见错误

- **`UPDATE`、`DELETE` 的条件列上没有索引**：锁住全表；
- **以为 `FOR UPDATE` 查不到行就没有加锁**：可重复读下锁住了间隙，并且间隙锁之间互不冲突，「先查再插」会死锁；
- **事务中做了耗时的操作**：持有锁的时间被拉长；
- **以不同的顺序修改同一组行**：死锁；
- **收到死锁错误后只重试失败的那一条语句**：整个事务已经回滚了，要从头重试；
- **把锁等待超时设得很长**：默认 50 秒，一个卡住的事务可以让很多请求排队等待几十秒。

## 7. 小结

- 当前读加锁，锁在事务结束时释放；共享锁之间兼容，排他锁和任何锁冲突
- `performance_schema.data_locks` 显示所有的锁，`sys.innodb_lock_waits` 显示谁在等谁
- 行锁加在索引记录上：按主键修改只锁一行；条件列上没有索引时，实测一条没有修改任何行的 `UPDATE` 锁住了全表
- 可重复读下有间隙锁和临键锁，`k = 10 FOR UPDATE` 锁住了 (5, 10] 和 (10, 15)，阻止了插入 7 和 12；读已提交下只锁记录
- 死锁：InnoDB 立即检测并回滚其中一个事务（错误 1213）；死锁日志给出双方持有和等待的锁
- 「先 `FOR UPDATE` 查不存在，再插入」会因为间隙锁死锁，改为直接插入并处理重复的错误；按固定的顺序加锁可以避免交叉死锁

## 8. 练习

1. 5.1 节的两个事务，如果都先改 5 再改 10，会怎样？
2. 转账时，事务要同时修改两个账户。怎样写才能避免两笔方向相反的转账（张三转李四、李四转张三）死锁？
3. 可重复读下，A 执行 `DELETE FROM lk WHERE id = 13`（这一行不存在），没有提交。B 插入 `id = 14`，会被阻塞吗？

<details>
<summary>点击查看答案</summary>

1. 实测：

   ```
     4.3s A> UPDATE lk SET v = v + 1 WHERE id = 5;
     5.5s B> UPDATE lk SET v = v + 1 WHERE id = 5;
     6.1s A> UPDATE lk SET v = v + 1 WHERE id = 10;
     6.7s A> COMMIT;
           B| Query OK, 1 row affected (1.24 sec)
     7.4s B> UPDATE lk SET v = v + 1 WHERE id = 10;
     8.0s B> COMMIT;
   ```

   B 在第一步就等待（5 被 A 锁着），这时 B 还没有锁住任何行，不会挡住 A。A 顺利改完 10 并提交，B 才继续。只是等待，没有死锁。
2. 按账户 `id` 的大小顺序加锁，而不是按「先转出、后转入」的顺序。例如：

   ```sql
   START TRANSACTION;
   -- 不管谁转给谁，都先锁 id 小的，再锁 id 大的
   SELECT * FROM account WHERE id IN (1, 2) ORDER BY id FOR UPDATE;
   UPDATE account SET balance = balance - 30 WHERE id = 2;
   UPDATE account SET balance = balance + 30 WHERE id = 1;
   COMMIT;
   ```

   张三转李四和李四转张三，都先锁 1 再锁 2，后来的那个只会在第一步等待，和练习 1 一样。
3. 会被阻塞。实测：

   ```
     9.2s A> DELETE FROM lk WHERE id = 13;
    10.5s B> INSERT INTO lk (id, k) VALUES (14, 14);
           B| ERROR 1205 (HY000): Lock wait timeout exceeded; try restarting transaction
   ```

   `id = 13` 不存在，`DELETE` 锁住了它所在的间隙 (10, 15)，14 也在这个间隙中。删除一个不存在的行，看起来什么都没做，却挡住了这个范围内所有的插入。

</details>
