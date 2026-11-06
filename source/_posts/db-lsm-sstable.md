---
title: SSTable：有序的只读文件
date: 2026-11-13 10:20:00
updated: 2026-11-13 10:20:00
categories:
  - 数据库
  - 存储引擎
tags:
  - 数据库
  - 存储引擎
  - LSM
order: 3
description: 内存表落盘以后变成 SSTable（Sorted String Table）：一个按键排好序、写完就再也不改的文件。本文设计 lsm-kv 的 SSTable 格式：4 KB 的数据块、记录每个块最后一个键的索引块、布隆过滤器、固定 40 字节的文件尾，再用 od 和一个小脚本把一个真实的 4 MB 文件拆开来看：1026 个数据块、33826 条数据，索引和布隆过滤器只占 1.8%，打开文件时把它们读进内存，查找一个键时二分找到数据块，只需要读一次磁盘。实测在 200 万条数据中随机查存在的键，平均每次读 1.02 个数据块。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 SSTable 的结构：数据块、索引块、过滤器、文件尾；会写一个 SSTable 的构建器（按顺序追加条目、切分数据块）；理解为什么索引常驻内存、查找只读一个数据块；理解「不可修改」带来的好处；知道 L0 和下面各层的区别。
>
> **前置知识**：本章第 1、2 篇；本专栏的《B+ 树索引》《InnoDB 的存储结构》（页和 B+ 树，作为对比）。
>
> **实验环境**：同第 1 篇。

## 1. 文件的结构

SSTable 是按键排好序的条目，一次性顺序写出，写完就**再也不修改**。lsm-kv 的格式：

<figure class="diagram">
<svg viewBox="0 0 700 280" role="img" aria-label="一个 4 MB 的 SSTable 的结构（实测文件）：1026 个约 4 KB 的数据块，共 4194424 字节；索引块 32852 字节，每个数据块一项；布隆过滤器 42291 字节；最后是固定的 40 字节文件尾。索引和过滤器只占 1.8%，打开文件时读进内存">
  <defs><marker id="lay-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="lay-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="20" width="400" height="44" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="220.0" y="38.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">数据块 0</text><text x="220.0" y="53.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">[键长][值长][键][值] …… 约 33 条</text><rect x="20" y="70" width="400" height="44" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="220.0" y="96.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">数据块 1</text><text x="220.0" y="132.0" text-anchor="middle" style="fill:var(--fg);font-size:14px">⋮</text><rect x="20" y="140" width="400" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="220.0" y="162.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">数据块 1025</text><rect x="20" y="182" width="400" height="30" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="220.0" y="201.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">索引块：最小键；每块的最后一个键、偏移、大小</text><rect x="20" y="216" width="400" height="26" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="220.0" y="233.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">布隆过滤器：每个键 10 位</text><rect x="20" y="246" width="400" height="24" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="220.0" y="262.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">文件尾 40 字节：索引、过滤器的位置，条目数、魔数</text><text x="440.0" y="98.0" text-anchor="start" style="fill:var(--fg);font-size:12px">4,194,424 字节（98.2%）</text><text x="440.0" y="116.0" text-anchor="start" style="fill:var(--muted);font-size:11px">留在磁盘上，查找时读一块</text><text x="440.0" y="202.0" text-anchor="start" style="fill:var(--fg);font-size:12px">32,852 字节</text><text x="440.0" y="234.0" text-anchor="start" style="fill:var(--fg);font-size:12px">42,291 字节</text><text x="440.0" y="262.0" text-anchor="start" style="fill:var(--muted);font-size:11px">打开文件时先读它</text><text x="560.0" y="216.0" text-anchor="start" style="fill:var(--accent);font-size:12px">常驻内存</text>
  </g>
</svg>
</figure>

```
数据块 × N：条目 [键长 u32][值长 u32][键][值]，每块约 4 KB，块内按键有序
索引块：     [最小键的长度 u32][最小键]，然后每个数据块一项：[最后一个键的长度 u32][最后一个键][偏移 u64][大小 u32]
布隆过滤器： [位数 u32][哈希函数个数 u32][位数组]
文件尾：     固定 40 字节：[索引偏移 u64][索引大小 u32][过滤器偏移 u64][过滤器大小 u32][条目数 u64][块数 u32][魔数 u32]
```

