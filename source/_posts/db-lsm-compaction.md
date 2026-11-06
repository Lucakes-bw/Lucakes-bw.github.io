---
title: 合并：写放大、读放大与空间放大
date: 2026-11-13 10:40:00
updated: 2026-11-13 10:40:00
categories:
  - 数据库
  - 存储引擎
tags:
  - 数据库
  - 存储引擎
  - LSM
order: 5
description: 文件越积越多，读就越慢，旧版本和墓碑也一直占着空间，所以 LSM 树要不停地合并：把几个文件归并成新的文件，丢掉被覆盖的旧值。本文实现多路归并和分层合并，实测它的代价：随机写入 221 MB 数据，合并又写了 1235 MB，加上日志和落盘，磁盘一共写了 1.72 GB，写放大 7.76；同样的数据按顺序写入，键的范围互不重叠，文件直接平移到下一层，写放大只有 2.18，速度快了 2.6 倍。合并是同步的，200 万次写入中有 18 次被挡住、一共等了 2.11 秒；同一批键覆盖写两遍、三遍，磁盘占用从 239 MB 涨到 314 MB、352 MB。三种放大此消彼长，这是所有存储引擎都逃不开的取舍。
top_group_list: true
disableNunjucks: true
viz: [db-lsm]
---

> **本文目标**：理解合并要解决的问题；会用多路归并合并多个有序文件，处理同一个键的多个版本和墓碑；理解分层合并（leveled）的结构和触发条件；理解写放大、读放大、空间放大的含义并会测量；理解合并造成的写入卡顿；知道真正的引擎在合并上做了哪些优化。
>
> **前置知识**：本章第 1～4 篇；《数据结构》专栏的《归并排序》。
>
> **实验环境**：同第 1 篇。写入 200 万条，键 16 字节、值 100 字节，写入的数据共 221.3 MB；不 `fsync` 日志（`syncEvery` = 0），每键 10 位的布隆过滤器。

## 1. 为什么要合并

只落盘、不合并会怎样？200 万条数据会变成 75 个 L0 文件：

- **读**：每个 L0 文件都可能有任意的键，查一个键要问 75 个文件，就算有布隆过滤器，也有约 0.75 次白读；
- **空间**：同一个键被写了 10 次，10 个版本都留在不同的文件中；删除的键，墓碑和旧值都在；
- **打开的文件**：越来越多。

**合并**（compaction）把几个文件按键归并成新的文件：同一个键只留最新的版本，确定没有更旧版本的墓碑也一起丢掉，然后删除旧文件。

## 2. 多路归并

输入是若干个有序的来源（SSTable 的迭代器），**按从新到旧排列**。每一步从所有来源的当前条目中选出最小的键；同一个键在多个来源中都有时，选下标最小的那个，也就是最新的版本，然后让所有来源都跳过这个键：

```c
for (;;) {
    int best = -1;
    for (int i = 0; i < n; i++) {
        if (!its[i].valid) continue;
        if (best < 0 || keyCmp(its[i].key, its[i].klen, its[best].key, its[best].klen) < 0) best = i;
    }
    if (best < 0) break;
    // its[best] 是最小的键中最新的版本（相同的键，下标小的先被选中）
    if (!(dropTombstones && its[best].vlen == TOMBSTONE)) {
        builderAdd(&b, its[best].key, ...);
        if (builderSize(&b) >= db->opt.tableBytes) { 写完这个文件，开始下一个; }
    }
    for (int i = 0; i < n; i++)  // 所有来源中跳过这个键（旧的版本被丢弃）
        while (its[i].valid && keyCmp(its[i].key, its[i].klen, k, kl) == 0) iterNext(&its[i]);
}
```

输出写满 4 MB（`tableBytes`）就切到下一个文件，所以合并后的一层由很多个 4 MB 的文件组成，**彼此不重叠、按键排列**。墓碑只在「更深的层都是空的」时才丢掉（第 2 篇）。这里每一步都要比较所有的来源，来源少（几个到十几个）时足够了；来源多时用最小堆。

## 3. 分层合并

lsm-kv 用的是**分层合并**（leveled compaction）的一个简化版本：

