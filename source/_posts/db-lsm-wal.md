---
title: 预写日志：先写日志，再改数据
date: 2026-11-13 10:00:00
updated: 2026-11-13 10:00:00
categories:
  - 数据库
  - 存储引擎
tags:
  - 数据库
  - 存储引擎
  - LSM
order: 1
description: 本章用 C 从零写一个 LSM 树键值存储 lsm-kv，第一步是预写日志（WAL）：每次写入先追加到日志文件，再改内存中的数据，崩溃后重放日志就能恢复。本文实现带校验和的日志记录，实测 fsync 的代价：每次写都 fsync 每秒 7525 次，每 10 次 fsync 一次 6.7 万次，从不 fsync 143 万次，差了 190 倍；用 SIGKILL 在随机的时刻杀死写入进程，已确认的写入一条不少；手工截掉日志末尾的 5 个字节，恢复时校验和发现了这条不完整的记录，恢复出前面完整的 3646 条。
top_group_list: true
disableNunjucks: true
viz: [db-lsm]
---

> **本文目标**：了解 LSM 树的整体结构和本章要实现的 lsm-kv；理解预写日志为什么能保证不丢数据；会设计带校验和的日志记录格式，处理写了一半的记录；理解 `write` 和 `fsync` 的区别，以及进程崩溃和断电的区别；知道组提交为什么能大幅提高写入速度。
>
> **前置知识**：本专栏的《日志与崩溃恢复》（InnoDB 的 redo 日志）；《Linux 基础》专栏的《页缓存与写回》（`write` 只写进页缓存，`fsync` 才落盘）；《数据结构》专栏的《跳表》（下一篇用到）。本章的完整代码在 [projects/lsm-kv](https://github.com/Lucakes-bw/Lucakes-bw.github.io/tree/main/projects/lsm-kv)。
>
> **实验环境**：Docker 中的 Ubuntu 24.04 容器，内核 6.8，GCC 13.3；虚拟机 2 核、2 GB 内存；数据目录在 ext4 上。InnoDB 的对照实验用 MySQL 8.4（第 6 篇）。

## 1. 为什么要自己写一个存储引擎

前面几章用 MySQL 的 InnoDB 讲了 B+ 树、redo 日志、缓冲池。InnoDB 是**原地更新**的：要改一行，就找到它所在的 16 KB 的页，在页中修改，再把整页写回磁盘。

另一条路线是 **LSM 树**（Log-Structured Merge Tree）：数据**从不原地修改**，所有的写入都先放在内存中，攒够了按顺序写成一个新文件；旧文件中的过时数据，由后台的**合并**（compaction）慢慢清理。RocksDB、LevelDB、Cassandra、HBase、TiKV 的底层、ClickHouse 的 MergeTree 都是这一类。

这一章从零写一个 LSM 树键值存储 **lsm-kv**，引擎本身 900 多行 C（含注释），每一篇讲一个零件，并实测它的代价：

<figure class="diagram">
<svg viewBox="0 0 700 300" role="img" aria-label="lsm-kv 的结构：写入先追加到 WAL、再放进内存中的跳表；跳表写满就落盘成 L0 的 SSTable；L0 攒够 4 个文件和 L1 合并，各层超过上限就合并到下一层。读取从内存表开始，从新到旧逐层查找，每个文件先问布隆过滤器">
  <defs><marker id="arc-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="arc-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="20.0" y="22.0" text-anchor="start" style="fill:var(--muted);font-size:11px">内存</text><rect x="20" y="32" width="120" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="80.0" y="56.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">put / delete</text><rect x="220" y="32" width="180" height="40" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="310.0" y="48.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">内存表（跳表）</text><text x="310.0" y="63.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">有序，4 MB</text><rect x="480" y="32" width="120" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="540.0" y="56.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">get</text><line x1="20" y1="96" x2="680" y2="96" style="stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:6 4"/><text x="20.0" y="114.0" text-anchor="start" style="fill:var(--muted);font-size:11px">磁盘</text><rect x="20" y="124" width="120" height="40" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="80.0" y="140.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">WAL</text><text x="80.0" y="155.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">只追加</text><line x1="80" y1="72" x2="80" y2="122" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#arc-a)"/><text x="124.0" y="100.0" text-anchor="middle" style="fill:var(--link);font-size:11px">① 先写日志</text><line x1="142" y1="52" x2="218" y2="52" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#arc-a)"/><text x="180.0" y="44.0" text-anchor="middle" style="fill:var(--link);font-size:11px">② 再改内存</text><rect x="220" y="124" width="180" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="310.0" y="140.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">L0：4 个文件</text><text x="310.0" y="155.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">彼此重叠</text><line x1="310" y1="72" x2="310" y2="122" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#arc-a)"/><text x="355.0" y="100.0" text-anchor="middle" style="fill:var(--link);font-size:11px">写满：落盘</text><rect x="220" y="186" width="180" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="310.0" y="208.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">L1：≤ 20 MB</text><rect x="220" y="242" width="180" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="310.0" y="264.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">L2：≤ 200 MB …</text><line x1="310" y1="166" x2="310" y2="184" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#arc-a)"/><text x="340.0" y="178.0" text-anchor="middle" style="fill:var(--link);font-size:11px">合并</text><line x1="310" y1="224" x2="310" y2="240" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#arc-a)"/><text x="340.0" y="236.0" text-anchor="middle" style="fill:var(--link);font-size:11px">合并</text><path d="M540,72 C540,110 470,100 402,52" style="fill:none;stroke:var(--accent);stroke-width:1.5" marker-end="url(#arc-a)"/><text x="560.0" y="120.0" text-anchor="start" style="fill:var(--accent);font-size:11px">读：内存表 → L0 →</text><text x="560.0" y="138.0" text-anchor="start" style="fill:var(--accent);font-size:11px">L1 → L2 ……</text><text x="560.0" y="156.0" text-anchor="start" style="fill:var(--accent);font-size:11px">每个文件先问</text><text x="560.0" y="174.0" text-anchor="start" style="fill:var(--accent);font-size:11px">布隆过滤器</text>
  </g>
</svg>
</figure>

- **写入**：先追加到预写日志（本篇），再放进内存中的跳表（第 2 篇）；跳表写满 4 MB 后整个写成一个有序的只读文件 **SSTable**（第 3 篇），放在第 0 层（L0）；
- **读取**：先查内存表，再从新到旧查每一层的 SSTable，每个文件先问**布隆过滤器**「可能有吗」（第 4 篇）；
- **合并**：L0 的文件达到 4 个，就和 L1 合并成新的文件；某一层超过上限，就合并到下一层（第 5 篇）；
- 最后和 InnoDB 比一比同样的 200 万次写入（第 6 篇）。

对外的接口只有几个函数：

```c
Lsm* lsmOpen(const char* dir, const LsmOptions* o);  // 打开（不存在就创建），并从 WAL 恢复内存表
int lsmPut(Lsm* db, const char* key, uint32_t klen, const char* val, uint32_t vlen);
int lsmDelete(Lsm* db, const char* key, uint32_t klen);
int lsmGet(Lsm* db, const char* key, uint32_t klen, char** val, uint32_t* vlen);
void lsmClose(Lsm* db);
```

写完以后，先用一个随机测试检查它是对的：在 2 万个键上随机地写、删、读一百万次，每 5 万次关闭再打开一次，和一个记录期望值的数组对照：

```
$ ./build/bench check /data/chk 1000000
1000000 次操作，重新打开 20 次，……；结果不一致 0 次
  L2： 10 个文件，    0.6 MB，14308 条
```

测试中把内存表和文件都设得很小（64 KB），让落盘、合并、重新打开、从日志恢复频繁地发生，最终的数据已经被合并到了第 2 层。

<div class="dsviz" data-viz="lsm-flow"></div>

## 2. 预写日志

内存表在内存中，进程一崩溃就没了。所以每次写入，在改内存表**之前**，先把这次写入追加到一个日志文件中，这就是**预写日志**（Write-Ahead Log，WAL）。崩溃后重新打开时，把日志从头读一遍、重新插入内存表，就恢复了崩溃前的状态。等内存表写成了 SSTable，这段日志就没用了，可以删掉。

日志只追加、不修改，是磁盘最擅长的顺序写。

### 2.1 记录的格式

每条记录：

```
[校验和 u32][键长 u32][值长 u32][键][值]
```

值长为 `0xFFFFFFFF` 表示这是一次删除（**墓碑**，第 2 篇）。校验和是对后面所有字节算的 CRC32：

```c
static int writeRecord(Lsm* db, const char* k, uint32_t kl, const char* v, uint32_t vl)
{
    uint32_t vbytes = vl == TOMBSTONE ? 0 : vl;
    size_t n = 12 + kl + vbytes;
    ...
    put32(rec + 4, kl);
    put32(rec + 8, vl);
    memcpy(rec + 12, k, kl);
    if (vbytes) memcpy(rec + 12 + kl, v, vbytes);
    put32(rec, crc32(rec + 4, n - 4));
    writeAll(db->walFd, rec, n);  // 一次 write：要么整条在页缓存中，要么没有
    db->st.walBytes += n;
    if (db->opt.syncEvery && ++db->sinceSync >= db->opt.syncEvery) {
        CHECK(fdatasync(db->walFd) == 0);
        db->sinceSync = 0;
    }
    memPut(db->mem, k, kl, v, vl);      // 日志写好了，才改内存表
    ...
}
```

<figure class="diagram">
<svg viewBox="0 0 700 130" role="img" aria-label="WAL 中一条记录的格式：4 字节的 CRC32 校验和（覆盖后面所有字节）、4 字节的键长、4 字节的值长（0xFFFFFFFF 表示删除）、键、值。16 字节的键和 100 字节的值一共 128 字节">
  <defs><marker id="rec-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rec-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="30" width="60" height="40" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="50.0" y="46.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">校验和</text><text x="50.0" y="61.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">4 字节</text><rect x="84" y="30" width="60" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="114.0" y="46.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">键长</text><text x="114.0" y="61.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">4 字节</text><rect x="148" y="30" width="60" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="178.0" y="46.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">值长</text><text x="178.0" y="61.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">4 字节</text><rect x="212" y="30" width="110" height="40" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="267.0" y="46.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">键（16）</text><text x="267.0" y="61.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">16 字节</text><rect x="326" y="30" width="330" height="40" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="491.0" y="46.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">值（100）</text><text x="491.0" y="61.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">100 字节</text><path d="M84,80 L84,90 L664,90 L664,80" style="fill:none;stroke:var(--warn);stroke-width:1.5"/><text x="374.0" y="108.0" text-anchor="middle" style="fill:var(--warn);font-size:11px">CRC32 的范围：恢复时对不上，就认为日志到此为止</text>
  </g>
</svg>
</figure>

为什么要校验和？崩溃可能发生在一条记录写到一半的时候，日志的末尾就留下一条不完整的记录；磁盘上的数据也可能损坏。恢复时逐条检查，遇到长度不够或者校验和对不上，就认为日志到此为止：

```c
while (off + 12 <= (size_t)s.st_size) {
    uint32_t kl = get32(data + off + 4), vl = get32(data + off + 8);
    uint32_t vb = vl == TOMBSTONE ? 0 : vl;
    if (off + 12 + kl + vb > (size_t)s.st_size) break;                          // 不完整
    if (crc32(data + off + 4, 8 + kl + vb) != get32(data + off)) break;        // 损坏
    memPut(db->mem, data + off + 12, kl, data + off + 12 + kl, vl);
    off += 12 + kl + vb;
    n++;
}
if (off < (size_t)s.st_size) CHECK(ftruncate(fd, (off_t)off) == 0);  // 截掉坏的尾巴
```

最后把文件截断到最后一条好记录的末尾，否则之后追加的新记录会接在坏数据的后面，下次恢复时就再也读不到了。

## 3. fsync 的代价

`write` 返回时，数据只是进了内核的页缓存（《页缓存与写回》），要 `fsync`（或者只同步数据的 `fdatasync`）返回，才确定写到了磁盘上。lsm-kv 用 `syncEvery` 控制多久同步一次。写入 10 万条（16 字节的键、100 字节的值）：

| 同步间隔 | 每秒写入 | `fsync` 次数 |
|---|---|---|
| 每次写都 `fsync` | 7,525 | 100,011 |
| 每 10 次一次 | 67,066 | 10,009 |
| 每 100 次一次 | 403,341 | 1,009 |
| 从不（`syncEvery` = 0） | 1,430,709 | 11 |

<figure class="diagram">
<svg viewBox="0 0 700 200" role="img" aria-label="写入 10 万条，不同的 fsync 间隔下每秒的写入次数（对数坐标）：每次都 fsync 7525 次，每 10 次 67066 次，每 100 次 40.3 万次，从不 fsync 143 万次">
  <defs><marker id="fsy-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="fsy-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="140.0" y="36.0" text-anchor="end" style="fill:var(--fg);font-size:12px">每次都 fsync</text><rect x="150.0" y="20.0" width="96.4" height="22.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="254.4" y="36.0" text-anchor="start" style="fill:var(--muted);font-size:11px">7,525 次/秒</text><text x="140.0" y="72.0" text-anchor="end" style="fill:var(--fg);font-size:12px">每 10 次</text><rect x="150.0" y="56.0" width="200.9" height="22.0" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="358.9" y="72.0" text-anchor="start" style="fill:var(--muted);font-size:11px">67,066 次/秒</text><text x="140.0" y="108.0" text-anchor="end" style="fill:var(--fg);font-size:12px">每 100 次</text><rect x="150.0" y="92.0" width="286.6" height="22.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="444.6" y="108.0" text-anchor="start" style="fill:var(--muted);font-size:11px">403,341 次/秒</text><text x="140.0" y="144.0" text-anchor="end" style="fill:var(--fg);font-size:12px">从不</text><rect x="150.0" y="128.0" width="347.1" height="22.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="505.1" y="144.0" text-anchor="start" style="fill:var(--muted);font-size:11px">1,430,709 次/秒</text><line x1="150.0" y1="168" x2="150.0" y2="174" style="stroke:var(--border-2)"/><text x="150.0" y="190.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1 千</text><line x1="260.0" y1="168" x2="260.0" y2="174" style="stroke:var(--border-2)"/><text x="260.0" y="190.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1 万</text><line x1="370.0" y1="168" x2="370.0" y2="174" style="stroke:var(--border-2)"/><text x="370.0" y="190.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">10 万</text><line x1="480.0" y1="168" x2="480.0" y2="174" style="stroke:var(--border-2)"/><text x="480.0" y="190.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">100 万</text><line x1="150" y1="168" x2="524.0" y2="168" style="stroke:var(--border-2)"/>
  </g>
</svg>
</figure>

每次都 `fsync` 时，每次写入约 133 微秒，几乎全是等磁盘确认的时间；不 `fsync` 时，一次写入不到 1 微秒。差了 **190 倍**。（11 次 `fsync` 是落盘 SSTable 和写清单文件时做的。）这台虚拟机的磁盘由 Mac 的 SSD 支撑，在普通的云盘或机械硬盘上，一次 `fsync` 要几毫秒，差距更大。

每 10 次同步一次，速度就提高了 9 倍：一次 `fsync` 的代价被 10 次写入平摊了。数据库把这叫做**组提交**（group commit）：多个事务的日志攒在一起，一次 `fsync`。MySQL 的 `innodb_flush_log_at_trx_commit`、`sync_binlog`，Redis 的 `appendfsync everysec`（《持久化：RDB 与 AOF》），都是在这个代价上做取舍。

## 4. 崩溃以后

`crash` 程序让子进程不停地写，每次 `lsmPut` 返回后，把「已确认的条数」记在共享内存中；父进程等一段时间后用 `SIGKILL` 杀死子进程，再打开数据库检查：

```
同步间隔 1（每次 fsync）：
lsm: 从 WAL 恢复了 5351 条记录
杀死子进程时，已确认写入 5350 条
重新打开后：已确认的 5350 条中找到 5350 条，丢失 0 条；未确认就被杀死的写入找到 1 条

同步间隔 0（从不 fsync），运行 1500 毫秒：
lsm: 从 WAL 恢复了 15519 条记录
杀死子进程时，已确认写入 1125742 条
重新打开后：已确认的 1125742 条中找到 1125742 条，丢失 0 条；未确认就被杀死的写入找到 1 条
```

已确认的写入一条都没丢。还有 1 条「未确认」的也找到了：子进程被杀时，这条已经写进了日志、还没来得及更新共享内存中的计数。对调用者来说，没有确认的写入可能成功也可能失败，两种都是正确的。

**从不 `fsync` 也一条没丢**，这是因为进程被杀死时，`write` 已经把数据交给了内核，页缓存中的数据不属于这个进程，内核之后照样会写回磁盘。**`fsync` 防的是操作系统崩溃和断电**：那时页缓存中还没写回的数据才会丢失。本文的实验没有模拟断电（需要虚拟机级别的强制关机），但道理很清楚：「已确认」在断电后仍然成立，必须在确认之前 `fsync`。

另一个现象：同步间隔为 0、运行 700 毫秒的两次实验，杀死时已确认的条数**都是 634465**，而且没有从日志恢复任何记录。原因是那一刻正好在做一次很长的合并（第 5 篇），合并期间写入被挡住了，两次都卡在同一个位置；合并前刚刚落盘、换了新的日志，所以日志是空的。

**撕裂的尾巴**。用 `nocheck` 模式在杀死子进程后保留现场，日志有 3647 条完整的记录（466816 字节，每条 128 字节）。用 `truncate -s -5` 截掉最后 5 个字节，模拟最后一条写了一半，再打开：

```
WAL 466816 字节 = 3647 条 × 128 字节
截掉 5 字节后 466811
lsm: 从 WAL 恢复了 3646 条记录
```

最后一条长度不够，被丢弃，前面的 3646 条完整恢复，文件被截断到它们的末尾。

## 5. 清单：原子地切换文件

除了日志，还要记录「现在有哪些 SSTable、各在哪一层、当前用的是哪个日志文件」，这就是**清单**（MANIFEST）。落盘和合并都会改变文件的集合，清单的更新必须是原子的：要么是旧的，要么是新的，不能写了一半。做法是先写一个临时文件、`fsync`，再 `rename` 覆盖旧的，最后 `fsync` 目录：

```c
FILE* f = fopen(tmp, "w");
fprintf(f, "next %llu\nwal %llu\n", ...);
for (各层的每个文件) fprintf(f, "table %d %llu\n", l, number);
fflush(f);
fsync(fileno(f));
fclose(f);
rename(tmp, fin);   // rename 是原子的
syncDir(db);        // 让「目录中的这一项指向新文件」也落盘
```

`rename` 在同一个文件系统上是原子操作：任何时刻打开 `MANIFEST`，看到的都是一个完整的文件。顺序也很重要：新的 SSTable 先写好并 `fsync`，再写清单，最后才删除旧的日志和文件，任何一步之间崩溃，都还能从旧的清单恢复。

## 6. 常见错误

1. **先改内存，再写日志**。两步之间崩溃，内存中的修改已经对读者可见、却没有记录，恢复后就不见了。
2. **日志记录没有校验和**。写了一半的记录会被当成正常数据重放，读出错误的键和值。
3. **以为 `write` 成功就落盘了**。进程崩溃不会丢，断电会丢。
4. **恢复时遇到坏记录不截断文件**。后面追加的新记录在下次恢复时被一起丢掉。
5. **直接覆盖写清单文件**。写到一半崩溃，清单损坏，整个数据库都打不开了。

## 7. 小结

- LSM 树不原地修改数据：写入先进日志和内存表，攒够了顺序写成只读的 SSTable，后台合并清理旧数据；
- 预写日志：先追加日志，再改内存；记录带 CRC32 校验和，恢复时遇到不完整或损坏的记录就停下并截断；
- `fsync` 很贵：每次写都同步每秒 7525 次，不同步 143 万次；组提交把一次 `fsync` 平摊给多次写入；
- 进程被 `SIGKILL` 杀死时，已确认的写入一条不丢（不同步也一样，数据在页缓存中）；`fsync` 防的是断电和系统崩溃；
- 清单用「写临时文件、`fsync`、`rename`」原子地更新。

## 8. 练习

1. 一个服务要求「已确认的写入在断电后不丢」，又需要每秒 5 万次写入，而一次 `fsync` 约 1 毫秒。怎么做？
2. 为什么 lsm-kv 用 `fdatasync` 而不是 `fsync`？
3. 如果日志记录的格式改成「[键长][值长][键][值][校验和]」，把校验和放在最后，有什么好处和坏处？

<details>
<summary>点击查看答案</summary>

1. 每次写入单独 `fsync`，每秒最多约 1000 次，不够。用组提交：多个线程的写入先进同一个缓冲区，由一个线程一次写入日志、一次 `fsync`，然后同时通知这一批写入「已确认」。每批 50 条以上，每秒 1000 次 `fsync` 就能支持 5 万次写入。代价是每次写入的延迟增加（要等这一批凑齐或者等一个很短的超时）。RocksDB、InnoDB 都是这样做的。

2. `fsync` 除了同步数据，还要同步文件的元数据（修改时间等）；`fdatasync` 只在必要时同步元数据（比如文件变长了，文件大小必须同步，否则读不到新数据）。日志一直在追加，每次的文件大小都变了，所以差别不大；但对于覆盖写的文件（比如预先分配好空间的日志），`fdatasync` 可以省掉一次元数据的写入。

3. 好处：可以边写边算校验和，最后再写出来（流式写入），不必先把整条记录准备好。坏处：恢复时要先读出键长、值长，才能找到校验和；如果长度字段本身被损坏，会跳到错误的位置去找校验和，读出一个巨大的长度时还可能越界。所以通常把校验和放在前面，并且让校验和覆盖长度字段，或者像 LevelDB 那样把日志切成固定大小的块，限制损坏的影响范围。

</details>