- **数据块**：条目一个接一个地放，凑够约 4 KB 就开始下一块。4 KB 是一次读盘的单位，和页缓存、文件系统的块大小一致；
- **索引块**：每个数据块一项，记下这一块**最后一个键**和块在文件中的位置。查找键 k 时，二分找到第一个「最后一个键 ≥ k」的块，k 如果存在，只可能在这一块中；
- **布隆过滤器**：第 4 篇；
- **文件尾**：固定在文件的最后 40 字节，打开文件时先读它，就知道索引和过滤器在哪里。魔数 `0x4C534D31`（"LSM1"）用来确认这是一个完整的 SSTable，写到一半的文件没有正确的文件尾。

把元数据放在文件末尾，是因为写数据块的时候还不知道一共有多少块、索引有多大；全部写完以后再写索引和文件尾，整个文件就可以**一次顺序写出**，不必回头修改前面的内容。

## 2. 构建器

写 SSTable 的代码是一个「构建器」，按键的顺序逐条加入：

```c
static void builderAdd(Builder* b, const char* k, uint32_t kl, const char* v, uint32_t vl)
{
    uint32_t vbytes = vl == TOMBSTONE ? 0 : vl;
    if (b->blockLen && b->blockLen + 8 + kl + vbytes > BLOCK_SIZE) flushBlock(b);  // 这一块满了
    put32(b->block + b->blockLen, kl);
    put32(b->block + b->blockLen + 4, vl);
    memcpy(b->block + b->blockLen + 8, k, kl);
    if (vbytes) memcpy(b->block + b->blockLen + 8 + kl, v, vbytes);
    b->blockLen += 8 + kl + vbytes;
    // 记下这一块的最后一个键（写索引用），收集键（建布隆过滤器用）
    ...
}

static void flushBlock(Builder* b)
{
    writeAll(b->fd, b->block, b->blockLen);              // 写出数据块
    indexAppend(b, 最后一个键, 偏移, 大小);                // 在内存中记下索引项
    b->off += b->blockLen;
    b->blockLen = 0;
    b->nblocks++;
}
```

`builderFinish` 写出最后一块、索引、布隆过滤器和文件尾，`fsync`，然后重新打开它，得到一个可以查找的 `Table`。落盘（遍历内存表）和合并（归并多个 SSTable）都用这个构建器，只是输入不同。

## 3. 拆开一个真实的文件

200 万条随机数据写完以后，取一个合并产生的 SSTable（4269607 字节）。最后 40 字节：

```
$ tail -c 40 001370.sst | od -A d -t u4 -w40
      4194424          0      32852    4227276          0      42291      33826          0       1026 1280527665
```

按格式解析：

```
数据块 1026 个，数据区 4194424 字节；索引 32852 字节；布隆过滤器 42291 字节；条目 33826；魔数 4c534d31
```

开头的 160 字节：

```
0000000 020  \0  \0  \0   d  \0  \0  \0   0   0   0   0   0   2   c   9
0000016   c   3   b   0   6   7   c   5   0   0   0   0   0   2   c   9
0000032   c   3   b   0   6   7   c   5   v   v   v   v   v   v   v   v
...
0000112   v   v   v   v   v   v   v   v   v   v   v   v 020  \0  \0  \0
```

`020`（八进制，即 16）是键长，`d`（100）是值长，接着是 16 字节的键 `000002c9c3b067c5` 和 100 字节的值（测试程序把键复制到值的开头，后面填满 `v`）。第 128 字节开始是下一条。

几个数字：

- 33826 条 × 124 字节 ≈ 4.19 MB，正好是数据区的大小；每个块平均 33 条；
- 索引 32852 字节，1026 项，每项约 32 字节（16 字节的键 + 4 + 8 + 4）；
- 布隆过滤器 42291 字节，每个键 10 位；
- **索引和过滤器一共 75 KB，只占文件的 1.8%**。

## 4. 查找：只读一个数据块

打开 SSTable 时，读出文件尾、索引和布隆过滤器，**常驻内存**；数据块留在磁盘上（以及页缓存中）。查找一个键：

