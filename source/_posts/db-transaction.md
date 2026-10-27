---
title: 事务与 ACID
date: 2026-10-25 10:40:00
updated: 2026-10-25 10:40:00
categories:
  - 数据库
  - 事务与锁
tags:
  - 数据库
  - MySQL
  - 事务
order: 1
description: 转账要先给一个账户加钱，再从另一个账户扣钱。本文实测：不用事务时，扣钱那一步失败了，加的 200 元却留了下来，凭空多出了 200 元；用了事务，出错后如果还是 COMMIT，这 200 元照样留下，必须自己 ROLLBACK；事务中途断开连接会自动回滚；CREATE TABLE 会悄悄提交当前的事务。然后用两个会话同时操作，看看未提交的修改别人能不能看到。最后测量：1 万次插入，每次自动提交用了 2.4 秒，放在一个事务中只要 0.2 秒。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解为什么需要事务；会用 `START TRANSACTION`、`COMMIT`、`ROLLBACK`、`SAVEPOINT`；理解自动提交；知道语句出错时事务不会自动回滚、哪些语句会隐式提交；理解 ACID 四个特性；理解提交的代价。
>
> **前置知识**：第 1 章的增删改查；第 1 章《数据库与 MySQL 入门》开头的计数器实验。
>
> **实验环境**：MySQL 8.4.13（Docker），默认的 InnoDB 存储引擎和隔离级别。本章的实验经常要两个会话同时操作，为此写了一个小脚本：每个会话是一个独立的 `mysql` 客户端进程，脚本按顺序把语句发给指定的会话，每条语句之后等 0.6 秒，再显示各个会话的输出。输出中 `1.2s A>` 表示第 1.2 秒时会话 A 执行的语句，下面缩进的 `A|` 是会话 A 的输出。

## 1. 转账的问题

一个账户表，张三有 100 元，李四有 50 元。余额不能为负，用 `CHECK` 约束保证：

```sql
CREATE TABLE account (
    id      INT UNSIGNED PRIMARY KEY,
    name    VARCHAR(20) NOT NULL,
    balance DECIMAL(10, 2) NOT NULL CHECK (balance >= 0)
);
INSERT INTO account VALUES (1, '张三', 100.00), (2, '李四', 50.00);
```

张三要给李四转 200 元（他其实只有 100 元）。转账是两条语句：给李四加 200，从张三扣 200。

```
  0.0s A> UPDATE account SET balance = balance + 200 WHERE id = 2;
        A| Query OK, 1 row affected (0.00 sec)
        A| Rows matched: 1  Changed: 1  Warnings: 0
  0.6s A> UPDATE account SET balance = balance - 200 WHERE id = 1;
        A| ERROR 3819 (HY000): Check constraint 'account_chk_1' is violated.
  1.2s A> SELECT * FROM account;
        A| +----+------+---------+
        A| | id | name | balance |
        A| +----+------+---------+
        A| |  1 | 张三 |  100.00 |
        A| |  2 | 李四 |  250.00 |
        A| +----+------+---------+
```

扣钱失败了（张三的余额会变成 -100），但加钱已经成功，李四多了 200 元，张三一分没少。银行凭空多出了 200 元。

这里失败的原因是余额不足，换成任何别的原因结果都一样：程序在两条语句之间崩溃了、网络断了、服务器断电了。问题在于，「转账」这件事由两步组成，它们必须**要么都完成，要么都不做**，而默认情况下，MySQL 把每一条语句当作独立的一件事，执行完就立即生效。

## 2. 事务

**事务**（transaction）把多条语句组成一个整体：

```sql
START TRANSACTION;      -- 或者 BEGIN
UPDATE …;
UPDATE …;
COMMIT;                 -- 提交：所有的修改一起生效
-- 或者
ROLLBACK;               -- 回滚：撤销这个事务中所有的修改
```

### 2.1 出错以后：COMMIT 还是 ROLLBACK

同样的转账，放进事务中，扣钱失败后提交：

