---
title: 跳表
date: 2026-11-06 10:10:00
updated: 2026-11-06 10:10:00
categories:
  - 数据结构
  - 排序与查找
tags:
  - 数据结构
  - 查找
  - 链表
  - 随机化
order: 6
top_group_list: true
description: 有序链表查找要从头走到尾。跳表在它上面加了几层「快速通道」：每个节点抛硬币决定自己有几层，上一层的节点数大约是下一层的一半，查找时从最高层往下、逐层缩小范围，平均 O(log n)。不需要旋转，代码比红黑树短得多，Redis 的有序集合就是用它实现的。本文实现跳表，统计 100 万个节点的层数分布，和有序链表、二分查找、红黑树比较。
disableNunjucks: true
viz: [ds-extra]
---

> **本文目标**：理解跳表的结构和查找过程；会实现查找、插入、删除；理解随机层数为什么能让期望复杂度是 O(log n)；知道跳表和平衡树相比的优缺点。
>
> **前置知识**：《单链表》、《二分查找》、《红黑树》（作为对比）。本文的完整代码在 [projects/ds-extra/skiplist.c](https://github.com/Lucakes-bw/Lucakes-bw.github.io/tree/main/projects/ds-extra/skiplist.c)。

## 1. 给链表加上快速通道

有序数组可以二分查找，但插入、删除要移动元素，O(n)。有序链表插入、删除只要改指针，但查找只能从头一个个往后走，也是 O(n)。

有没有办法让链表也能「跳着」找？想象地铁的快车和慢车：慢车每站都停，快车只停大站。要去一个小站，先坐快车到离它最近的大站，再换慢车。

**跳表**（skip list）就是这个思路：在有序链表上面再加几层链表，越往上的层，节点越少：

```
第 3 层：头 ───── ───── ───── → 12 ───── ───── → 21 ───── ─────
第 2 层：头 ───── ───── ───── → 12 → 17 → 19 → 21 ───── ─────
第 1 层：头 ───── →  7 →  9 → 12 → 17 → 19 → 21 → 25 → 26
第 0 层：头 →  3 →  7 →  9 → 12 → 17 → 19 → 21 → 25 → 26
```

（这是示例程序插入 9 个数后实际得到的结构的下面 4 层，21 一共有 7 层。）第 0 层是完整的有序链表，每个节点出现在从第 0 层开始的连续几层中。

### 1.1 查找

找 19：

1. 从头节点的最高层（第 6 层）开始，向右看：下一个是 21，比 19 大，不能走，**下降一层**。第 5、4 层也一样；
2. 第 3 层：下一个是 12，比 19 小，走过去；再下一个是 21，比 19 大，下降一层；
3. 第 2 层：从 12 向右，17 < 19，走过去；再下一个是 19，不小于 19，下降一层；
4. 第 1 层、第 0 层同样，下一个都是 19，最后在第 0 层停在 17，它的下一个就是 19，找到了。

规则只有一条：**在当前层尽量向右走，走不动了就下降一层**。

```c
static bool slFind(SkipList* s, int key)
{
    SkipNode* x = s->head;
    for (int i = s->level - 1; i >= 0; --i) {           // 从最高层往下
        while (x->next[i] && x->next[i]->key < key) x = x->next[i];   // 在这一层往右走
    }
    x = x->next[0];
    return x && x->key == key;
}
```

<div class="dsviz" data-viz="skiplist"></div>

### 1.2 节点的结构

每个节点有一个 `next` 数组，长度就是它的层数：

```c
typedef struct SkipNode {
    int key;
    int level;                              // 这个节点有几层
    struct SkipNode* next[];                // next[i]：第 i 层的下一个节点（柔性数组）
} SkipNode;
```

`next[]` 是 C99 的柔性数组成员：分配节点时根据层数决定数组的长度，`malloc(sizeof(SkipNode) + sizeof(SkipNode*) * level)`。头节点不存数据，有最多的层数（32 层）。

## 2. 每个节点有几层：抛硬币

如果严格规定「第 1 层每隔一个节点有一个，第 2 层每隔三个……」，查找当然是 O(log n)，但插入一个节点以后，后面所有节点的层数都要调整，又退化成 O(n) 了。

跳表的办法是**随机**：插入一个节点时抛硬币，正面就再长一层，直到抛出反面为止：

```c
// 抛硬币：每一层有 1/2 的概率再往上长一层
static int randomLevel(void)
{
    int level = 1;
    while (level < MAX_LEVEL && (nextRand() & 1)) ++level;
    return level;
}
```

一个节点有至少 k 层的概率是 1/2^(k-1)。所以平均来说，第 1 层的节点数是第 0 层的一半，第 2 层又是第 1 层的一半……插入 100 万个随机数以后实际统计：

```
第  0 层： 999527 个节点
第  1 层： 499711 个节点
第  2 层： 249776 个节点
第  3 层： 125128 个节点
第  4 层：  62589 个节点
……
第 10 层：    970 个节点
……
第 19 层：      1 个节点
第 20 层：      1 个节点
共 999527 个节点，最高 21 层，平均每个节点 2.001 个指针
```

每一层几乎正好是下一层的一半，最高 21 层，接近 log₂(100 万) ≈ 20。平均每个节点 2 个指针：1 + 1/2 + 1/4 + … = 2。和单链表相比，空间只多了一倍。

<figure class="diagram">
<svg viewBox="0 0 700 300" role="img" aria-label="100 万个节点的跳表中，各层的节点数：第 0 层 999527 个，第 1 层 499711 个，第 2 层 249776 个……每一层几乎正好是下一层的一半">
  <defs><marker id="sll-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="sll-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="80.0" y="29.0" text-anchor="end" style="fill:var(--fg);font-size:11px">第 0 层</text><rect x="90" y="16" width="480.0" height="16" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="578.0" y="29.0" text-anchor="start" style="fill:var(--muted);font-size:11px">999,527</text><text x="80.0" y="53.0" text-anchor="end" style="fill:var(--fg);font-size:11px">第 1 层</text><rect x="90" y="40" width="240.0" height="16" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="338.0" y="53.0" text-anchor="start" style="fill:var(--muted);font-size:11px">499,711</text><text x="80.0" y="77.0" text-anchor="end" style="fill:var(--fg);font-size:11px">第 2 层</text><rect x="90" y="64" width="119.9" height="16" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="217.9" y="77.0" text-anchor="start" style="fill:var(--muted);font-size:11px">249,776</text><text x="80.0" y="101.0" text-anchor="end" style="fill:var(--fg);font-size:11px">第 3 层</text><rect x="90" y="88" width="60.1" height="16" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="158.1" y="101.0" text-anchor="start" style="fill:var(--muted);font-size:11px">125,128</text><text x="80.0" y="125.0" text-anchor="end" style="fill:var(--fg);font-size:11px">第 4 层</text><rect x="90" y="112" width="30.1" height="16" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="128.1" y="125.0" text-anchor="start" style="fill:var(--muted);font-size:11px">62,589</text><text x="80.0" y="149.0" text-anchor="end" style="fill:var(--fg);font-size:11px">第 5 层</text><rect x="90" y="136" width="15.1" height="16" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="113.1" y="149.0" text-anchor="start" style="fill:var(--muted);font-size:11px">31,518</text><text x="80.0" y="173.0" text-anchor="end" style="fill:var(--fg);font-size:11px">第 6 层</text><rect x="90" y="160" width="7.6" height="16" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="105.6" y="173.0" text-anchor="start" style="fill:var(--muted);font-size:11px">15,727</text><text x="80.0" y="197.0" text-anchor="end" style="fill:var(--fg);font-size:11px">第 7 层</text><rect x="90" y="184" width="3.8" height="16" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="101.8" y="197.0" text-anchor="start" style="fill:var(--muted);font-size:11px">7,892</text><text x="80.0" y="221.0" text-anchor="end" style="fill:var(--fg);font-size:11px">第 8 层</text><rect x="90" y="208" width="1.9" height="16" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="99.9" y="221.0" text-anchor="start" style="fill:var(--muted);font-size:11px">3,936</text><text x="80.0" y="245.0" text-anchor="end" style="fill:var(--fg);font-size:11px">第 9 层</text><rect x="90" y="232" width="1.5" height="16" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="99.5" y="245.0" text-anchor="start" style="fill:var(--muted);font-size:11px">1,989</text><text x="80.0" y="269.0" text-anchor="end" style="fill:var(--fg);font-size:11px">第 10 层</text><rect x="90" y="256" width="1.5" height="16" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="99.5" y="269.0" text-anchor="start" style="fill:var(--muted);font-size:11px">970</text><text x="90.0" y="292.0" text-anchor="start" style="fill:var(--muted);font-size:11px">第 11～20 层还有 471、230、110……直到 1 个；平均每个节点 1 + 1/2 + 1/4 + … ≈ 2.001 个指针</text>
  </g>
</svg>
</figure>

### 2.1 为什么是 O(log n)

层数是 O(log n)（第 k 层平均只有 n/2^k 个节点，超过 log₂n 层就几乎没有节点了）。在每一层上，平均向右走几步？倒过来看查找的路径：从第 0 层的终点往回走，每到一个节点，如果它还有更高的层就往上走，否则往左走。一个节点有更高一层的概率是 1/2，所以平均每往左走 1 步就会往上 1 步，每层平均只走 2 步左右。总共 O(log n) 步，大约 2log₂n 次比较。

实测：

```
插入 0.165 秒，查找 1000000 次 0.174 秒，平均每次比较 37.8 次（log2 n ≈ 19.9）
```

平均 37.8 次比较，正好接近 2log₂n。去掉所有的上层，只剩第 0 层的有序链表，在 2 万个节点上比较：

```
n=20000：跳表平均比较 27.1 次，只用第 0 层（有序链表）平均比较 10000.5 次
```

## 3. 插入和删除

### 3.1 插入

和查找一样从上往下走，但在每一层下降之前，记下这一层中**新节点前面的那个节点**（`update[i]`）。然后抛硬币决定新节点的层数，在每一层像单链表一样插入：

```c
static bool slInsert(SkipList* s, int key)
{
    SkipNode* update[MAX_LEVEL];                        // update[i]：第 i 层中，新节点前面的那个节点
    SkipNode* x = s->head;
    for (int i = s->level - 1; i >= 0; --i) {
        while (x->next[i] && x->next[i]->key < key) x = x->next[i];
        update[i] = x;
    }
    if (x->next[0] && x->next[0]->key == key) return false;   // 已经存在
    int level = randomLevel();
    if (level > s->level) {                             // 比现在的都高：多出的层前面是头节点
        for (int i = s->level; i < level; ++i) update[i] = s->head;
        s->level = level;
    }
    SkipNode* n = newNode(key, level);
    for (int i = 0; i < level; ++i) {                   // 每一层都像普通链表一样插入
        n->next[i] = update[i]->next[i];
        update[i]->next[i] = n;
    }
    ++s->size;
    return true;
}
```

**没有旋转，没有颜色，没有向上的修复**。插入只影响新节点所在的那几层的前驱。

### 3.2 删除

同样先找到每一层的前驱，然后在节点出现的每一层中把它摘掉：

```c
for (int i = 0; i < x->level; ++i) update[i]->next[i] = x->next[i];
free(x);
while (s->level > 1 && s->head->next[s->level - 1] == NULL) --s->level;   // 最高层空了就降下来
```

程序随机做 30 万次插入、删除、查找（key 在 0 到 4999 之间），每次的结果都和一个布尔数组对比，全部一致；也在 AddressSanitizer 下运行过。

## 4. 和其他结构比较

100 万个随机数（`-O2`）：

| 结构 | 插入 100 万个 | 查找 100 万次 | 有序遍历 | 插入、删除 |
|---|---|---|---|---|
| 跳表 | 0.165 s | 0.174 s | 沿第 0 层走 | O(log n)，期望 |
| 红黑树（《红黑树》） | 0.135 s | 0.060 s | 中序遍历 | O(log n)，最坏 |
| 排好序的数组 + 二分查找 | （排序） | 0.060 s | 直接遍历 | O(n) |

跳表的查找比红黑树和二分查找慢了将近 3 倍：它的比较次数大约是 2log₂n，比平衡树的 log₂n 多；而且每次比较都要跟着一个指针跳到内存中的另一个位置，缓存不友好。

那为什么 Redis 的有序集合（ZSET）、LevelDB 和 RocksDB 的内存表（MemTable）用跳表？

1. **实现简单**：插入、删除就是链表操作，没有旋转和重新着色，不容易出错；
2. **范围查询方便**：找到起点以后，沿着第 0 层往后走就是有序的；平衡树需要中序遍历的栈或者父指针；
3. **容易做成并发的**：插入只修改少数几个前驱的指针，可以用无锁的方式实现（Java 的 `ConcurrentSkipListMap`）；平衡树的旋转会影响一片节点，很难无锁；
4. **可以调整空间和速度的取舍**：把晋升概率从 1/2 改成 1/4，平均每个节点只有 1.33 个指针，查找稍慢一点。Redis 就是用的 1/4。

Redis 的作者对这个选择有一段解释：跳表的内存占用和平衡树差不多，范围查询（ZRANGE）很常见，并且跳表的代码更容易阅读、调试和修改。

## 5. 常见错误

1. **层数上限太小**。MAX_LEVEL 是 16 时，最多只适合几万个元素；节点数超过 2^MAX_LEVEL 以后，上层就不够用了，查找变慢。32 层足够 40 亿个元素。
2. **新节点比当前最高层还高时，忘了把多出的层的前驱设为头节点**。`update[i]` 是随机值，插入时访问野指针。
3. **删除后没有降低 `level`**。功能上没有问题，但查找时从一个空的层开始，白走几步。
4. **用 `rand() % 2` 作为随机数**。有些平台上 `rand()` 的最低位周期很短，层数的分布会很不均匀。用质量好的随机数生成器，或者取高位。
5. **以为跳表的最坏情况也是 O(log n)**。它是期望复杂度。极端情况下（随机数很不走运），所有节点都只有 1 层，退化成链表。概率极小，但理论上存在。

## 6. 小结

- 跳表是多层的有序链表，每个节点抛硬币决定层数，上一层平均是下一层的一半；
- 查找：从最高层开始，尽量向右，走不动就下降，期望 O(log n)，大约 2log₂n 次比较；
- 插入、删除：记下每一层的前驱，像单链表一样修改指针，不需要旋转；
- 比平衡树慢一些，但简单、范围查询方便、容易并发，Redis 的有序集合、LevelDB 的内存表都用它。

## 7. 练习

1. 晋升概率改成 1/4，平均每个节点有几个指针？平均比较次数大约是多少？改一下示例程序验证。
2. 怎样在跳表中支持「找第 k 小的元素」？（提示：Redis 的跳表在每个指针上记录了它跨过了多少个节点。）
3. 跳表能不能用来实现一个有序的集合，支持查找「大于等于 x 的最小元素」？

<details>
<summary>点击查看答案</summary>

1. 平均指针数是 1 + 1/4 + 1/16 + … = 4/3 ≈ 1.33。层数大约是 log₄n，每层平均向右走大约 4 步（往回看时，平均走 4 步左才往上一步），总的比较次数大约是 4log₄n = 2log₂n，和 1/2 时一样。实测 100 万个随机数：平均每个节点 1.334 个指针，最高 10 层，平均比较 37.4 次，查找 100 万次 0.165 秒，和 1/2 时（37.8 次、0.174 秒）几乎没有差别，而指针少了三分之一。这就是 Redis 选择 1/4 的原因。

2. 每个节点的每一层除了 next 指针，再记录一个 span：从这个节点沿这一层走到下一个节点，跨过了第 0 层的多少个节点。查找第 k 小时，从最高层开始，如果当前累计的位置加上 span 不超过 k，就往右走并累加，否则下降。插入、删除时要更新路径上的 span。Redis 的 ZRANK、ZRANGE 就是这样做的。

3. 可以，而且很自然：按查找的方法走到第 0 层，停下来的位置 x 满足 x->key < 目标，x->next[0] 就是大于等于目标的最小元素。这就是 C++ `std::map::lower_bound` 的功能。

</details>