<figure class="diagram">
<svg viewBox="0 0 700 250" role="img" aria-label="lsm-kv 的分层：L0 的文件由内存表落盘而来，彼此重叠，攒够 4 个就和 L1 合并；L1 上限 20 MB，以下每层乘以 10。随机写入 200 万条后，85% 的数据在 L3">
  <defs><marker id="lvl-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="lvl-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="230.0" y="20" width="240" height="40" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="350.0" y="44.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">L0：3 个文件，9.5 MB（重叠）</text><rect x="210.0" y="74" width="280" height="40" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="350.0" y="98.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">L1：上限 20 MB（这时为空）</text><line x1="350" y1="60" x2="350" y2="72" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lvl-a)"/><rect x="150.0" y="128" width="400" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="350.0" y="152.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">L2：上限 200 MB；7 个文件，25.5 MB</text><line x1="350" y1="114" x2="350" y2="126" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lvl-a)"/><rect x="30" y="182" width="640" height="40" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="350.0" y="206.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">L3：上限 2 GB；51 个文件，203.7 MB</text><line x1="350" y1="168" x2="350" y2="180" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lvl-a)"/><text x="350.0" y="240.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">每次合并都把数据往下一层重写一次：这就是写放大的来源</text>
  </g>
</svg>
</figure>

- L0 的文件达到 4 个，就把 L0 的全部文件和整个 L1 合并，结果放在 L1；
- L1 超过上限（20 MB），就把整个 L1 合并进 L2；L2 的上限是 200 MB，L3 是 2 GB，每往下一层乘以 10。

```c
static void maybeCompact(Lsm* db)
{
    if (db->levels[0].n >= L0_TRIGGER) compactLevel(db, 0);
    uint64_t limit = db->opt.level1Bytes;
    for (int l = 1; l < MAX_LEVEL - 1; l++, limit *= 10)
        if (levelBytes(&db->levels[l]) > limit) compactLevel(db, l);
}
```

每一层是上一层的 10 倍，所以 90% 的数据在最底层，查找时大部分键在最底层才找到（第 4 篇的 L3 占了 85%），但空间上旧版本最多只占 10% 左右。

**简化在哪里**：RocksDB、LevelDB 每次只从上一层挑**一个文件**，和下一层中与它重叠的那几个文件合并，而不是整层合并；lsm-kv 每次把整层重写进下一层，写放大会比真正的分层合并更大。好处是代码短得多，现象也一样明显。

<div class="dsviz" data-viz="lsm-flow"></div>

## 4. 写放大

随机顺序写入 200 万条：

```
2000000 次，3.58 秒，每秒 558590 次
写入的数据 221.3 MB；WAL 244.1 MB，落盘 238.7 MB，合并写出 1234.6 MB（读入 1212.9 MB）
写放大 7.76（WAL + 落盘 + 合并写出，除以写入的数据）；落盘 75 次，合并 25 次，平移 3 次
各层：
  L0：  3 个文件，    9.5 MB，79300 条
  L2：  7 个文件，   25.5 MB，211476 条
  L3： 51 个文件，  203.7 MB，1691798 条
SSTable 一共 238.7 MB；/proc/self/io 统计的写盘 1718.9 MB
```

**写放大**（write amplification）= 实际写到磁盘的字节数 ÷ 程序写入的字节数。程序只写了 221 MB，磁盘却被写了 1.72 GB：

<figure class="diagram">
<svg viewBox="0 0 700 170" role="img" aria-label="200 万条（221 MB）写入时磁盘上写了什么：随机顺序时日志 244 MB、落盘 239 MB、合并 1235 MB，共 1.72 GB，写放大 7.76；顺序写入时合并全部变成平移，只有日志和落盘，共 483 MB，写放大 2.18">
  <defs><marker id="wam-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="wam-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="90.0" y="44.0" text-anchor="end" style="fill:var(--fg);font-size:12px">随机顺序</text><rect x="100.0" y="24.0" width="63.4" height="30.0" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="131.7" y="44.0" text-anchor="middle" style="fill:var(--fg);font-size:10px">WAL 244</text><rect x="163.4" y="24.0" width="62.1" height="30.0" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="194.5" y="44.0" text-anchor="middle" style="fill:var(--fg);font-size:10px">落盘 239</text><rect x="225.6" y="24.0" width="321.1" height="30.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="386.1" y="44.0" text-anchor="middle" style="fill:var(--fg);font-size:10px">合并 1235</text><text x="554.7" y="44.0" text-anchor="start" style="fill:var(--muted);font-size:11px">1718 MB，写放大 7.76</text><text x="90.0" y="100.0" text-anchor="end" style="fill:var(--fg);font-size:12px">顺序</text><rect x="100.0" y="80.0" width="63.4" height="30.0" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="131.7" y="100.0" text-anchor="middle" style="fill:var(--fg);font-size:10px">WAL</text><rect x="163.4" y="80.0" width="62.1" height="30.0" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="194.5" y="100.0" text-anchor="middle" style="fill:var(--fg);font-size:10px">落盘</text><text x="233.6" y="100.0" text-anchor="start" style="fill:var(--muted);font-size:11px">483 MB，写放大 2.18</text><text x="100.0" y="158.0" text-anchor="start" style="fill:var(--muted);font-size:11px">写入的数据 221 MB　　单位：MB</text>
  </g>
