---
title: 布隆过滤器：先问一句「可能有吗」
date: 2026-11-13 10:30:00
updated: 2026-11-13 10:30:00
categories:
  - 数据库
  - 存储引擎
tags:
  - 数据库
  - 存储引擎
  - LSM
order: 4
description: LSM 树的数据分散在好几层中，查一个不存在的键，每一层都要读一个数据块才能确定「没有」。布隆过滤器用每个键 10 位的内存回答「这个文件里一定没有」或「可能有」。实测在 200 万条数据中查不存在的键：没有过滤器时每次读 3.00 个数据块，有了过滤器只读 0.03 个，速度从每秒 53 万次提高到 663 万次；查存在的键从 2.80 个块降到 1.02 个，正好等于按每层的数据比例算出来的期望。再测假阳性率和每键位数的关系：10 位时实测 1.30%，比理论值 0.84% 高，原因是 k 个哈希函数都是从同一个 32 位哈希值算出来的。
top_group_list: true
disableNunjucks: true
viz: [db-lsm]
---

> **本文目标**：理解为什么 LSM 树特别需要布隆过滤器；理解布隆过滤器的原理：位数组、多个哈希函数、只有假阳性没有假阴性；会计算最优的哈希函数个数和假阳性率；会用双重哈希从一个哈希值得到多个；会用「平均读几个数据块」衡量读放大。
>
> **前置知识**：本章第 3 篇；《数据结构》专栏的《哈希表》。
>
> **实验环境**：同第 1 篇。数据是第 5 篇中随机写入的 200 万条，读测试打开数据库时 L0 已经合并下去了，有 L1、L2、L3 三层。

## 1. 不存在的键最贵

查一个键，要从新到旧查内存表和每一层，**在某一层找到就停**。如果这个键**不存在**，就要查完所有的层，每一层都要读一个数据块，才能确定「这里没有」。

读测试时的数据分布：

```
L1：  3 个文件，   11.5 MB，96726 条
L2：  7 个文件，   25.2 MB，211476 条
L3： 51 个文件，  201.6 MB，1691798 条
```

查一个不存在的键，三层各读一个块，一共 3 个；查一个存在的键，平均要读几个块？它在 L1 的概率是 4.8%（读 1 个块），在 L2 的概率是 10.6%（读 2 个），在 L3 的概率是 84.6%（读 3 个），期望是 0.048 × 1 + 0.106 × 2 + 0.846 × 3 ≈ **2.80**。

关闭布隆过滤器（`bloomBitsPerKey` = 0）写入同样的数据，随机查 20 万次：

```
查存在的键：  每秒 546937 次；每次查找平均读 2.80 个数据块
查不存在的键：每秒 534067 次；每次查找平均读 3.00 个数据块
```

和计算的完全一致。大部分数据在最底层，所以就连存在的键，平均也要读将近 3 个块，其中 1.8 个是在上面的层中白读的。这就是 LSM 树的**读放大**。而实际中「查一个不存在的键」非常常见：插入之前检查键是否已经存在、缓存未命中时查数据库、去重……

## 2. 布隆过滤器

布隆过滤器是一个 m 位的位数组和 k 个哈希函数：

- **加入**一个键：算出 k 个哈希值，把对应的 k 位置为 1；
- **查询**一个键：算出同样的 k 位，**只要有一位是 0，这个键一定没有加入过**；如果全是 1，它**可能**加入过（也可能是别的键恰好把这些位都置成了 1）。

<div class="dsviz" data-viz="lsm-bloom"></div>

它只会**误报**（假阳性：说可能有，其实没有），不会**漏报**（假阴性：说没有，其实有）。这正是查找需要的：过滤器说没有，就跳过这个文件，不读盘；说可能有，再去读数据块确认。误报的代价只是一次白读。

每个 SSTable 在写出时为它的所有键建一个过滤器，存在文件中（第 3 篇的格式），打开文件时读进内存。查找时先问它：