```c
static int tableGet(Lsm* db, Table* t, const char* k, uint32_t kl, char** val, uint32_t* vlen)
{
    if (k < 最小键 || k > 最大键) return 0;                     // 不在这个文件的范围内
    if (t->bloom && !bloomMayContain(...)) return 0;            // 布隆过滤器说一定没有（第 4 篇）
    // 二分：第一个「最后一个键 >= k」的块
    uint32_t lo = 0, hi = t->nblocks;
    while (lo < hi) { ... }
    // 读出这一块，顺序找
    pread(t->fd, buf, bi->size, bi->off);
    db->st.blockReads++;
    ...
}
```

<figure class="diagram">
<svg viewBox="0 0 700 220" role="img" aria-label="在 SSTable 中查找键 k：内存中的索引是每块最后一个键组成的有序数组，二分找到第一个「最后一个键 ≥ k」的块，只读这一块，在块中顺序查找">
  <defs><marker id="lkp-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="lkp-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="20.0" y="22.0" text-anchor="start" style="fill:var(--muted);font-size:11px">内存中的索引（每块的最后一个键）</text><rect x="20" y="32" width="86" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="63.0" y="52.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0042…</text><rect x="112" y="32" width="86" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="155.0" y="52.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0091…</text><rect x="204" y="32" width="86" height="32" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="247.0" y="52.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">00d7…</text><rect x="296" y="32" width="86" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="339.0" y="52.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0130…</text><rect x="388" y="32" width="86" height="32" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="431.0" y="52.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">⋯</text><rect x="480" y="32" width="86" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="523.0" y="52.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">ffa2…</text><rect x="572" y="32" width="86" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="615.0" y="52.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">ffff…</text><text x="20.0" y="92.0" text-anchor="start" style="fill:var(--fg);font-size:12px">查 k = 00a3…：二分，0091… &lt; k ≤ 00d7…，所以只可能在第 2 块</text><line x1="223" y1="66" x2="223" y2="130" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lkp-a)"/><rect x="110" y="132" width="340" height="40" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="280.0" y="148.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">磁盘上的第 2 块（pread 一次）</text><text x="280.0" y="163.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">00920… 0099a… 00a3… …… 00d7…</text><text x="20.0" y="200.0" text-anchor="start" style="fill:var(--muted);font-size:11px">实测：200 万条中随机查存在的键，平均每次读 1.02 个数据块</text>
  </g>
</svg>
</figure>

内存中的二分查找不需要读盘，只有最后读数据块的那一次 `pread` 需要。在 200 万条数据中随机查 20 万个存在的键：

```
200000 次查找，找到 200000 个，0.17 秒，每秒 1204555 次
每次查找平均读 1.02 个数据块
```

平均每次 1.02 次读：绝大多数查找只在一个文件中读了一个块，多出来的 0.02 是布隆过滤器的误判（第 4 篇）。数据量是 240 MB，比内存小，读都命中了页缓存，所以每秒能查 120 万次；数据比内存大时，每次读块就是一次真正的磁盘读取，**读块的次数**才是决定性能的指标。

整个数据库 200 万条、约 60 个文件，常驻内存的索引和过滤器一共约 4.5 MB。数据再大一些（比如 1 亿条），索引和过滤器也有几百 MB，RocksDB 因此提供了「把索引和过滤器也分块、放进块缓存」的选项。

和 InnoDB 对比：B+ 树查找要从根走到叶子，3～4 层，上面几层通常在缓冲池中，最后读一个叶子页；SSTable 把「上面几层」压缩成了一个常驻内存的有序数组，因为文件不会再变，不需要树形结构来支持插入。

## 5. 不可修改的好处

SSTable 写完就不再改，这让很多事情变得简单：

- **不需要页内的空闲空间**：B+ 树的页要留空间给以后的插入，随机插入时页的填充率只有 50%～70%（第 6 篇实测 InnoDB 的表文件是数据的 2 倍）；SSTable 每个块都写得满满的；
- **可以压缩**：每个块可以整块压缩（RocksDB 常用 LZ4、ZSTD），B+ 树的页要原地修改，压缩起来麻烦得多；
- **并发简单**：读文件不需要加锁，没有人会同时修改它；
- **缓存简单**：缓存的块永远不会过期，只会随文件被删除而失效；
- **备份、复制简单**：已经存在的文件不会变，增量备份只需要复制新文件（RocksDB 的 checkpoint 就是给所有文件做硬链接）。