```
  0.0s A> START TRANSACTION;
        A| Query OK, 0 rows affected (0.00 sec)
  0.6s A> UPDATE account SET balance = balance + 200 WHERE id = 2;
        A| Query OK, 1 row affected (0.00 sec)
  1.2s A> UPDATE account SET balance = balance - 200 WHERE id = 1;
        A| ERROR 3819 (HY000): Check constraint 'account_chk_1' is violated.
  1.8s A> COMMIT;
        A| Query OK, 0 rows affected (0.01 sec)
  2.4s A> SELECT * FROM account;
        A| +----+------+---------+
        A| | id | name | balance |
        A| +----+------+---------+
        A| |  1 | 张三 |  100.00 |
        A| |  2 | 李四 |  250.00 |
        A| +----+------+---------+
```

结果和不用事务时一模一样。**一条语句出错，只是这条语句本身没有执行，事务并没有结束，也没有回滚**，之前成功的语句仍然在事务中。这时如果 `COMMIT`，就把「加了 200 元」提交了。

正确的做法是，任何一步出错，都执行 `ROLLBACK`：

```
  1.2s A> UPDATE account SET balance = balance - 200 WHERE id = 1;
        A| ERROR 3819 (HY000): Check constraint 'account_chk_1' is violated.
  1.8s A> ROLLBACK;
        A| Query OK, 0 rows affected (0.00 sec)
  2.4s A> SELECT * FROM account;
        A| +----+------+---------+
        A| | id | name | balance |
        A| +----+------+---------+
        A| |  1 | 张三 |  100.00 |
        A| |  2 | 李四 |   50.00 |
        A| +----+------+---------+
```

两个账户都回到了原来的样子。在程序中，事务的写法通常是这样的结构：

```
开始事务
try:
    执行第 1 条语句
    执行第 2 条语句
    提交
except 任何错误:
    回滚
    把错误报告给上层
```

事务提供的是「能够整体撤销」的**能力**，什么时候撤销，要由程序决定。

### 2.2 自动回滚的情况

有两种情况，事务会被自动回滚：

**连接断开。** 会话 A 开始事务，把张三的余额改成 0，然后客户端退出了（没有提交）：

```
  0.0s A> START TRANSACTION;
  0.6s A> UPDATE account SET balance = 0 WHERE id = 1;
        A| Query OK, 1 row affected (0.01 sec)
（会话 A 退出）

+----+------+---------+
| id | name | balance |
+----+------+---------+
|  1 | 张三 |  100.00 |
|  2 | 李四 |   50.00 |
+----+------+---------+
```

服务器发现连接断开，就回滚这个连接上未提交的事务。程序崩溃、网络中断都属于这种情况，所以「两条语句之间程序崩溃」不会留下一半的转账。

**死锁。** 两个事务互相等待对方的锁时，InnoDB 会选择回滚其中一个（4.3 篇）。

此外，执行 SQL 脚本时要注意：命令行客户端默认遇到错误就停止并退出，相当于断开连接，事务会被回滚（练习 1）。

### 2.3 隐式提交

有些语句会**先提交当前的事务**，再执行自己。最常见的是 DDL（`CREATE`、`ALTER`、`DROP`、`TRUNCATE` 等）：

```
  0.0s A> START TRANSACTION;
  0.6s A> UPDATE account SET balance = 999 WHERE id = 2;
        A| Query OK, 1 row affected (0.00 sec)
  1.2s A> CREATE TABLE log (msg VARCHAR(100));
        A| Query OK, 0 rows affected (0.01 sec)
  1.8s A> ROLLBACK;
        A| Query OK, 0 rows affected (0.00 sec)
  2.4s A> SELECT * FROM account WHERE id = 2;
        A| +----+------+---------+
        A| | id | name | balance |
        A| +----+------+---------+
        A| |  2 | 李四 |  999.00 |
        A| +----+------+---------+
```

`ROLLBACK` 没有报错，但 999 已经被 `CREATE TABLE` 提交了，回滚的是一个空的事务。所以不要在事务中执行 DDL。在 MySQL 中，DDL 本身也不能回滚。再次执行 `START TRANSACTION`、`LOCK TABLES` 等语句也会隐式提交。

### 2.4 保存点

`SAVEPOINT` 在事务中设置一个位置，可以只回滚到这个位置，而不是整个事务：

```
  3.0s A> START TRANSACTION;
  3.7s A> UPDATE account SET balance = 1 WHERE id = 1;
  4.3s A> SAVEPOINT s1;
  4.9s A> UPDATE account SET balance = 2 WHERE id = 2;
  5.5s A> ROLLBACK TO SAVEPOINT s1;
  6.1s A> COMMIT;
  6.7s A> SELECT * FROM account;
        A| +----+------+---------+
        A| | id | name | balance |
        A| +----+------+---------+
        A| |  1 | 张三 |    1.00 |
        A| |  2 | 李四 |  999.00 |
        A| +----+------+---------+
```

