---
title: LSM 树与 B+ 树：同样的 200 万次写入
date: 2026-11-13 10:50:00
updated: 2026-11-13 10:50:00
categories:
  - 数据库
  - 存储引擎
tags:
  - 数据库
  - 存储引擎
  - LSM
  - InnoDB
order: 6
description: 同样的 200 万条随机键（16 字节的键、100 字节的值，每 1000 条提交一次），分别写进 lsm-kv 和 MySQL 8.4 的 InnoDB，从容器的 cgroup 统计磁盘上实际写了多少字节。lsm-kv 用了 4.6 秒，写盘 1.7 GB，文件 241 MB；InnoDB 的缓冲池调到 768 MB 时用了 6 秒，写盘 4.3 GB，表文件 476 MB；默认的 128 MB 缓冲池装不下整张表，用了 38 秒，写盘 26.9 GB，是数据量的 122 倍。本文用 InnoDB 自己的状态变量拆开这些数字：每一页被反复写了多少次、双写缓冲让写入翻倍、随机插入让页只有一半是满的。最后说清楚两种结构各适合什么场景，以及这个对比哪里不公平。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用 cgroup 的 `io.stat` 测量一个进程（容器）实际写盘的字节数；理解 B+ 树随机插入时写放大的来源：脏页刷新、双写缓冲、redo 日志、页分裂；理解缓冲池大小对 B+ 树写放大的决定性影响；能说清 LSM 树和 B+ 树各自的优势、代价和适用场景。
>
> **前置知识**：本章第 1～5 篇；本专栏的《InnoDB 的存储结构》《日志与崩溃恢复》（缓冲池、redo、doublewrite）《B+ 树索引》。
>
> **实验环境**：同第 1 篇。MySQL 8.4.13 的官方镜像，`--skip-log-bin`（关掉 binlog，只比较存储引擎），其余保持默认：`innodb_flush_log_at_trx_commit = 1`（每次提交 `fsync` redo 日志）、双写缓冲打开、页 16 KB。lsm-kv 每 1000 次写入 `fsync` 一次日志，对应 InnoDB 每 1000 行提交一次。两者都在单独的 Docker 容器中运行，用容器 cgroup 的 `io.stat` 中的 `wbytes` 统计写盘量（包括文件系统自己的写入），统计到写入结束 20 秒后（让后台的刷盘完成）。

## 1. 实验

数据：200 万条，键是 16 字节的随机十六进制串（和前几篇的 `bench fill ... rand` 完全相同），值 100 字节，一共 221.3 MB。

**lsm-kv**：

```bash
./build/bench fill /data/sync1000 2000000 100 rand 1000 10
```

**InnoDB**：

```sql
CREATE TABLE t (k VARBINARY(16) PRIMARY KEY, v VARBINARY(100)) ENGINE=InnoDB;
```

用一个脚本生成 2000 条 `INSERT INTO t VALUES (...), (...), ...;`，每条 1000 行（自动提交，每条语句一个事务），通过 `mysql` 命令行执行。

结果：

| | 耗时 | 磁盘写入 | 写放大 | 数据文件 |
|---|---|---|---|---|
| lsm-kv | 4.6 秒 | 1,734 MB | 7.8 | 241 MB |
| InnoDB，缓冲池 768 MB | 6 秒 | 4,302 MB | 19.4 | 476 MB |
| InnoDB，缓冲池 128 MB（默认） | 38 秒 | 26,895 MB | **122** | 476 MB |

<figure class="diagram">
<svg viewBox="0 0 700 230" role="img" aria-label="同样的 200 万条（221 MB）写入，磁盘实际写入的字节数（对数坐标）：lsm-kv 随机键 1734 MB；InnoDB 顺序主键 1255 MB；InnoDB 随机主键、缓冲池 768 MB 4302 MB；缓冲池 128 MB 26895 MB">
  <defs><marker id="cmp-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="cmp-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="220.0" y="36.0" text-anchor="end" style="fill:var(--fg);font-size:12px">lsm-kv，随机键</text><rect x="230.0" y="20.0" width="125.6" height="22.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="363.6" y="36.0" text-anchor="start" style="fill:var(--muted);font-size:11px">1,734 MB，4.6 秒</text><text x="220.0" y="74.0" text-anchor="end" style="fill:var(--fg);font-size:12px">InnoDB，顺序主键，768 MB</text><rect x="230.0" y="58.0" width="101.8" height="22.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="339.8" y="74.0" text-anchor="start" style="fill:var(--muted);font-size:11px">1,255 MB，3 秒</text><text x="220.0" y="112.0" text-anchor="end" style="fill:var(--fg);font-size:12px">InnoDB，随机，768 MB</text><rect x="230.0" y="96.0" width="192.7" height="22.0" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="430.7" y="112.0" text-anchor="start" style="fill:var(--muted);font-size:11px">4,302 MB，6 秒</text><text x="220.0" y="150.0" text-anchor="end" style="fill:var(--fg);font-size:12px">InnoDB，随机，128 MB</text><rect x="230.0" y="134.0" width="328.0" height="22.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="566.0" y="150.0" text-anchor="start" style="fill:var(--muted);font-size:11px">26,895 MB，38 秒</text><line x1="315.0" y1="176" x2="315.0" y2="182" style="stroke:var(--border-2)"/><text x="315.0" y="198.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1 GB</text><line x1="485.0" y1="176" x2="485.0" y2="182" style="stroke:var(--border-2)"/><text x="485.0" y="198.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">10 GB</text><line x1="230" y1="176" x2="587.4" y2="176" style="stroke:var(--border-2)"/><text x="20.0" y="222.0" text-anchor="start" style="fill:var(--muted);font-size:11px">写入的数据 221 MB；InnoDB 关闭了 binlog，每 1000 行提交一次，lsm-kv 每 1000 次写入 fsync 一次</text>
  </g>
