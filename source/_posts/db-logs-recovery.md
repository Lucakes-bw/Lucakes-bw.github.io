---
title: 日志与崩溃恢复
date: 2026-10-25 11:10:00
updated: 2026-10-25 11:10:00
categories:
  - 数据库
  - 事务与锁
tags:
  - 数据库
  - MySQL
  - 事务
  - 日志
order: 4
description: 提交成功的数据，断电后还在吗？本文让一个客户端不停地插入，记下服务器确认提交的最后一条，然后 kill -9 杀掉 MySQL 再重启：默认设置下一条不少，日志中可以看到 InnoDB 应用了 333 条 redo 记录；改成每秒才写一次日志后，重启后丢了 6～57 条已确认的提交。还有一次，表中的数据反而比确认的多了一条，那是处于两阶段提交中间的事务，被恢复过程提交了。最后测量这几种设置的代价：1 万次提交从 2.4 秒到 0.28 秒。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 redo 日志、undo 日志、binlog 各自的作用；理解预写日志（WAL）为什么能让提交又快又可靠；理解 `innodb_flush_log_at_trx_commit` 和 `sync_binlog` 的含义和代价；理解两阶段提交和崩溃恢复的过程。
>
> **前置知识**：本章前三篇；第 3 章的页和缓冲池。了解 `fsync`（Linux 基础专栏《文件 IO》）。
>
> **实验环境**：MySQL 8.4.13（Docker）。崩溃用 `docker kill -s KILL` 模拟：向 MySQL 服务器进程发送 SIGKILL，进程立即终止，来不及做任何收尾工作，相当于 `kill -9`。注意它模拟的是**进程崩溃**，操作系统仍然正常运行；断电的情况第 4 节再讨论。

## 1. 问题：修改了内存中的页，什么时候写回磁盘

第 3 章说过，InnoDB 读写数据以页为单位，读过的页缓存在缓冲池中。修改一行时，修改的是缓冲池中的页，这样的页叫**脏页**。

提交时，要不要把脏页写回磁盘？

- 写：一个事务修改了 10 行，这 10 行可能在 10 个不同的页中，就要随机地写 10 个 16 KB 的页，提交很慢；
- 不写：提交后服务器崩溃，内存中的修改就丢了，违背了持久性。

InnoDB 的办法是**预写日志**（WAL，Write-Ahead Logging）：提交时不写数据页，而是把「对哪个页做了什么修改」记录到 **redo 日志**中，并保证 redo 日志写到了磁盘上。数据页以后在后台慢慢写回。崩溃后重启时，根据 redo 日志把还没写回的修改重做一遍，数据就恢复了。

redo 日志的写入是**顺序的追加**，而且很小。插入一行，看 redo 日志的位置（LSN，日志序列号，就是写了多少字节）前进了多少：

```
插入 1 行：LSN 增加 313 字节；再插入 1 万行（逐行提交）：增加 2973619 字节
```

一行只要几百字节的日志，而不是写回一个 16 KB 的页。顺序地写几百字节，比随机地写若干个 16 KB 的页快得多。

redo 日志是固定大小、循环使用的一组文件（这里是 `#innodb_redo` 目录中的 32 个 3.2 MB 的文件，共 100 MB，由 `innodb_redo_log_capacity` 设置）。后台线程不断把脏页写回磁盘，写回之后，对应的 redo 日志就不再需要了，这个位置叫**检查点**（checkpoint）。崩溃恢复只需要从检查点开始重做。