（接着上一个实验，李四的余额是 999。）保存点 `s1` 之后的修改被撤销了，之前的修改随 `COMMIT` 一起提交。保存点适合「一个大事务中，某一步失败了可以换一种方式重试」的场景。

## 3. 自动提交

在 `START TRANSACTION` 之外执行的语句，难道不在事务中吗？其实也在。MySQL 默认开启**自动提交**（`autocommit = 1`）：每一条单独的语句都是一个事务，执行完自动提交。第 1 节的两条 `UPDATE` 就是两个独立的事务，所以第二个失败不影响第一个。`START TRANSACTION` 的作用就是临时关闭自动提交，直到 `COMMIT` 或 `ROLLBACK`。

也可以用 `SET autocommit = 0` 关闭自动提交，之后的语句都在一个事务中，直到显式提交。这样做很危险：忘记提交时，修改别人看不到，并且这个事务一直持有锁（练习 2）。一般保持自动提交开启，需要事务时用 `START TRANSACTION`。

## 4. 别人能看到未提交的修改吗

两个会话同时操作：

```
  0.0s A> START TRANSACTION;
  0.6s A> UPDATE account SET balance = balance - 30 WHERE id = 1;
        A| Query OK, 1 row affected (0.00 sec)
  1.2s A> SELECT balance FROM account WHERE id = 1;
        A| +---------+
        A| | balance |
        A| +---------+
        A| |   70.00 |
        A| +---------+
  1.8s B> SELECT balance FROM account WHERE id = 1;
        B| +---------+
        B| | balance |
        B| +---------+
        B| |  100.00 |
        B| +---------+
  2.4s A> COMMIT;
  3.0s B> SELECT balance FROM account WHERE id = 1;
        B| +---------+
        B| | balance |
        B| +---------+
        B| |   70.00 |
        B| +---------+
```

会话 A 在自己的事务中看到了修改后的 70，会话 B 看到的还是 100；A 提交之后，B 才看到 70。未提交的修改只有自己能看到，如果它最终被回滚，别人就像从来没见过它一样。这是**隔离性**，下一篇详细讨论：B 在什么时候能看到 A 的修改，有好几种不同的规定。

## 5. ACID

事务的四个特性，通常用四个字母概括：

| 特性 | 含义 | 本文的例子 | InnoDB 怎样实现 |
|---|---|---|---|
| **A**tomicity 原子性 | 事务中的修改要么全部生效，要么全部不生效 | 回滚后两个账户都恢复了 | undo 日志：记录修改前的值，回滚时据此恢复（4.4 篇） |
| **C**onsistency 一致性 | 事务前后，数据都满足所有的约束和业务规则 | 余额不能为负；转账前后总额不变 | 由约束、原子性、隔离性共同保证，也依赖程序正确 |
| **I**solation 隔离性 | 并发的事务互不干扰，就像依次执行一样 | B 看不到 A 未提交的修改 | 锁和多版本并发控制（4.2、4.3 篇） |
| **D**urability 持久性 | 提交成功的修改永久保存，即使随后断电 | — | redo 日志：提交时写入磁盘（4.4 篇） |

一致性和其他三个有些不同：数据库能检查的只是你声明的约束（`CHECK (balance >= 0)`），「转账前后总额不变」这样的业务规则，要靠程序把两个 `UPDATE` 放在一个事务中、出错时回滚来保证。原子性、隔离性、持久性是手段，一致性是目的。

## 6. 提交的代价

插入 1 万行，每条 `INSERT` 都自动提交，和把它们放在一个事务中、最后提交一次，各执行 3 次：

```
第 1 次：自动提交 3369 毫秒，一个事务 199 毫秒
第 2 次：自动提交 2429 毫秒，一个事务 197 毫秒
第 3 次：自动提交 2419 毫秒，一个事务 202 毫秒
```

慢了 12 倍。为了保证持久性，每次提交都要等日志真正写到磁盘上（调用 `fsync`），这一步比执行插入本身慢得多。1 万次提交就是 1 万次 `fsync`，一个事务只要 1 次。（4.4 篇会调整这个行为，并用 `kill -9` 验证它的后果。）