</svg>
</figure>

写放大 = 磁盘写入 ÷ 221.3 MB。lsm-kv 的 1734 MB 和它自己统计的（日志 + 落盘 + 合并，第 5 篇）基本一致。

## 2. 拆开 InnoDB 的写入

InnoDB 的状态变量能说明这些字节去了哪里：

| 状态变量 | 缓冲池 768 MB | 缓冲池 128 MB |
|---|---|---|
| `Innodb_os_log_written`（redo 日志） | 545 MB | 556 MB |
| `Innodb_pages_written`（写出的数据页） | 104,342 | 823,650 |
| `Innodb_dblwr_pages_written`（写进双写缓冲的页） | 104,201 | 820,935 |
| `Innodb_buffer_pool_reads`（缓冲池没命中、从磁盘读的页） | 1,029 | 705,746 |

**redo 日志**：545 MB，约为数据的 2.5 倍（每行的日志记录还包括页号、偏移、事务信息等），这一部分两种缓冲池一样。

**数据页**：表文件 476 MB，约 3 万个 16 KB 的页。缓冲池 768 MB 时写出了 10.4 万页，**每一页平均被写了 3.4 次**；缓冲池 128 MB 时写出了 82.4 万页，**每一页平均被写了 27 次**。

原因是随机插入。B+ 树按主键排序，随机的键会落在任意一个叶子页中，插入一行就要把那一页变成脏页。缓冲池装得下整张表时，一个页可以在内存中积累很多次修改再写出，只是检查点会定期把脏页刷回磁盘（一页被修改、刷出、再被修改、再刷出，所以平均 3.4 次）。缓冲池只有 128 MB，装不下 476 MB 的表时，页刚被改了几行就要被换出去腾地方，换出前必须写回；下一次插入落到这一页，又要从磁盘读回来（70 万次读），改几行，再写回。**为了插入一行 116 字节，反复读写整个 16 KB 的页**。

**双写缓冲**：每个数据页在写回表文件之前，先写一份到双写缓冲（《日志与崩溃恢复》：防止一个 16 KB 的页只写了一半），`Innodb_dblwr_pages_written` 和 `Innodb_pages_written` 几乎相等，数据页的写入直接翻倍。

合计（768 MB 的情况）：redo 545 MB + 数据页 10.4 万 × 16 KB × 2（双写）≈ 3.7 GB，加上文件系统的元数据和日志，和测得的 4.3 GB 大致对得上。

## 3. 文件大小：页只满了一半

InnoDB 的表文件是 476 MB，数据只有 221 MB（每行再加上约 20 字节的行头和事务信息，约 270 MB）。随机插入时，一个满了的叶子页要**分裂**成两个半满的页（《B+ 树索引》），新的行会陆续把它们填满，但同时又有别的页在分裂，稳定下来以后，页平均只有 50%～70% 是满的。

lsm-kv 的 SSTable 是一次写满的，没有空闲空间，241 MB 只比数据多了 9%（每条的长度头、索引、布隆过滤器）。RocksDB 还会对数据块压缩，差距更大。

## 4. 这个对比不公平的地方

数字很漂亮，但要清楚它在比什么：

- **InnoDB 做的事多得多**：事务、MVCC、行锁、二级索引、外键、在线 DDL，每一行都有事务 ID 和回滚指针，还有 SQL 的解析和执行。lsm-kv 只是一个单线程的键值存储；
- **同样是 LSM 树的生产引擎，写放大通常比 lsm-kv 大**：RocksDB 的经验值是 10～30，还有更多的元数据和后台工作；
- **B+ 树的随机写入可以优化**：缓冲池足够大、增大 redo 日志让检查点不那么频繁、使用有序的主键（自增 ID），写放大都会大大降低。在 768 MB 的缓冲池上，把同样的 200 万条改成按顺序递增的键再写一次：3 秒，写盘 1255 MB（写放大 5.7），表文件只有 296 MB；写出的数据页 19501 个，而表文件一共 18944 页，**每一页基本只写了一次**，剩下的主要是 redo 日志（337 MB）和双写缓冲；
- **只比了写入**。

## 5. 读的一面

读取方面，B+ 树有天然的优势：