```c
if (t->bloom) {
    db->st.bloomChecks++;
    if (!bloomMayContain(t->bloom, t->bloomBits, t->bloomK, k, kl)) {
        db->st.bloomNegatives++;
        return 0;                 // 一定没有，不读盘
    }
}
```

## 3. 效果

同样的 200 万条数据，每个键 10 位：

| | 没有过滤器 | 每键 10 位 |
|---|---|---|
| 查不存在的键：每次读的块 | 3.00 | **0.03** |
| 查不存在的键：每秒 | 534,067 | **6,634,152** |
| 查存在的键：每次读的块 | 2.80 | **1.02** |
| 查存在的键：每秒 | 546,937 | 1,204,555 |

<figure class="diagram">
<svg viewBox="0 0 700 200" role="img" aria-label="每次查找平均读的数据块（200 万条，L1～L3 三层）：查不存在的键，没有过滤器 3.00 个，每键 10 位的过滤器 0.03 个；查存在的键 2.80 个降到 1.02 个">
  <defs><marker id="eff-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="eff-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="190.0" y="36.0" text-anchor="end" style="fill:var(--fg);font-size:12px">不存在的键，无过滤器</text><rect x="200.0" y="20.0" width="360.0" height="22.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="568.0" y="36.0" text-anchor="start" style="fill:var(--muted);font-size:11px">3.00 个块</text><text x="190.0" y="72.0" text-anchor="end" style="fill:var(--fg);font-size:12px">不存在的键，10 位</text><rect x="200.0" y="56.0" width="3.6" height="22.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="211.6" y="72.0" text-anchor="start" style="fill:var(--muted);font-size:11px">0.03 个块</text><text x="190.0" y="108.0" text-anchor="end" style="fill:var(--fg);font-size:12px">存在的键，无过滤器</text><rect x="200.0" y="92.0" width="336.0" height="22.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="544.0" y="108.0" text-anchor="start" style="fill:var(--muted);font-size:11px">2.80 个块</text><text x="190.0" y="144.0" text-anchor="end" style="fill:var(--fg);font-size:12px">存在的键，10 位</text><rect x="200.0" y="128.0" width="122.4" height="22.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="330.4" y="144.0" text-anchor="start" style="fill:var(--muted);font-size:11px">1.02 个块</text><text x="20.0" y="190.0" text-anchor="start" style="fill:var(--muted);font-size:11px">速度：不存在的键每秒 53 万次 → 663 万次；存在的键 55 万次 → 120 万次</text>
  </g>
</svg>
</figure>

查不存在的键，读块的次数降到了原来的 1%，速度快了 12 倍：问 3 个过滤器只要几十纳秒，根本不读盘。查存在的键，上面两层的白读几乎都被排除了，只剩下真正有它的那一层的 1 次读，外加偶尔的误判。

这里的数据都在页缓存中，读一个块也只是一次内存复制；数据在磁盘上时，一次读要几十微秒到几毫秒，过滤器的作用会大得多。

从统计中也能算出实际的假阳性率：查不存在的键时，一共问了 599977 次过滤器，排除了 594311 次，另外 5666 次说「可能有」，全是误报，**假阳性率 0.94%**。

## 4. 每键几位、几个哈希函数

n 个键、m 位、k 个哈希函数，假阳性率约为：

**(1 − e^(−kn/m))^k**

对每键位数 b = m / n，让它最小的 k 约为 **b × ln 2 ≈ 0.69 b**，这时假阳性率约为 0.6185^b。lsm-kv 按这个公式取 k：

```c
int k = (int)(bpk * 0.69);   // 最优的哈希函数个数 ≈ 每键位数 × ln2
```

用 `bloomtest` 实测：放入 100 万个键，再查 100 万个不存在的键：

```
每键位数  哈希个数  实测假阳性率  理论值
       2         1       39.352%  39.347%
       4         2       15.480%  15.482%
       6         4        8.488%   5.606%
       8         5        3.182%   2.168%
      10         6        1.299%   0.844%
      12         8        0.499%   0.314%
      14         9        0.213%   0.121%
      16        11        0.093%   0.046%
```