所以批量导入数据时，应该每几百、几千行提交一次。但事务也不是越大越好：

- 大事务持有的锁更多、时间更长，会阻塞其他会话；
- 回滚一个大事务需要很长时间（要逐条撤销）；
- 大事务会让 undo 日志无法清理（下一篇）。

## 7. 常见错误

- **以为出错时事务会自动回滚**：一条语句出错，之前的修改仍然在事务中，出错后要 `ROLLBACK`，不能继续 `COMMIT`；
- **在事务中执行 DDL**：DDL 会隐式提交之前的修改；
- **关闭自动提交后忘记提交**：修改别人看不到，锁一直不释放；
- **一次只插入一行并逐行提交**：批量操作慢十几倍；
- **把很长的操作放进一个事务**：例如在事务中等待用户输入、调用外部的接口，事务越长，持有锁的时间越长；
- **使用不支持事务的存储引擎**：MyISAM 等引擎不支持事务，`ROLLBACK` 对它们没有效果。

## 8. 小结

- 事务把多条语句组成一个整体，`COMMIT` 一起生效，`ROLLBACK` 一起撤销
- 语句出错时事务不会自动回滚，实测出错后 `COMMIT`，转账的一半被提交了；连接断开、死锁时才自动回滚
- DDL 会隐式提交当前的事务；`SAVEPOINT` 可以只回滚一部分
- 默认自动提交：每条语句是一个事务
- 未提交的修改别人看不到（隔离性）
- ACID：原子性（undo 日志）、一致性（目的）、隔离性（锁和 MVCC）、持久性（redo 日志）
- 每次提交都要写磁盘：1 万次插入逐条提交 2.4 秒，一个事务 0.2 秒

## 9. 练习

1. 把转账写成一个 SQL 文件 `START TRANSACTION; UPDATE … + 200; UPDATE … - 200; COMMIT;`，用 `mysql 库名 < 文件` 执行。扣钱失败后，最后的 `COMMIT` 会执行吗？李四的余额是多少？
2. 会话 A 执行 `SET autocommit = 0` 后修改了张三的余额，没有提交；会话 B 要修改张三的余额，会怎样？
3. 如果要导入 100 万行，你会每多少行提交一次？为什么不是 1 行，也不是 100 万行？

<details>
<summary>点击查看答案</summary>

1. 实测：

   ```
   ERROR 3819 (HY000) at line 3: Check constraint 'account_chk_1' is violated.
   退出码 1
   +----+------+---------+
   | id | name | balance |
   +----+------+---------+
   |  1 | 张三 |  100.00 |
   |  2 | 李四 |   50.00 |
   +----+------+---------+
   ```

   命令行客户端执行脚本时，遇到错误就停止，后面的 `COMMIT` 不会执行，客户端随即退出，连接断开，服务器回滚了事务，李四还是 50 元。这次的结果是对的，但它依赖于「出错就停止」这个行为：如果加上 `--force`（出错后继续执行），`COMMIT` 就会执行，加的 200 元又被提交了。
2. 实测（B 把锁等待的超时设成了 3 秒，默认是 50 秒）：

   ```
     0.0s A> SET autocommit = 0;
     0.6s A> UPDATE account SET balance = balance - 10 WHERE id = 1;
     1.8s B> SELECT balance FROM account WHERE id = 1;
           B| |  100.00 |
     2.4s B> UPDATE account SET balance = balance + 5 WHERE id = 1;
           B| ERROR 1205 (HY000): Lock wait timeout exceeded; try restarting transaction
     6.5s A> COMMIT;
     7.1s B> SELECT balance FROM account WHERE id = 1;
           B| |   90.00 |
   ```

   B 能读（读到的是修改前的 100），但不能改：A 的事务一直没有结束，持有这一行的锁，B 等了 3 秒后放弃。A 提交后，锁才释放。一个忘记提交的会话可以让所有需要这一行的操作都卡住。锁的细节见 4.3 篇。
3. 通常每几百到几千行提交一次。逐行提交，每行都要等一次磁盘写入，实测慢 12 倍；100 万行一次提交，事务太大：持有大量的锁，中途出错时回滚要很久，undo 日志在提交前不能清理，主从复制时从库也要等这个大事务全部完成才能应用（5.3 篇）。每批几千行时，提交的开销已经可以忽略，而一批失败了，也只需要重新导入这一批。

</details>