- **点查**：B+ 树从根走到叶子，上面几层在缓冲池中，通常只读 1 个叶子页。LSM 树的数据分散在多层中，没有布隆过滤器时，查一个存在的键平均要读 2.80 个块（第 4 篇），有了过滤器是 1.02 个，还要多花内存和 CPU；
- **范围查询**：B+ 树的叶子页之间有链表，顺序读即可。LSM 树要把所有层的迭代器归并在一起，跳过旧版本和墓碑，大量删除以后，扫描会经过很多墓碑；
- **读的稳定性**：LSM 树的读性能受合并状态影响：L0 积压时，每个查询都要多查好几个文件。

## 6. 怎么选

| | B+ 树（InnoDB、PostgreSQL、SQLite） | LSM 树（RocksDB、LevelDB、Cassandra） |
|---|---|---|
| 写入 | 原地更新，随机写时写放大大 | 顺序写，写放大中等，写入吞吐量高 |
| 点查 | 稳定，通常一次读 | 需要布隆过滤器，受合并状态影响 |
| 范围查询 | 很好 | 要归并多层，较好 |
| 空间 | 页半满，空间放大约 1.5～2 | 一次写满、可压缩，空间放大约 1.1 |
| 延迟 | 比较平稳 | 合并会造成尖刺，需要调优 |
| 典型用途 | 事务型业务、读多写少 | 写多读少：日志、时序、消息、计数、缓存的持久化 |

**写入量大、数据量远大于内存、能接受稍慢一点的读**，LSM 树更合适：TiDB、CockroachDB 的存储层用 RocksDB / Pebble，Facebook 在部分 MySQL 集群上用 MyRocks（MySQL + RocksDB）替换了 InnoDB，据他们公开的数据，存储空间约省了一半，写入量也大幅下降。**读多写少、需要稳定的读延迟、大量的范围查询和复杂的事务**，B+ 树仍然是更稳妥的选择。

## 7. 本章回顾

lsm-kv 从一个日志文件开始，一步步长成了一个完整的存储引擎：

1. 预写日志：先写日志再改内存，CRC 校验，崩溃后重放；`fsync` 每次 7525 次/秒，不同步 143 万次/秒；
2. 内存表：有序的跳表，覆盖写在内存中合并，删除写墓碑，写满 4 MB 落盘；
3. SSTable：有序、不可修改，数据块 + 常驻内存的索引，查一个键只读一个块；
4. 布隆过滤器：查不存在的键从读 3 个块降到 0.03 个；
5. 合并：多路归并、分层、平移；写放大 7.76（顺序写入 2.18），合并造成写入卡顿，空间放大 1.3～1.5；
6. 和 InnoDB 比：同样的 200 万次随机写入，写盘 1.7 GB 对 4.3 GB（缓冲池 768 MB）、26.9 GB（128 MB）。

真正的 RocksDB 在这些零件上又做了大量的工作：后台线程和写入限流、多个列族、块缓存和压缩、事务和快照、前缀布隆过滤器、多种合并策略。但核心的思路就是这一章的六篇。

## 8. 练习

1. 如果把 InnoDB 的主键改成自增整数（插入顺序和主键顺序一致），本文的实验结果会怎样变化？
2. 为什么说缓冲池大小对 B+ 树的写放大有决定性影响，而对 LSM 树影响不大？
3. 一个计数服务每秒对几百万个不同的计数器各加 1，读得很少。选 B+ 树还是 LSM 树？还有什么优化？

<details>
<summary>点击查看答案</summary>

1. 新的行总是插到最右边的叶子页，写满一页就开始下一页，前面的页不会再被修改，不会被反复写出；页也是写满了才开始新的一页（InnoDB 对顺序插入有专门的处理），填充率高，表文件小得多。本文第 4 节实测（缓冲池 768 MB）：3 秒，写盘 1255 MB，写放大 5.7，表文件 296 MB，每个数据页基本只写一次，剩下的是 redo 日志和双写缓冲。缓冲池大小的影响也小得多。这和 LSM 树中顺序写入时用平移代替合并是同一个道理：数据按顺序到达时，两种结构都很便宜。

2. B+ 树原地更新：随机插入时，能不能在一个页被写出之前积累多次修改，取决于这一页能在缓冲池中待多久，也就是缓冲池能装下多大比例的表。装不下时，每次修改几乎都要换来一次页的读和写。LSM 树的写入只追加日志和内存表，落盘和合并都是大块的顺序读写，和数据量与内存的比例关系不大（内存表的大小有一点影响）。缓冲池（块缓存）对 LSM 树主要影响读。

3. LSM 树更合适：每次加 1 都是一次写入，B+ 树要找到计数器所在的页并原地修改，计数器很多时就是大量随机的页写入。LSM 树把它们顺序写入。进一步的优化：用 RocksDB 的合并操作符（merge operator），只写入「加 1」这个增量，读取或合并时再把增量累加起来，不需要先读出旧值；还可以在应用层先在内存中聚合一段时间（比如 1 秒）的增量，再批量写入。

</details>