<figure class="diagram">
<svg viewBox="0 0 660 230" role="img" aria-label="一次提交的过程：修改缓冲池中的页并写 undo 日志，写 redo 日志并在提交时刷到磁盘，写 binlog 并刷到磁盘，脏页在之后由后台线程写回数据文件">
  <defs>
    <marker id="wal-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/>
    </marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <text x="20" y="20" style="fill:var(--muted)">内存</text>
    <rect x="20" y="30" width="180" height="50" rx="6" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="32" y="50" style="fill:var(--accent)">缓冲池</text>
    <text x="32" y="68" style="fill:var(--fg)">修改页（脏页）+ undo</text>
    <rect x="240" y="30" width="180" height="50" rx="6" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="252" y="50" style="fill:var(--accent)">redo 日志缓冲</text>
    <text x="252" y="68" style="fill:var(--fg)">记录「改了什么」</text>
    <rect x="460" y="30" width="180" height="50" rx="6" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="472" y="50" style="fill:var(--accent)">binlog 缓存</text>
    <text x="472" y="68" style="fill:var(--fg)">记录「哪些行变了」</text>
    <text x="20" y="140" style="fill:var(--muted)">磁盘</text>
    <rect x="20" y="150" width="180" height="50" rx="6" style="fill:var(--panel-2);stroke:var(--fg-2)"/>
    <text x="32" y="170" style="fill:var(--fg)">数据文件 .ibd</text>
    <text x="32" y="188" style="fill:var(--muted)">后台慢慢写回</text>
    <rect x="240" y="150" width="180" height="50" rx="6" style="fill:var(--panel-2);stroke:var(--warn)"/>
    <text x="252" y="170" style="fill:var(--warn)">redo 日志文件</text>
    <text x="252" y="188" style="fill:var(--muted)">提交时 fsync ①</text>
    <rect x="460" y="150" width="180" height="50" rx="6" style="fill:var(--panel-2);stroke:var(--warn)"/>
    <text x="472" y="170" style="fill:var(--warn)">binlog 文件</text>
    <text x="472" y="188" style="fill:var(--muted)">提交时 fsync ②</text>
    <line x1="110" y1="82" x2="110" y2="148" style="stroke:var(--fg-2);stroke-dasharray:4,3" marker-end="url(#wal-arrow)"/>
    <line x1="330" y1="82" x2="330" y2="148" style="stroke:var(--warn)" marker-end="url(#wal-arrow)"/>
    <line x1="550" y1="82" x2="550" y2="148" style="stroke:var(--warn)" marker-end="url(#wal-arrow)"/>
    <text x="118" y="120" style="fill:var(--muted)">检查点</text>
  </g>
</svg>
</figure>

## 2. 三种日志

| | redo 日志 | undo 日志 | binlog |
|---|---|---|---|
| 属于 | InnoDB | InnoDB | MySQL 服务器层（与存储引擎无关） |
| 记录 | 对页的物理修改 | 修改前的旧值 | 修改了哪些行（或执行了什么语句） |
| 用途 | 崩溃后**重做**已提交的修改（持久性） | **回滚**事务（原子性）；MVCC 的旧版本（隔离性） | 主从复制、按时间点恢复（第 5 章） |
| 大小 | 固定，循环使用 | 不再需要的旧版本被 purge 清理 | 不断增长，按文件切分，定期删除 |

undo 日志前两篇已经见过：回滚和 MVCC 的版本链都靠它。undo 日志本身的修改也会记录在 redo 日志中，所以崩溃后 undo 日志也能恢复。

binlog 是 MySQL 服务器的日志，记录所有修改数据的操作，默认是 ROW 格式（记录每一行修改前后的值）。执行一条 `UPDATE` 后查看 binlog 中的事件：

```sql
SHOW BINLOG EVENTS IN 'binlog.000014';
```

```
+---------------+-----+----------------+-----------+-------------+--------------------------------------+
| Log_name      | Pos | Event_type     | Server_id | End_log_pos | Info                                 |
+---------------+-----+----------------+-----------+-------------+--------------------------------------+
| binlog.000014 |   4 | Format_desc    |         1 |         127 | Server ver: 8.4.13, Binlog ver: 4    |
| binlog.000014 | 127 | Previous_gtids |         1 |         158 |                                      |
| binlog.000014 | 158 | Anonymous_Gtid |         1 |         237 | SET @@SESSION.GTID_NEXT= 'ANONYMOUS' |
| binlog.000014 | 237 | Query          |         1 |         319 | BEGIN                                |
| binlog.000014 | 319 | Table_map      |         1 |         381 | table_id: 90 (tx.account)            |
| binlog.000014 | 381 | Update_rows    |         1 |         451 | table_id: 90 flags: STMT_END_F       |
| binlog.000014 | 451 | Xid            |         1 |         482 | COMMIT /* xid=28 */                  |
+---------------+-----+----------------+-----------+-------------+--------------------------------------+
```