代价是：修改和删除只能写新的版本，旧版本要靠合并才能清理（第 5 篇）。

## 6. L0 和下面的层

lsm-kv 的文件分成多层。**L0** 的文件直接由内存表落盘而来，每个文件都覆盖了那段时间写入的所有键，**彼此的键范围是重叠的**：查一个键，L0 的每个文件都可能有，要从新到旧逐个查。

L1 及以下各层的文件由合并产生，**同一层中的文件互不重叠、按键排列**：查一个键，每一层只需要二分找到唯一可能包含它的那一个文件：

```c
for (int i = db->levels[0].n - 1; i >= 0; i--) {   // L0：从新到旧，每个文件都要查
    int r = tableGet(db, db->levels[0].t[i], key, klen, val, vlen);
    if (r) return r > 0;
}
for (int l = 1; l < MAX_LEVEL; l++) {               // 其余各层：二分找到唯一可能的文件
    ...
}
```

所以 L0 的文件不能太多（lsm-kv 到 4 个就合并），否则每次查找都要查很多个文件。

## 7. 常见错误

1. **把索引放在文件开头**。写数据之前不知道索引的内容，只能先预留空间或者写完再回头改。
2. **不检查文件尾的魔数**。写到一半的文件会被当成正常的 SSTable 解析，读出乱码。
3. **查找时逐块顺序扫描**。块的「最后一个键」是有序的，应该二分。
4. **数据块太大**。每次查找都要读一整块，块越大，读一个键浪费的字节越多；块太小，索引又会变大。常见的是 4～64 KB。
5. **以为 SSTable 的查找和 B+ 树一样只查一个地方**。L0 的文件互相重叠，各层都可能有这个键，要靠布隆过滤器减少无用的读取。

## 8. 小结

- SSTable 是按键有序、写完不再修改的文件：数据块、索引块、布隆过滤器、固定长度的文件尾，元数据放在最后，整个文件一次顺序写出；
- 实测一个 4 MB 的文件：1026 个数据块、33826 条，索引和过滤器只占 1.8%，常驻内存；
- 查找：二分索引找到唯一的数据块，只读一次；200 万条中随机查存在的键，平均读 1.02 个块；
- 不可修改让压缩、并发、缓存、备份都变得简单，代价是需要合并；
- L0 的文件互相重叠，要逐个查；下面各层不重叠，每层只查一个文件。

## 9. 练习

1. 一个 SSTable 有 1000 个数据块，索引在内存中。查找一个不在这个文件范围内的键，需要读几次磁盘？在范围内但不存在的键呢（没有布隆过滤器）？
2. 如果把数据块的大小从 4 KB 改成 64 KB，对索引的大小和一次查找读取的字节数有什么影响？
3. SSTable 支持范围查询（比如「所有以 user:100 开头的键」）吗？怎么做？

<details>
<summary>点击查看答案</summary>

1. 不在范围内（比最小键小或比最大键大）：0 次，用内存中的最小、最大键就能判断。在范围内但不存在：二分找到可能的块，读 1 次，在块中没找到。所以没有布隆过滤器时，查一个不存在的键，每个范围覆盖它的文件都要读一次，这就是第 4 篇要解决的问题。

2. 块数变为原来的 1/16，索引也约为原来的 1/16，占的内存更少；但每次查找要读出 64 KB 的块，读取的字节数是原来的 16 倍，块内的顺序查找也更慢。如果查找都是随机的点查，小块更好；如果多是顺序扫描（范围查询），大块更好，而且压缩率更高。

3. 支持。在每个文件中二分找到第一个不小于起始键的块，然后顺序往后读，直到超过结束键。因为一个键可能在多层都有版本，范围查询要像合并一样，把内存表和所有相关的 SSTable 的迭代器做多路归并，同一个键取最新的版本、跳过墓碑。这比 B+ 树的范围扫描要复杂和昂贵一些。

</details>