<figure class="diagram">
<svg viewBox="0 0 700 240" role="img" aria-label="布隆过滤器的假阳性率与每键位数（对数坐标）：实线是实测，虚线是理论值。k 为 1、2 时两者重合，k 从 4 开始实测约为理论的 1.5 倍">
  <defs><marker id="fpr-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="fpr-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <line x1="70" y1="68.3" x2="650" y2="68.3" style="stroke:var(--border);stroke-width:1"/><text x="62.0" y="72.3" text-anchor="end" style="fill:var(--muted);font-size:10px">10%</text><line x1="70" y1="116.5" x2="650" y2="116.5" style="stroke:var(--border);stroke-width:1"/><text x="62.0" y="120.5" text-anchor="end" style="fill:var(--muted);font-size:10px">1%</text><line x1="70" y1="164.8" x2="650" y2="164.8" style="stroke:var(--border);stroke-width:1"/><text x="62.0" y="168.8" text-anchor="end" style="fill:var(--muted);font-size:10px">0.1%</text><path d="M70.0,39.5 L152.9,59.1 L235.7,80.4 L318.6,100.3 L401.4,120.1 L484.3,140.8 L567.1,160.8 L650.0,181.0" style="fill:none;stroke:var(--muted);stroke-width:1.5;stroke-dasharray:5 4"/><path d="M70.0,39.5 L152.9,59.1 L235.7,71.7 L318.6,92.3 L401.4,111.0 L484.3,131.1 L567.1,148.9 L650.0,166.3" style="fill:none;stroke:var(--accent);stroke-width:2"/><circle cx="70.0" cy="39.5" r="3" style="fill:var(--accent)"/><text x="70.0" y="208.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">2</text><circle cx="152.9" cy="59.1" r="3" style="fill:var(--accent)"/><text x="152.9" y="208.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">4</text><circle cx="235.7" cy="71.7" r="3" style="fill:var(--accent)"/><text x="235.7" y="208.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">6</text><circle cx="318.6" cy="92.3" r="3" style="fill:var(--accent)"/><text x="318.6" y="208.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">8</text><circle cx="401.4" cy="111.0" r="3" style="fill:var(--accent)"/><text x="401.4" y="208.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">10</text><circle cx="484.3" cy="131.1" r="3" style="fill:var(--accent)"/><text x="484.3" y="208.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">12</text><circle cx="567.1" cy="148.9" r="3" style="fill:var(--accent)"/><text x="567.1" y="208.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">14</text><circle cx="650.0" cy="166.3" r="3" style="fill:var(--accent)"/><text x="650.0" y="208.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">16</text><text x="360.0" y="232.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">每键位数</text><text x="407.4" y="103.0" text-anchor="start" style="fill:var(--accent);font-size:11px">10 位：实测 1.30%</text><text x="407.4" y="136.1" text-anchor="start" style="fill:var(--muted);font-size:11px">理论 0.84%</text>
  </g>
</svg>
</figure>

每多 2 位，假阳性率大约降到原来的 40%。每键 10 位、误报约 1%，是 LevelDB、RocksDB 的默认选择：200 万个键只要 2.4 MB 内存，换来 99% 的无用读取被挡掉。

但有一个和理论不一致的地方：k 为 1、2 时实测和理论几乎完全一样，**k 从 4 开始，实测比理论高了 50% 左右**。原因在哈希函数上。lsm-kv 和 LevelDB 一样，不真的算 k 个独立的哈希，而是只算一个 32 位的哈希值 h，用「双重哈希」生成 k 个位置：

```c
uint32_t h = hash1(key, kl), delta = (h >> 17) | (h << 15);   // delta 由 h 旋转得到
for (int i = 0; i < k; i++, h += delta) 置位(h % nbits);
```

