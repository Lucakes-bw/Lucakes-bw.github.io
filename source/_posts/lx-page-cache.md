---
title: 页缓存与写回
date: 2026-11-09 10:20:00
updated: 2026-11-09 10:20:00
categories:
  - Linux基础
  - 内存管理
tags:
  - Linux
  - 内存
  - 文件IO
order: 3
description: 读写文件时，数据先经过内核的页缓存。在一块限速 100 MB/s 的磁盘上实测：write 256 MB 只用了 0.05 秒，数据还在内存中，fsync 又花了 2.5 秒才写到磁盘；连续写 1.5 GB，前 256 MB 以 5 GB/s 的速度写进缓存，脏页到了上限以后，write 被限速到和磁盘一样的 100 MB/s。读：冷读 102 MB/s，热读 4684 MB/s；O_DIRECT 绕过页缓存，每次都是磁盘的速度；posix_fadvise 可以把文件从缓存中赶出去。
top_group_list: true
disableNunjucks: true
viz: [lx-mem]
---

> **本文目标**：理解读写文件时页缓存的作用；理解脏页、写回和 `fsync`；知道脏页的上限和写入限速；知道 `O_DIRECT`、`posix_fadvise` 的用途；会用 `/proc/meminfo`、`mincore` 观察页缓存。
>
> **前置知识**：本专栏的《文件描述符与系统调用》和《内存映射 mmap》（其中第 1 节介绍了页缓存和 `buff/cache`）；本章前两篇。
>
> **实验环境**：同第 1 篇。文件放在一个 ext4 的数据卷上。这台虚拟机的磁盘是 Mac 上的一个文件，被宿主机缓存，不限速时「冷读」也有 6.8 GB/s，fsync 256 MB 只要 0.07 秒，和真实的磁盘相差太远。所以用 `docker run --device-read-bps /dev/vdb:100mb --device-write-bps /dev/vdb:100mb` 把实验容器的磁盘带宽限制在 100 MB/s，模拟一块普通的云盘。

## 1. 读写都经过页缓存

《内存映射 mmap》一文看到过：读一遍 320 MB 的文件，`buff/cache` 就增加了 321 MB。内核把读过的文件页留在内存中，这就是**页缓存**（page cache）。写也一样：

- `read`：要读的页在页缓存中，直接复制给程序；不在，就从磁盘读进页缓存，再复制；
- `write`：把数据复制进页缓存中对应的页，把这些页标记为**脏页**（dirty），然后立即返回。内核在之后的某个时候把脏页写到磁盘（**写回**，writeback）。

<figure class="diagram">
<svg viewBox="0 0 700 210" role="img" aria-label="读写文件时的页缓存：write 把数据复制进页缓存、标记为脏页后立即返回，内核稍后写回磁盘；fsync 等待写回完成；read 先查页缓存，没有才读磁盘；O_DIRECT 绕过页缓存">
  <defs><marker id="pfl-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="pfl-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="80" width="120" height="44" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="80.0" y="106.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">程序</text><rect x="270" y="60" width="180" height="84" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="360.0" y="98.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">页缓存</text><text x="360.0" y="114.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">干净页 / 脏页</text><rect x="560" y="80" width="120" height="44" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="620.0" y="106.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">磁盘</text><line x1="140" y1="90" x2="268" y2="82" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#pfl-a)"/><text x="200.0" y="74.0" text-anchor="middle" style="fill:var(--link);font-size:11px">write：复制、标记脏页</text><line x1="268" y1="118" x2="140" y2="114" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#pfl-a)"/><text x="200.0" y="136.0" text-anchor="middle" style="fill:var(--link);font-size:11px">read：命中直接返回</text><line x1="450" y1="90" x2="558" y2="92" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#pfl-a)"/><text x="505.0" y="82.0" text-anchor="middle" style="fill:var(--link);font-size:11px">写回</text><line x1="558" y1="112" x2="452" y2="118" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#pfl-a)"/><text x="505.0" y="134.0" text-anchor="middle" style="fill:var(--link);font-size:11px">未命中时读入</text><path d="M80,124 Q80,190 360,190 Q620,190 620,126" style="fill:none;stroke:var(--accent);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#pfl-a)"/><text x="360.0" y="204.0" text-anchor="middle" style="fill:var(--accent);font-size:11px">O_DIRECT：绕过页缓存，直接和磁盘交换数据</text><text x="470.0" y="30.0" text-anchor="start" style="fill:var(--muted);font-size:11px">写回：后台线程，或者 fsync</text>
  </g>
</svg>
</figure>

所以 `write` 返回时，数据通常还**不在磁盘上**。