一个事务在 binlog 中是一组事件：`BEGIN`，然后是表的信息（`Table_map`）和行的修改（`Update_rows`，70 字节，包含这一行修改前后的值），最后是提交（`Xid`）。从库读取这些事件，就能重放主库的修改（5.3 篇）。

## 3. 提交时写到哪一步

为了持久性，提交时 redo 日志和 binlog 都要写到磁盘上。但「写到磁盘」有两步：`write` 把数据交给操作系统（进入操作系统的页缓存），`fsync` 让操作系统真正写到磁盘。`fsync` 很慢（要等磁盘确认），所以 MySQL 提供了两个参数来取舍：

| `innodb_flush_log_at_trx_commit` | 提交时对 redo 日志做什么 | MySQL 进程崩溃 | 操作系统崩溃、断电 |
|---|---|---|---|
| **1**（默认） | `write` + `fsync` | 不丢 | 不丢 |
| 2 | 只 `write`，每秒 `fsync` 一次 | 不丢 | 可能丢最近约 1 秒 |
| 0 | 什么都不做，每秒 `write` + `fsync` 一次 | 可能丢最近约 1 秒 | 可能丢最近约 1 秒 |

`sync_binlog` 对 binlog 做类似的控制：1（默认）表示每次提交都 `fsync`，0 表示交给操作系统决定什么时候写。

### 3.1 代价

1 万次插入，每次自动提交，在不同的设置下各执行两次：

```
innodb_flush_log_at_trx_commit=1 sync_binlog=1：2437 毫秒
innodb_flush_log_at_trx_commit=1 sync_binlog=1：2401 毫秒
innodb_flush_log_at_trx_commit=1 sync_binlog=0：1007 毫秒
innodb_flush_log_at_trx_commit=1 sync_binlog=0：994 毫秒
innodb_flush_log_at_trx_commit=2 sync_binlog=0：472 毫秒
innodb_flush_log_at_trx_commit=2 sync_binlog=0：469 毫秒
innodb_flush_log_at_trx_commit=0 sync_binlog=0：279 毫秒
innodb_flush_log_at_trx_commit=0 sync_binlog=0：294 毫秒
```

默认设置下，每次提交要做两次 `fsync`（redo 日志和 binlog），平均每次提交约 0.24 毫秒；去掉 binlog 的 `fsync`，快了一倍多；redo 日志也不 `fsync`，又快了一倍；连 `write` 都不做，最快，接近 9 倍。（这台机器的磁盘是虚拟机中的固态硬盘，`fsync` 比较快。在机械硬盘上，一次 `fsync` 要几毫秒，差距会大得多。）

快是有代价的。下面用崩溃来验证。

### 3.2 崩溃实验

一个客户端在一个连接中不停地执行「插入一行，然后 `SELECT` 这一行的编号」，把收到的编号写进文件。客户端收到某个编号，说明这个编号所在的插入已经提交成功了（自动提交，`INSERT` 返回时已经提交）。3 秒后，`kill -9` 杀掉 MySQL 服务器，再重启，比较「客户端收到确认的最后一个编号」和「表中实际有的最大编号」：

```bash
docker exec -d mysql84 bash -c 'for i in $(seq 1 1000000); do echo "INSERT INTO c (v) VALUES ($i); SELECT $i;"; done | mysql -N --unbuffered tx > /tmp/acked.txt'
sleep 3
docker kill -s KILL mysql84
docker start mysql84
```

**默认设置**（1 和 1）：

```
innodb_flush_log_at_trx_commit = 1
客户端收到确认的最后一个编号：11268
重启后表中的最大编号和行数：11268 11268
```

一条不少。重启时的日志（MySQL 8.4 默认不输出这些信息，要设置 `log_error_verbosity = 3`）：