k 个位置都由同一个 32 位的值决定，两个键只要 h 相同，k 个位置就完全相同；delta 又是 h 自己旋转而来，位置之间并不独立。k 越大，这种相关性的影响越明显。（第 3 节中整个数据库实测 0.94%，比 `bloomtest` 的 1.30% 低，是因为键的格式和测试中的不同，哈希的分布也不同。）RocksDB 的新版过滤器改用 64 位哈希和更好的位置生成方法，实测的假阳性率就很接近理论值。对一个只需要「大约 1%」的过滤器来说，这个差距可以接受。

## 5. 局限

- **只对点查有用**。范围查询（「所有以 user:100 开头的键」）无法用普通的布隆过滤器排除文件，RocksDB 为此有「前缀布隆过滤器」；
- **不能删除**：一位可能被多个键共用，不能清零。SSTable 不可修改，正好没有这个问题，文件被合并掉时过滤器一起丢弃；
- **内存**：每键 10 位，10 亿个键就是 1.2 GB。RocksDB 允许最底层不建过滤器（数据大多在最底层，查到最底层时反正要读），以节省内存；
- **存在的键仍然要读**：过滤器只能排除「一定没有」的文件，不能减少真正需要的那次读。

## 6. 常见错误

1. **哈希函数个数随便取**。k 太大或太小都会让假阳性率升高，取每键位数 × 0.69。
2. **用一个 32 位的哈希生成很多个位置**，以为能达到理论值。k 较大时，相关性会让实测明显变差。
3. **以为布隆过滤器能加快所有的查询**。它只排除「一定没有」，对存在的键、范围查询帮助有限。
4. **每键位数太少**。2 位时 39% 的误报，几乎没有用。
5. **只看平均延迟，不看读块的次数**。数据在页缓存中时差别不大，数据量超过内存时，读块的次数才决定性能。

## 7. 小结

- LSM 树查一个不存在的键要查遍所有的层；实测 3 层时每次读 3.00 个块，查存在的键平均 2.80 个，和按每层数据比例算的期望一致；
- 布隆过滤器用 k 个哈希把键映射到位数组，只有假阳性没有假阴性；
- 每键 10 位：查不存在的键读块降到 0.03 个、快了 12 倍；查存在的键降到 1.02 个；
- 最优的哈希函数个数约为每键位数 × 0.69；实测 10 位时假阳性率 1.30%，比理论的 0.84% 高，原因是双重哈希的位置相关；
- 过滤器只对点查有效，内存约为每键 1.25 字节。

## 8. 练习

1. 每键 10 位、假阳性率 1%。数据分布在 5 层中，查一个不存在的键，平均要读几个块？存在于最底层的键呢？
2. 如果把每键位数从 10 提高到 20，内存加倍，换来了什么？值得吗？
3. 为什么说「最底层不建过滤器」是一个合理的取舍？

<details>
<summary>点击查看答案</summary>

1. 不存在的键：5 层各有 1% 的概率误报，平均读 5 × 0.01 = 0.05 个块。最底层的键：上面 4 层平均误报 0.04 次，最底层真正读 1 次，共约 1.04 个块。

2. 假阳性率从约 1% 降到约 0.01%（理论上 0.6185^20 ≈ 0.007%），不存在的键的读块从 0.05 降到几乎为 0，存在的键从 1.04 降到 1.0。也就是说，最多再省下每次查找 0.04～0.05 次读。如果读块很贵（数据远大于内存、存储很慢）、不存在的查询很多，可能值得；大多数情况下，把这些内存用作块缓存，命中更多的块，收益更大。

3. 大部分数据（约 90%）在最底层，最底层的过滤器占了全部过滤器内存的约 90%。对于存在的键，查到最底层时它多半就在那里，过滤器帮不上忙；对于不存在的键，最底层的过滤器只能省下最后那一次读。用 90% 的内存只换来很少的收益，不如省下来；如果业务中「查不存在的键」很多，再打开它。

</details>