</svg>
</figure>

- 日志 244 MB 和落盘 239 MB 各约 1 倍，这是任何 LSM 树都免不了的；
- **合并写出了 1235 MB，占了大头**：同一条数据从 L0 合并到 L1，再到 L2，再到 L3，每经过一次合并就被重写一次；而且下一层的旧数据也被一起重写了。

程序自己统计的 1718.9 MB（WAL + 落盘 + 合并），和内核 `/proc/self/io` 中记录的写盘字节数完全一致，说明统计没有遗漏。

写放大不只是浪费带宽：SSD 的寿命以写入量计算，写放大 8 倍，寿命就缩短 8 倍；云盘按 IOPS、吞吐量限速时，合并会占掉大部分配额。

## 5. 顺序写入：平移

同样的 200 万条，键按顺序递增：

```
2000000 次，1.36 秒，每秒 1474406 次
写入的数据 221.3 MB；WAL 244.1 MB，落盘 238.7 MB，合并写出 0.0 MB（读入 0.0 MB）
写放大 2.18；落盘 75 次，合并 0 次，平移 28 次
```

**一次合并都没做**，写放大只有 2.18（日志加落盘），速度快了 2.6 倍。键是递增的，每个新的 L0 文件的键范围都在已有的所有文件之后，和下一层没有任何重叠，归并的结果就是原来的文件。所以合并之前先检查：如果没有重叠，直接把文件挪到下一层，只改清单，这叫**平移**（trivial move）：

```c
static int trivialMove(Lsm* db, int from)
{
    for (本层的每个文件 a) {
        for (下一层的每个文件 b) if (overlaps(a, b)) return 0;
        for (本层的其他文件 c) if (overlaps(a, c)) return 0;   // L0 的文件之间也可能重叠
    }
    把本层的文件都挪到下一层，按最小键排序;
    writeManifest(db);
    return 1;
}
```

最初的 lsm-kv 没有平移，顺序写入和随机写入的结果**一模一样**（写放大都是 8.91），加上平移以后，顺序写入的合并全部变成了平移，随机写入也在开头几次（下一层还是空的时候）用上了平移，写放大从 8.91 降到了 7.76。

这就是为什么用 LSM 树存时间序列、日志这类**按时间递增写入**的数据特别划算，也是为什么用自增 ID 作为键、往 LSM 树中批量导入数据时，先排好序再写入会快很多。

## 6. 合并会挡住写入

lsm-kv 的合并是**同步**的：触发合并的那一次 `lsmPut` 要等合并完成。随机写入时统计每次写入的耗时：

```
超过 10 ms 的写 18 次，一共 2.11 秒；最慢的一次 457 ms
```

总共 3.58 秒，有 **2.11 秒**花在 18 次被挡住的写入上；最慢的一次等了 457 毫秒，那是一次把 L2 整层合并进 L3 的大合并。第 1 篇的崩溃实验中，两次在 700 毫秒时杀死写入进程，已确认的条数都停在 634465，就是因为两次都碰上了同一次大合并。

真正的引擎用后台线程合并，写入只需要等内存表落盘。但合并跟不上写入时，L0 的文件越积越多，读越来越慢，RocksDB 会先让写入**变慢**（slowdown），再到某个阈值让写入**停止**（stall），等合并追上来。生产中看到 RocksDB 的写入延迟突然出现尖刺，十有八九是这个原因，日志中会有 `Stalling writes` 之类的提示。

## 7. 空间放大

**空间放大** = 磁盘上的数据量 ÷ 有效数据量。把同样的 200 万个键再随机覆盖写一遍、两遍：

```
第一遍后：SSTable 一共 238.7 MB
第二遍后：L0 9.5 MB、L1 12.7 MB、L2 50.9 MB、L3 240.8 MB（2000000 条），一共 313.9 MB
第三遍后：L0 9.5 MB、L2 101.8 MB、L3 240.8 MB（2000000 条），一共 352.1 MB
```

有效数据就是 L3 中的 200 万条（240.8 MB），上面几层中的都是还没被合并掉的旧版本，或者新版本（对应的旧版本在 L3 中）。空间放大从 1.0 涨到了 **1.30**、**1.46**。分层合并中每层是上一层的 10 倍，理论上空间放大约为 1.1；lsm-kv 的上面几层还没有满，合并到 L3 之前，L2 的数据都是重复的，所以更高。

## 8. 三种放大的取舍