## 2. 写：write、脏页和 fsync

### 2.1 write 只写进了内存

写一个 256 MB 的文件，再 `fsync`：

```c
for (long done = 0; done < mb * MB; done += CHUNK) CHECK(write(fd, buf, CHUNK) == CHUNK);
/* ... */
CHECK(fsync(fd) == 0);
```

```
写之前：Dirty 0.0 MB
write 256 MB：0.049 秒（5226 MB/s），之后 Dirty 159.9 MB
fsync：2.547 秒，之后 Dirty 0.0 MB
```

- **`write` 256 MB 只用了 0.049 秒**，5 GB/s，远远超过磁盘的 100 MB/s。它只是复制进了内存；
- 写完以后 `/proc/meminfo` 中的 `Dirty` 是 160 MB：还有 160 MB 的数据只在内存中（另外的部分已经被后台线程开始写回了，见下一节）；
- **`fsync` 花了 2.5 秒**，正好是 256 MB 以 100 MB/s 写到磁盘的时间。`fsync` 返回后 `Dirty` 归零。

如果在 `write` 之后、写回之前机器断电，这些数据就丢了。数据库在提交事务时要 `fsync` 日志（《数据库》专栏的《日志与崩溃恢复》），Redis 的 AOF 有 `appendfsync` 配置，都是这个原因；也解释了为什么 `fsync` 那么慢：它要等真正的磁盘写入。

### 2.2 谁在什么时候写回

内核的写回线程在这些情况下把脏页写到磁盘：

- 脏页存在的时间超过 `dirty_expire_centisecs`（默认 3000，即 30 秒）；
- 脏页的总量超过 `dirty_background_ratio`（默认可用内存的 10%），开始在后台写回；
- 程序调用 `fsync`、`fdatasync`、`sync`，或者 `msync`（mmap 的文件）。

本文环境中的两个阈值（单位是页）：

```
nr_dirty_threshold 87442              ← 约 342 MB
nr_dirty_background_threshold 43667   ← 约 171 MB
```

### 2.3 脏页太多时，write 被限速

如果程序写得比磁盘快，脏页会不断增加。超过 `dirty_ratio`（默认 20%）对应的上限后，**内核让写入的进程自己等待**，直到脏页降下来。连续写 1.5 GB，每 128 MB 报告一次：

```
已写   128 MB：这 128 MB 用了  0.025 秒（ 5065 MB/s），Dirty  127.9 MB，Writeback   0.0 MB
已写   256 MB：这 128 MB 用了  0.022 秒（ 5712 MB/s），Dirty  159.9 MB，Writeback  88.0 MB
已写   384 MB：这 128 MB 用了  0.960 秒（  133 MB/s），Dirty  144.0 MB，Writeback 144.0 MB
已写   512 MB：这 128 MB 用了  1.171 秒（  109 MB/s），Dirty  127.9 MB，Writeback 168.0 MB
已写   640 MB：这 128 MB 用了  1.205 秒（  106 MB/s），Dirty  159.9 MB，Writeback 144.0 MB
……
已写  1536 MB：这 128 MB 用了  1.272 秒（  101 MB/s），Dirty  139.3 MB，Writeback 172.7 MB
一共 12.33 秒
```