```
[InnoDB] Database was not shutdown normally!
[InnoDB] Starting crash recovery.
[InnoDB] Starting to parse redo log at lsn = 877440036, whereas checkpoint_lsn = 877440070 and start_lsn = 877440000
[InnoDB] Doing recovery: scanned up to log sequence number 880861593
[InnoDB] Applying a batch of 333 redo log records ...
[InnoDB] Apply batch completed!
...
[InnoDB] Transaction 454577 was in the XA prepared state.
...
[Server] Recovering after a crash using binlog
[InnoDB] 1 transactions in prepared state after recovery
[Server] Crash recovery finished in InnoDB engine. Successfully committed 1 internal transaction(s).
```

（这段日志来自另一次同样设置的崩溃，第 5 节会解释其中的 prepared 状态。）InnoDB 发现上次没有正常关闭，从检查点开始扫描 redo 日志，扫描到 880861593，重做了 333 条 redo 记录。检查点之后的修改都在 redo 日志中，所以已经提交的数据全部恢复了。

**设置为 0**，三次：

```
客户端收到确认的最后一个编号：60876，重启后表中的最大编号：60870
客户端收到确认的最后一个编号：61859，重启后表中的最大编号：61843
客户端收到确认的最后一个编号：61252，重启后表中的最大编号：61195
```

分别丢了 6、16、57 个**已经告诉客户端提交成功**的事务。redo 日志还在 MySQL 进程的内存中，没来得及写出去，进程就被杀了。

**设置为 2**，两次：

```
客户端收到确认的最后一个编号：43782，重启后表中的最大编号：43782
客户端收到确认的最后一个编号：43344，重启后表中的最大编号：43344
```

没有丢。设置为 2 时，每次提交都把 redo 日志 `write` 给了操作系统，MySQL 进程被杀，操作系统还在，页缓存中的数据最终会写到磁盘上。但如果是断电或者操作系统崩溃，页缓存中还没 `fsync` 的数据就丢了，这个实验无法模拟。

所以：

- **1**：最安全，最慢，钱、订单这类数据必须用它；
- **2**：MySQL 崩溃不丢，机器断电可能丢约 1 秒，可以接受这个风险时使用；
- **0**：MySQL 崩溃就可能丢，只适合可以重新生成的数据（例如批量导入时临时使用）。

## 4. 断电时数据页写到一半

redo 日志能重做「对页的修改」，前提是页本身是完好的。但写一个 16 KB 的页时断电，可能只写了一部分（操作系统和磁盘写入的单位通常是 4 KB），页损坏了，redo 日志也无法在一个损坏的页上重做。

InnoDB 的**双写缓冲**（doublewrite buffer，`innodb_doublewrite = ON`）解决这个问题：写回脏页时，先把页顺序地写到双写文件中（数据目录中的 `#ib_16384_0.dblwr`），`fsync` 后，再写到数据文件中的位置。如果写数据文件时断电，恢复时可以从双写文件中取出完好的副本。这让写回脏页多了一次写入，但只是顺序写，代价不大。

## 5. 两阶段提交

redo 日志和 binlog 是两个独立的日志，提交时要保证它们**一致**：一个事务要么两个日志中都有，要么都没有。否则，比如 redo 日志中有而 binlog 中没有，主库上这个事务提交了，从库却永远收不到它，主从的数据就不一致了。

MySQL 用**两阶段提交**（2PC）来保证：

1. **prepare**：InnoDB 写 redo 日志，把事务标记为「准备好了」（prepared），`fsync`；
2. 写 binlog，`fsync`；
3. **commit**：InnoDB 把事务标记为已提交（这一步的日志不需要立即 `fsync`）。

崩溃恢复时，对每个处于 prepared 状态的事务，检查它是否在 binlog 中：

- 在 binlog 中：说明第 2 步已经完成，**提交**它；
- 不在：说明第 2 步没有完成，**回滚**它。

前面那次崩溃恢复的日志正好碰到了这种情况：

```
客户端收到确认的最后一个编号：11408
重启后表中的最大编号和行数：11409 11409
```

```
[InnoDB] Transaction 454577 was in the XA prepared state.
[InnoDB] Resurrected 1 transactions doing inserts.
[Server] Recovering after a crash using binlog
[InnoDB] Transaction 454577 in prepared state after recovery
[Server] Found 1 prepared transaction(s) in InnoDB
[Server] Crash recovery finished in InnoDB engine. Successfully committed 1 internal transaction(s).
```