<figure class="diagram">
<svg viewBox="0 0 700 260" role="img" aria-label="三种放大的取舍：分层合并偏向读和空间，写放大大；分级合并写放大小，读和空间放大大；B+ 树原地更新，读放大小，随机写入时写放大很大。不可能三者同时最小">
  <defs><marker id="rum-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rum-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <path d="M350,30 L120,220 L580,220 Z" style="fill:none;stroke:var(--border-2);stroke-width:1.5"/><text x="350.0" y="20.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">读放大小</text><text x="120.0" y="242.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">写放大小</text><text x="580.0" y="242.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">空间放大小</text><rect x="300" y="106" width="90" height="28" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="345.0" y="124.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">B+ 树</text><rect x="360" y="151" width="170" height="28" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="445.0" y="169.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">分层合并（RocksDB 默认）</text><rect x="130" y="166" width="170" height="28" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="215.0" y="184.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">分级合并（Cassandra 默认）</text><text x="350.0" y="252.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">越靠近一个角，那一种放大越小</text>
  </g>
</svg>
</figure>

- **写放大**：一份数据被写了几次。合并越积极（每层的比例越小、层数越多），写放大越大；
- **读放大**：一次查找读了几个块。文件越多、层数越多，读放大越大；合并越积极，读放大越小；
- **空间放大**：旧版本占了多少空间。合并越积极，空间放大越小。

三者不可能同时最小。**分层合并**（LevelDB、RocksDB 的默认）偏向读和空间：每层不重叠，查找每层只看一个文件，旧版本少，但写放大大（RocksDB 的经验值是 10～30）。另一种**分级合并**（tiered / universal，Cassandra 的默认）：等同一级攒够几个大小相近的文件再合并成一个更大的，每份数据被重写的次数少，写放大小，但同时存在的文件多，读放大和空间放大都大（合并时可能需要 2 倍的空间）。B+ 树也在这三者之间：原地更新没有旧版本，读放大小，但随机写入时写放大很大（第 6 篇）。

## 9. 常见错误

1. **合并时同一个键保留了旧版本**。来源要按新旧排序，相同的键取最新的一个。
2. **在上面的层就丢掉墓碑**。更深的层还有旧值时，旧值会重新出现。
3. **只看写入的吞吐量，不看写放大**。磁盘的实际写入量可能是数据的十几倍。
4. **忽略合并造成的写入卡顿**。平均延迟很好看，尾延迟有几百毫秒的尖刺。
5. **随机的键批量导入**。能先排序就先排序，写放大可以降低几倍。

## 10. 小结

- 合并用多路归并把多个有序文件合成新的文件，同一个键只留最新的版本，最底层丢掉墓碑；
- 分层合并：L0 到 4 个文件就合并进 L1，每层上限是上一层的 10 倍；lsm-kv 简化为整层合并；
- 随机写入 221 MB，日志、落盘、合并一共写了 1.72 GB，写放大 7.76，其中合并占 1235 MB；
- 顺序写入时键的范围不重叠，平移代替合并，写放大 2.18，速度快 2.6 倍；
- 同步合并让 18 次写入一共等了 2.11 秒，最慢 457 毫秒；
- 覆盖写两遍、三遍，空间放大 1.30、1.46；写、读、空间三种放大此消彼长。

## 11. 练习

1. 一个分层合并的 LSM 树有 4 层（L1～L4），每层是上一层的 10 倍。一条数据从写入到最终落在 L4，大约被写了几次？
2. 为什么说「每层是上一层的 10 倍」时，空间放大约为 1.1？
3. 一个应用每天删除前一天的全部数据（按时间递增的键）。在 LSM 树上，删除以后磁盘空间会立刻释放吗？有什么更好的办法？

<details>
<summary>点击查看答案</summary>

1. 日志 1 次、落盘到 L0 1 次，然后从 L0 到 L1、L1 到 L2、L2 到 L3、L3 到 L4，每次合并都会重写这条数据；而且在真正的分层合并中，上一层的一个文件和下一层约 10 个重叠的文件合并，下一层那 10 份数据也被重写了，所以每下一层的写放大约为 10（最坏），平均也有好几倍。总的写放大可以达到 1 + 1 + 4 × 10 ≈ 42（最坏），实际中常见的是 10～30。

2. 最底层是有效数据的主体，设为 N；上一层最多 N/10，再上一层 N/100……上面所有层加起来最多约 N/9 ≈ 0.11N。最坏情况下上面各层都是最底层中某些键的旧版本或新版本，重复的数据最多约 0.11N，所以空间放大约为 1.11。

3. 不会立刻释放。删除写的是墓碑，墓碑和旧数据要等合并到最底层时才一起丢掉，在那之前空间反而增加了（多了墓碑）。更好的办法：按时间把数据分到不同的「列族」或不同的数据库实例（每天一个），过期时直接删除整个文件或目录；RocksDB 也提供了 `DeleteRange` 和按文件删除（`DeleteFilesInRange`）的接口，Cassandra 有按时间窗口合并的策略（TWCS），整个过期的文件直接丢掉。

</details>