<figure class="diagram">
<svg viewBox="0 0 700 230" role="img" aria-label="连续写 1.5 GB 时每 128 MB 的写入速度：前两段约 5000 MB/s（只写进页缓存），之后脏页到了上限，稳定在 100 MB/s 左右，和限速后的磁盘一样">
  <defs><marker id="pth-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="pth-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <line x1="70" y1="190" x2="680" y2="190" style="stroke:var(--border-2);stroke-width:1.2"/><rect x="80" y="42.9" width="36" height="147.1" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="98.0" y="37.9" text-anchor="middle" style="fill:var(--muted);font-size:10px">5065</text><text x="98.0" y="204.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">128</text><rect x="130" y="40.8" width="36" height="149.2" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="148.0" y="35.8" text-anchor="middle" style="fill:var(--muted);font-size:10px">5712</text><text x="148.0" y="204.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">256</text><rect x="180" y="105.7" width="36" height="84.3" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="198.0" y="100.7" text-anchor="middle" style="fill:var(--muted);font-size:10px">133</text><text x="198.0" y="204.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">384</text><rect x="230" y="109.1" width="36" height="80.9" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="248.0" y="104.1" text-anchor="middle" style="fill:var(--muted);font-size:10px">109</text><text x="248.0" y="204.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">512</text><rect x="280" y="109.6" width="36" height="80.4" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="298.0" y="104.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">106</text><text x="298.0" y="204.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">640</text><rect x="330" y="110.3" width="36" height="79.7" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="348.0" y="105.3" text-anchor="middle" style="fill:var(--muted);font-size:10px">102</text><text x="348.0" y="204.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">768</text><rect x="380" y="110.8" width="36" height="79.2" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="398.0" y="105.8" text-anchor="middle" style="fill:var(--muted);font-size:10px">99</text><text x="398.0" y="204.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">896</text><rect x="430" y="110.4" width="36" height="79.6" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="448.0" y="105.4" text-anchor="middle" style="fill:var(--muted);font-size:10px">101</text><text x="448.0" y="204.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">1024</text><rect x="480" y="110.6" width="36" height="79.4" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="498.0" y="105.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">100</text><text x="498.0" y="204.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">1152</text><rect x="530" y="110.8" width="36" height="79.2" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="548.0" y="105.8" text-anchor="middle" style="fill:var(--muted);font-size:10px">99</text><text x="548.0" y="204.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">1280</text><rect x="580" y="110.6" width="36" height="79.4" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="598.0" y="105.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">100</text><text x="598.0" y="204.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">1408</text><rect x="630" y="110.4" width="36" height="79.6" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="648.0" y="105.4" text-anchor="middle" style="fill:var(--muted);font-size:10px">101</text><text x="648.0" y="204.0" text-anchor="middle" style="fill:var(--muted);font-size:9px">1536</text><text x="14.0" y="30.0" text-anchor="start" style="fill:var(--muted);font-size:10px">MB/s</text><text x="14.0" y="44.0" text-anchor="start" style="fill:var(--muted);font-size:10px">（对数）</text><text x="380.0" y="224.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">已写入（MB）；脏页上限约 342 MB，磁盘限速 100 MB/s</text>
  </g>
</svg>
</figure>

- **前 256 MB 以 5 GB/s 的速度写进页缓存**；超过后台阈值（约 171 MB）后，内核开始写回（`Writeback` 是正在写往磁盘的页）；
- 脏页（`Dirty` + `Writeback`）接近上限（约 342 MB）以后，**write 的速度降到了 100 MB/s 左右，和磁盘一样**，并且一直保持；
- 页缓存只能吸收一段突发的写入，持续写入的速度最终由磁盘决定。

<div class="dsviz" data-viz="lx-dirty"></div>

这个限速有时会造成意外：一个日志程序平时 `write` 很快，某一刻另一个程序在同一个磁盘上大量写入，脏页到了上限，日志程序的 `write` 也被拖慢了几百毫秒。对延迟敏感的服务，可以调低 `dirty_background_ratio`（更早地开始写回，脏页不至于积累太多），或者用 `dirty_background_bytes`、`dirty_bytes` 按字节设置。

## 3. 读：冷读与热读

先清空页缓存（需要 root），用 `mincore` 查看文件有多少页在页缓存中，再读两遍：

```bash
sync; echo 3 > /proc/sys/vm/drop_caches
```

```c
void* p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_SHARED, fd, 0);
CHECK(mincore(p, (size_t)st.st_size, vec) == 0);   // 每一页是否在内存中
```

```
页缓存中有 0 / 65536 页（0%）
读 256 MB：2.506 秒（102 MB/s）
页缓存中有 65536 / 65536 页（100%）
读 256 MB：0.055 秒（4684 MB/s）
```

第一次读（**冷读**）是磁盘的速度，102 MB/s；读完以后整个文件都在页缓存中，第二次读（**热读**）4684 MB/s，快了 46 倍。

`echo 3 > /proc/sys/vm/drop_caches` 只丢弃干净的页缓存（所以先 `sync`），它是测试用的，生产环境中没有理由执行：页缓存是可以随时回收的内存，内存紧张时内核会自动回收最近没用过的页，`free` 命令中 `available` 一列已经把它算作可用的内存。

## 4. 绕过和控制页缓存

### 4.1 O_DIRECT

打开文件时加上 `O_DIRECT`，读写直接在程序的缓冲区和磁盘之间进行，不经过页缓存：

```c
void* p;
posix_memalign(&p, 4096, CHUNK);                  // O_DIRECT 要求缓冲区按块大小对齐
int fd = open(path, O_RDONLY | O_DIRECT);
```

```
O_DIRECT 读 256 MB：2.529 秒（101 MB/s）
```

这次读之前，整个文件都在页缓存中（上一节刚读过），`O_DIRECT` 却还是用了 2.5 秒：它不使用页缓存，每次都从磁盘读。