插入 11409 的事务，在崩溃时已经完成了 prepare 和写 binlog，但还没来得及完成第 3 步、更没来得及告诉客户端。恢复时，MySQL 在 binlog 中找到了它，于是把它提交了。结果是：**表中的数据比客户端确认的多了一条**。

这说明了一件很重要的事：客户端发出 `COMMIT` 后，如果连接断了、没有收到回复，**它不知道事务到底提交了没有**。可能没提交，也可能已经提交了。程序在这种情况下不能简单地重试（可能重复下单、重复扣款），而要先查询确认，或者把操作设计成可以安全地重复执行（例如用唯一的订单号，重复插入会因为唯一约束而失败）。

## 6. 常见错误

- **为了性能把 `innodb_flush_log_at_trx_commit` 设为 0 或 2，却没有意识到代价**：实测设为 0 时，进程崩溃丢了最多 57 个已确认的事务；
- **把 `sync_binlog` 设为 0**：崩溃后 binlog 中可能缺少已提交的事务，从库和按时间点恢复都会受影响；
- **以为没收到 `COMMIT` 的回复就是没提交**：两阶段提交中崩溃的事务可能在恢复时被提交；
- **redo 日志太小**：写入很多时，检查点追不上（实验中错误日志就出现过 `Consider increasing innodb_redo_log_capacity` 的警告），写入会被迫等待；
- **以为 binlog 能代替备份，或者备份能代替 binlog**：两者配合才能恢复到任意时间点（5.2 篇）。

## 7. 小结

- 预写日志：提交时只保证 redo 日志写到磁盘，数据页之后再写；redo 日志顺序写，一行只要几百字节
- redo 日志用于崩溃后重做（持久性），undo 日志用于回滚和 MVCC，binlog 用于复制和按时间点恢复
- `innodb_flush_log_at_trx_commit`：1 每次提交都 `fsync`，2 只交给操作系统，0 每秒一次。实测 1 万次提交：2.4 秒到 0.28 秒
- 崩溃实验：默认设置一条不丢；设为 0 丢了 6～57 个已确认的事务；设为 2 在进程崩溃时不丢，但断电可能丢
- 双写缓冲防止断电时页写到一半
- 两阶段提交保证 redo 日志和 binlog 一致；恢复时，prepared 并且在 binlog 中的事务会被提交，实测表中比客户端确认的多了一条

## 8. 练习

1. 为什么 InnoDB 不在提交时直接把修改过的数据页写回磁盘？用本文的数字估算一下。
2. 设置为 2 时，MySQL 进程崩溃不丢数据。那么在什么情况下会丢？最多丢多少？
3. 一个支付接口：扣款的事务 `COMMIT` 后，网络断了，程序没有收到回复。程序应该怎么做？

<details>
<summary>点击查看答案</summary>

1. 插入一行，redo 日志只增加约 300 字节，而且是在日志文件末尾**顺序追加**；如果提交时写回数据页，至少要写一个 16 KB 的页（插入还会修改二级索引的页），位置是**随机**的，还可能触发双写。1 万次提交产生的 redo 日志约 3 MB，如果改成每次写回一个页，就是 160 MB 的随机写。另外，同一个页在短时间内被多次修改（比如自增主键总是插在最后一个页），写回数据页的方式每次都要写整个页，而攒起来以后只需要写一次。
2. 操作系统崩溃或者断电时会丢。设置为 2 时，redo 日志在提交时只进入了操作系统的页缓存，每秒才 `fsync` 一次，断电时页缓存中的数据就丢了，最多丢失约 1 秒内提交的事务。（如果 `sync_binlog` 也不是 1，binlog 中同样可能缺少这些事务。）
3. 不能直接重试，也不能直接当作失败。事务可能已经提交了（就像第 5 节中多出的那一条）。应该：先查询这笔支付的状态（比如按支付单号查扣款记录），确认是否已经扣款，再决定是否重试；更好的做法是让接口**幂等**：每笔支付有唯一的支付单号，扣款记录表对它有唯一约束，重复执行时插入扣款记录会失败，这样即使重试，也只会扣一次。

</details>