为什么要绕过页缓存？数据库（MySQL InnoDB 的 `innodb_flush_method = O_DIRECT`）有自己的缓冲池，知道哪些页更重要。如果再经过页缓存，同样的数据在内存中存了两份，而且内核的回收策略和数据库的缓冲池会互相干扰。代价是：缓冲区、偏移、长度都必须对齐；所有的缓存、预读都要程序自己做；用错了就是每次都读磁盘。

### 4.2 posix_fadvise

不想绕过页缓存，只是想告诉内核「这个文件读完就不需要了」：

```c
posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);   // 告诉内核：这个文件的缓存不再需要
```

```
posix_fadvise(POSIX_FADV_DONTNEED) 完成
页缓存中有 0 / 65536 页（0%）
读 256 MB：2.460 秒（104 MB/s）
```

文件立即从页缓存中消失了，再读又是磁盘的速度。备份程序、日志归档这类「读一遍大文件就不再看」的程序应该这样做，否则一次全量备份会把页缓存中真正有用的热数据全部挤出去（和《数据库》专栏中 LRU 的「缓存污染」是一回事）。`POSIX_FADV_SEQUENTIAL`、`POSIX_FADV_WILLNEED` 则告诉内核要顺序读、即将要读，让它加大预读。

## 5. 常见错误

1. **以为 `write` 返回就写到了磁盘**。只写进了页缓存，断电会丢。需要持久化时 `fsync`。
2. **每写一条记录就 `fsync` 一次**。每次都要等磁盘，吞吐量受限于磁盘的每秒写入次数。批量写入后一起 `fsync`（数据库的组提交就是这样做的）。
3. **只 `fsync` 文件，不 `fsync` 目录**。新建或重命名的文件，目录项的修改也要 `fsync` 目录才能持久化。
4. **看到 `free` 中 `free` 一列很小就以为内存不够了**，甚至定时 `drop_caches`。页缓存随时可以回收，看 `available`。
5. **用缓冲 IO 测磁盘的速度**，测到的是内存的速度。用 `O_DIRECT`、`fio --direct=1`，或者先清空页缓存。
6. **备份、归档程序不处理页缓存**，把热数据挤了出去，备份期间线上服务变慢。

## 6. 小结

- 读写文件都经过页缓存；`write` 只是把数据复制进页缓存、标记为脏页，由内核稍后写回；
- 实测 100 MB/s 的磁盘：`write` 256 MB 只要 0.05 秒，`fsync` 要 2.5 秒；持续写入时，脏页到了上限（约 342 MB），`write` 被限速到磁盘的 100 MB/s；
- 冷读 102 MB/s，热读 4684 MB/s；`O_DIRECT` 绕过页缓存，每次都是磁盘的速度；`posix_fadvise(DONTNEED)` 把文件赶出页缓存；
- 页缓存是可以回收的内存，`available` 已经把它算进去了。

## 7. 练习

1. 一个程序把 10 GB 的数据写进文件，`write` 全部返回以后打印「完成」，用户随即拔掉了 U 盘。会发生什么？怎样修改程序？
2. 为什么 InnoDB 用 `O_DIRECT` 读写数据文件，却对 redo log 使用普通的缓冲 IO 加 `fsync`？
3. 一台 64 GB 内存的机器，`dirty_ratio` 是 20%。一个程序写得比磁盘快，可能出现什么问题？

<details>
<summary>点击查看答案</summary>

1. `write` 返回只说明数据进了页缓存，可能还有几百 MB 甚至更多的脏页没有写到 U 盘上，拔掉以后这些数据丢失，文件损坏。程序应该在写完以后调用 `fsync`（并且检查它的返回值），`fsync` 成功后才提示完成；用户也应该先「弹出」（卸载时内核会写回所有的脏页）。

2. 数据文件由 InnoDB 的缓冲池管理，用 `O_DIRECT` 避免数据在页缓存中再存一份、避免双重缓存的干扰。redo log 是顺序追加的小块写入，写完立即需要持久化，读得很少；对它使用缓冲 IO，多个事务的日志可以先写进页缓存、再用一次 `fsync` 一起落盘（组提交），比每次都直接写磁盘更高效。

3. 20% 是约 12.8 GB 的脏页上限。在被限速之前，可能积累十几 GB 的脏页；之后写回这些数据要很长时间（比如磁盘 200 MB/s，要一分多钟），期间任何对这块磁盘的写入和 `fsync` 都可能被严重拖慢（`fsync` 要等前面排队的大量写回），服务出现长时间的停顿；断电时丢失的数据也更多。大内存的机器通常用 `dirty_background_bytes`、`dirty_bytes` 设置一个固定的、较小的上限（比如几百 MB）。

</details>
