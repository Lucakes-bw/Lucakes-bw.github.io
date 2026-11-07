---
title: LRU 缓存
date: 2026-11-06 10:30:00
updated: 2026-11-06 10:30:00
categories:
  - 数据结构
  - 排序与查找
tags:
  - 数据结构
  - 哈希表
  - 链表
  - 缓存
order: 7
top_group_list: true
description: 缓存满了，该扔掉哪一个？LRU 的回答是「最久没用过的那个」。只用哈希表，找不到最久没用的；只用链表，查找是 O(n)。把两者组合起来，get 和 put 都是 O(1)。本文实现 LRU 缓存，和暴力实现对拍；再用 1000 万次符合齐夫分布的访问比较 LRU、FIFO 和随机淘汰的命中率，最后看一个让 LRU 命中率变成 0 的访问模式。
disableNunjucks: true
viz: [ds-extra]
notebook: true
---

> **本文目标**：理解 LRU 淘汰策略；会用哈希表加双向链表实现 O(1) 的 get 和 put；知道 LRU 在什么情况下效果好、在什么情况下很差。
>
> **前置知识**：《双向链表与循环链表》、《哈希表》。本文的完整代码在 [projects/ds-extra/lru.c](https://github.com/Lucakes-bw/Lucakes-bw.github.io/tree/main/projects/ds-extra/lru.c)。

## 1. 缓存满了扔哪个

内存比磁盘快，CPU 缓存比内存快，本地比网络快。把常用的数据放在快的地方，就是**缓存**。缓存的容量总是有限的，满了以后放进新的数据，就要扔掉一个旧的。扔哪个，叫做**淘汰策略**。

最理想的是扔掉「以后最久才会用到」的那个，但未来是不知道的。**LRU**（Least Recently Used，最近最少使用）用过去推测未来：**最久没用过的，以后大概也不会用**，扔掉它。

容量为 3 的 LRU 缓存，依次执行几个操作（左边是最近用过的）：

```
[1]
[2 1]
[3 2 1]
get(1) 命中，[1 3 2]
put(4)：满了，淘汰最久没用的 2，[4 1 3]
get(2) 未命中
```

get(1) 让 1 变成了最近用过的，所以 put(4) 时被淘汰的是 2 而不是 1。

## 2. 两种结构各缺一半

要支持的操作：

- `get(key)`：查找，找到了要把它标记为「最近用过」；
- `put(key, value)`：插入或者更新，同样标记为最近用过；满了要找到最久没用的那个并删掉。

| 结构 | 按 key 查找 | 标记为最近用过 | 找到最久没用的 |
|---|---|---|---|
| 哈希表 | O(1) | 记一个时间戳，O(1) | 扫描全部，O(n) |
| 按使用顺序排的链表 | O(n) | 移到头部，O(1) | 就是尾部，O(1) |
| **哈希表 + 双向链表** | O(1) | O(1) | O(1) |

哈希表擅长查找，但没有顺序；链表能维护顺序，但查找要从头走。**把两者组合起来**：链表按使用顺序串起所有的数据，哈希表中存的是 key 到**链表节点**的指针。查找走哈希表，调整顺序走链表。

<figure class="diagram">
<svg viewBox="0 0 700 250" role="img" aria-label="LRU 缓存的结构：上面是哈希表的桶，桶中存 key 和指向链表节点的指针；下面的双向链表按使用顺序排列，head 后面是最近用过的 4，tail 前面是最久没用的 3。查找走哈希表，调整顺序走链表">
  <defs><marker id="lst-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="lst-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="14.0" y="50.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">哈希表</text><text x="14.0" y="68.0" text-anchor="start" style="fill:var(--muted);font-size:11px">找到节点 O(1)</text><rect x="150" y="34" width="50" height="32" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="175.0" y="54.2" text-anchor="middle" style="fill:var(--fg);font-size:11px"></text><text x="175.0" y="26.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[0]</text><rect x="206" y="34" width="50" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="231.0" y="54.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">key 1</text><text x="231.0" y="26.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[1]</text><rect x="262" y="34" width="50" height="32" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="287.0" y="54.2" text-anchor="middle" style="fill:var(--fg);font-size:11px"></text><text x="287.0" y="26.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[2]</text><rect x="318" y="34" width="50" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="343.0" y="54.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">key 3</text><text x="343.0" y="26.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[3]</text><rect x="374" y="34" width="50" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="399.0" y="54.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">key 4</text><text x="399.0" y="26.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[4]</text><rect x="430" y="34" width="50" height="32" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="455.0" y="54.2" text-anchor="middle" style="fill:var(--fg);font-size:11px"></text><text x="455.0" y="26.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[5]</text><rect x="486" y="34" width="50" height="32" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="511.0" y="54.2" text-anchor="middle" style="fill:var(--fg);font-size:11px"></text><text x="511.0" y="26.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[6]</text><rect x="542" y="34" width="50" height="32" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="567.0" y="54.2" text-anchor="middle" style="fill:var(--fg);font-size:11px"></text><text x="567.0" y="26.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">[7]</text><rect x="150" y="160" width="64" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="182.0" y="181.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">head</text><rect x="260" y="160" width="64" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="292.0" y="181.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">4:40</text><line x1="214" y1="171" x2="258" y2="171" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lst-a)"/><line x1="260" y1="184" x2="216" y2="184" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lst-a)"/><rect x="370" y="160" width="64" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="402.0" y="181.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">1:10</text><line x1="324" y1="171" x2="368" y2="171" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lst-a)"/><line x1="370" y1="184" x2="326" y2="184" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lst-a)"/><rect x="480" y="160" width="64" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="512.0" y="181.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">3:30</text><line x1="434" y1="171" x2="478" y2="171" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lst-a)"/><line x1="480" y1="184" x2="436" y2="184" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lst-a)"/><rect x="590" y="160" width="64" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="622.0" y="181.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">tail</text><line x1="544" y1="171" x2="588" y2="171" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lst-a)"/><line x1="590" y1="184" x2="546" y2="184" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lst-a)"/><line x1="231" y1="66" x2="402" y2="157" style="stroke:var(--accent);stroke-width:1.5" marker-end="url(#lst-a)"/><line x1="343" y1="66" x2="512" y2="157" style="stroke:var(--accent);stroke-width:1.5" marker-end="url(#lst-a)"/><line x1="399" y1="66" x2="292" y2="157" style="stroke:var(--accent);stroke-width:1.5" marker-end="url(#lst-a)"/><text x="14.0" y="182.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">双向链表</text><text x="150.0" y="214.0" text-anchor="start" style="fill:var(--muted);font-size:11px">← 最近用过</text><text x="654.0" y="214.0" text-anchor="end" style="fill:var(--muted);font-size:11px">最久没用 →</text><text x="402.0" y="240.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">摘下、放到头部、淘汰尾部：都只改几个指针，O(1)</text>
  </g>
</svg>
</figure>

链表为什么必须是双向的？找到一个节点以后要把它从链表中间摘下来，单链表要知道它的前驱，只能从头找，又成了 O(n)。双向链表有 `prev` 指针，摘下来是 O(1)。

## 3. 实现

### 3.1 结构

```c
typedef struct Entry {
    int key, value;
    struct Entry *prev, *next;              // 双向链表：越靠近头部，越是最近用过的
    struct Entry* hnext;                    // 哈希表同一个桶中的下一个
} Entry;

typedef struct {
    int capacity, size;
    Entry head, tail;                       // 两个哨兵：head.next 是最近用过的，tail.prev 是最久没用的
    Entry** buckets;
    int nbuckets;
} LRUCache;
```

<!-- run: hidden -->
```c
static unsigned hashInt(int key) { return (unsigned)key * 2654435761u; }

static void lruInit(LRUCache* c, int capacity)
{
    c->capacity = capacity;
    c->size = 0;
    c->head.next = &c->tail;
    c->tail.prev = &c->head;
    c->nbuckets = 1;
    while (c->nbuckets < capacity * 2) c->nbuckets <<= 1;   // 容量固定，桶数一次分配好，不需要扩容
    c->buckets = calloc((size_t)c->nbuckets, sizeof(Entry*));
    if (!c->buckets) exit(1);
}

static Entry* hfind(LRUCache* c, int key)
{
    for (Entry* e = c->buckets[hashInt(key) & (unsigned)(c->nbuckets - 1)]; e; e = e->hnext)
        if (e->key == key) return e;
    return NULL;
}

static void hremove(LRUCache* c, Entry* target)
{
    Entry** link = &c->buckets[hashInt(target->key) & (unsigned)(c->nbuckets - 1)];
    while (*link != target) link = &(*link)->hnext;
    *link = target->hnext;
}
```

一个 `Entry` 同时在两个结构中：用 `prev`/`next` 挂在双向链表上，用 `hnext` 挂在哈希表的桶中（链地址法，见《哈希表》）。

头尾各放一个**哨兵节点**，链表永远不为空，插入和删除时不用判断「是不是第一个、是不是最后一个」（《双向链表与循环链表》中的技巧）。

节点中要存 `key`：淘汰尾部的节点时，要用它的 key 去哈希表中把对应的项也删掉。

### 3.2 两个链表操作

```c
static void unlink(Entry* e)
{
    e->prev->next = e->next;
    e->next->prev = e->prev;
}

static void pushFront(LRUCache* c, Entry* e)    // 放到链表头部：最近用过
{
    e->next = c->head.next;
    e->prev = &c->head;
    c->head.next->prev = e;
    c->head.next = e;
}
```

有了哨兵，这两个函数一个 `if` 都没有。

### 3.3 get 和 put

```c
// 查找；找到时把它移到头部。返回是否命中
static bool lruGet(LRUCache* c, int key, int* value)
{
    Entry* e = hfind(c, key);
    if (!e) return false;
    unlink(e);
    pushFront(c, e);
    *value = e->value;
    return true;
}

static void lruPut(LRUCache* c, int key, int value)
{
    Entry* e = hfind(c, key);
    if (e) {                                // 已经在缓存中：更新值，移到头部
        e->value = value;
        unlink(e);
        pushFront(c, e);
        return;
    }
    if (c->size == c->capacity) {           // 满了：淘汰链表尾部，也就是最久没用过的
        Entry* old = c->tail.prev;
        unlink(old);
        hremove(c, old);
        free(old);
        --c->size;
    }
    e = malloc(sizeof(Entry));
    if (!e) exit(1);
    e->key = key;
    e->value = value;
    unsigned b = hashInt(key) & (unsigned)(c->nbuckets - 1);
    e->hnext = c->buckets[b];
    c->buckets[b] = e;
    pushFront(c, e);
    ++c->size;
}
```

容量是固定的，所以哈希表的桶数在初始化时一次分配好（不小于容量两倍的 2 的幂，装填因子不超过 0.5），不需要扩容。每一步都是常数次指针操作，**get 和 put 都是 O(1)**。

<div class="dsviz" data-viz="lru"></div>

测试一下，重复第 1 节的例子：

<!-- run: hidden -->
```c
static void lruFree(LRUCache* c)
{
    for (Entry* e = c->head.next; e != &c->tail;) {
        Entry* n = e->next;
        free(e);
        e = n;
    }
    free(c->buckets);
}

static void lruPrint(const LRUCache* c)     // 从最近用过的到最久没用的
{
    printf("[");
    for (Entry* e = c->head.next; e != &c->tail; e = e->next) printf(e == c->head.next ? "%d" : " %d", e->key);
    printf("]\n");
}
```

```c
LRUCache c;
lruInit(&c, 3);
int v;
lruPut(&c, 1, 10), lruPrint(&c);
lruPut(&c, 2, 20), lruPrint(&c);
lruPut(&c, 3, 30), lruPrint(&c);
printf("get(1) %s，", lruGet(&c, 1, &v) ? "命中" : "未命中"), lruPrint(&c);
printf("put(4)：满了，淘汰最久没用的 2，"), lruPut(&c, 4, 40), lruPrint(&c);
printf("get(2) %s\n", lruGet(&c, 2, &v) ? "命中" : "未命中");
lruFree(&c);
```

```
[1]
[2 1]
[3 2 1]
get(1) 命中，[1 3 2]
put(4)：满了，淘汰最久没用的 2，[4 1 3]
get(2) 未命中
```

### 3.4 对拍

再写一个简单但一定正确的版本：数组存所有的项，每次访问记一个递增的时间戳，淘汰时扫描整个数组找时间戳最小的。随机生成操作序列，两个版本同时执行，比较每次 get 的结果：

<!-- run: hidden -->
```c
typedef struct {
    int key[64], value[64], stamp[64], n, cap, clock;
} Brute;

static int bfind(Brute* b, int key)
{
    for (int i = 0; i < b->n; ++i)
        if (b->key[i] == key) return i;
    return -1;
}
static bool bruteGet(Brute* b, int key, int* value)
{
    int i = bfind(b, key);
    if (i < 0) return false;
    b->stamp[i] = ++b->clock;
    *value = b->value[i];
    return true;
}
static void brutePut(Brute* b, int key, int value)
{
    int i = bfind(b, key);
    if (i < 0) {
        if (b->n < b->cap) {
            i = b->n++;
        } else {
            i = 0;
            for (int k = 1; k < b->n; ++k)
                if (b->stamp[k] < b->stamp[i]) i = k;   // 时间戳最小的就是最久没用的
        }
        b->key[i] = key;
    }
    b->value[i] = value;
    b->stamp[i] = ++b->clock;
}

static unsigned long long g_rand = 88172645463325252ULL;   // 固定种子的随机数（xorshift），每次运行结果相同
static int randInt(int lo, int hi)
{
    g_rand ^= g_rand << 13;
    g_rand ^= g_rand >> 7;
    g_rand ^= g_rand << 17;
    return lo + (int)(g_rand % (unsigned long long)(hi - lo + 1));
}
```

```c
int same = 1;
for (int t = 0; t < 200; ++t) {
    int cap = randInt(1, 16);
    LRUCache x;
    lruInit(&x, cap);
    Brute b = {.cap = cap};
    for (int step = 0; step < 2000; ++step) {
        int key = randInt(0, 30), val = randInt(0, 1000), v1 = -1, v2 = -1;
        if (randInt(0, 1)) {
            lruPut(&x, key, val);
            brutePut(&b, key, val);
        } else {
            bool h1 = lruGet(&x, key, &v1), h2 = bruteGet(&b, key, &v2);
            if (h1 != h2 || (h1 && v1 != v2)) same = 0;
        }
    }
    lruFree(&x);
}
printf("随机 200 组、每组 2000 次操作，和暴力实现的结果%s\n", same ? "一致" : "不一致");
```

```
随机 200 组、每组 2000 次操作，和暴力实现的结果一致
```

容量从 1 到 16 随机，key 只在 0～30 中取，保证有大量的命中、更新和淘汰。

## 4. 命中率：LRU 好在哪里

现实中的访问不是均匀的：少数热门的数据被反复访问，大量冷门的数据偶尔访问一次。常用**齐夫分布**（Zipf）来模拟：第 r 热门的数据，被访问的概率和 1/r 成正比。

100 万个不同的 key，按齐夫分布生成 1000 万次访问。先看速度：

```
齐夫分布的 10000000 次访问、容量 10 万：LRU 用时 0.088 秒（每次 9 纳秒），命中率 77.7%
```

缓存只能放下 10% 的 key，却命中了 77.7% 的访问，因为热门的 key 一直留在缓存中。

再和两种更简单的策略比较：**FIFO**（先进先出，淘汰最早放进来的，不管它最近有没有被用过）和**随机淘汰**：

| 容量 | LRU | FIFO | 随机 |
|---|---|---|---|
| 1,000 | 40.3% | 36.7% | 36.7% |
| 10,000 | 58.5% | 55.0% | 55.0% |
| 100,000 | 77.7% | 74.7% | 74.8% |

<figure class="diagram">
<svg viewBox="0 0 700 272" role="img" aria-label="齐夫分布下三种淘汰策略的命中率：容量 1000 时 LRU 40.3%、FIFO 36.7%、随机 36.7%；容量 1 万时 58.5%、55.0%、55.0%；容量 10 万时 77.7%、74.7%、74.8%">
  <defs><marker id="lhr-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="lhr-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <line x1="60" y1="210" x2="640" y2="210" style="stroke:var(--border-2);stroke-width:1.2"/><rect x="90" y="137.5" width="40" height="72.5" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="110.0" y="131.5" text-anchor="middle" style="fill:var(--accent);font-size:11px">40.3</text><rect x="136" y="143.9" width="40" height="66.1" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="156.0" y="137.9" text-anchor="middle" style="fill:var(--muted);font-size:11px">36.7</text><rect x="182" y="143.9" width="40" height="66.1" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="202.0" y="137.9" text-anchor="middle" style="fill:var(--muted);font-size:11px">36.7</text><text x="156.0" y="228.0" text-anchor="middle" style="fill:var(--fg);font-size:12px">容量 1,000</text><rect x="280" y="104.7" width="40" height="105.3" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="300.0" y="98.7" text-anchor="middle" style="fill:var(--accent);font-size:11px">58.5</text><rect x="326" y="111.0" width="40" height="99.0" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="346.0" y="105.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">55.0</text><rect x="372" y="111.0" width="40" height="99.0" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="392.0" y="105.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">55.0</text><text x="346.0" y="228.0" text-anchor="middle" style="fill:var(--fg);font-size:12px">容量 10,000</text><rect x="470" y="70.1" width="40" height="139.9" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="490.0" y="64.1" text-anchor="middle" style="fill:var(--accent);font-size:11px">77.7</text><rect x="516" y="75.5" width="40" height="134.5" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="536.0" y="69.5" text-anchor="middle" style="fill:var(--muted);font-size:11px">74.7</text><rect x="562" y="75.4" width="40" height="134.6" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="582.0" y="69.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">74.8</text><text x="536.0" y="228.0" text-anchor="middle" style="fill:var(--fg);font-size:12px">容量 100,000</text><rect x="80" y="250" width="14" height="12" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="100.0" y="260.0" text-anchor="start" style="fill:var(--fg);font-size:11px">LRU</text><rect x="160" y="250" width="14" height="12" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="180.0" y="260.0" text-anchor="start" style="fill:var(--fg);font-size:11px">FIFO</text><rect x="240" y="250" width="14" height="12" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="260.0" y="260.0" text-anchor="start" style="fill:var(--fg);font-size:11px">随机</text><text x="340.0" y="260.0" text-anchor="start" style="fill:var(--muted);font-size:11px">100 万个 key，1000 万次访问；单位：%</text>
  </g>
</svg>
</figure>

LRU 每一档都高出 3～4 个百分点。FIFO 会把一个热门的 key 淘汰掉，只因为它进来得早；LRU 中热门的 key 每次被访问都会回到头部，很难被挤到尾部。

FIFO 和随机淘汰的命中率几乎一样，也就是说，**只看进来的时间，和随便扔一个差不多**，LRU 的优势来自「最近被访问过」这个信息。

## 5. LRU 的弱点：扫描

把访问模式换成循环扫描：0, 1, 2, …, 1099, 0, 1, 2, …，反复 100 遍，缓存容量 1000：

```
LRU 0.0%，FIFO 0.0%，随机 81.6%
```

**LRU 的命中率是 0**。访问 1000 时，缓存中是 0～999，最久没用的是 0，淘汰 0；接着访问 1001，淘汰 1……每个 key 都恰好在下一次被访问之前被淘汰。随机淘汰反而有 81.6%。

<figure class="diagram">
<svg viewBox="0 0 700 250" role="img" aria-label="循环扫描让 LRU 的命中率变成 0：容量 4，反复访问 0、1、2、3、4。访问 4 时淘汰最久没用的 0，接着访问 0，未命中，淘汰 1；接着访问 1，又刚好被淘汰……每个 key 都在下一次被访问之前被淘汰">
  <defs><marker id="lsc-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="lsc-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="14.0" y="36.0" text-anchor="start" style="fill:var(--fg);font-size:12px">访问 0～3 后</text><rect x="170" y="14" width="44" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="192.0" y="34.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">3</text><rect x="220" y="14" width="44" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="242.0" y="34.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">2</text><rect x="270" y="14" width="44" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="292.0" y="34.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">1</text><rect x="320" y="14" width="44" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="342.0" y="34.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">0</text><text x="14.0" y="78.0" text-anchor="start" style="fill:var(--fg);font-size:12px">访问 4，未命中</text><rect x="170" y="56" width="44" height="32" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="192.0" y="76.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">4</text><rect x="220" y="56" width="44" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="242.0" y="76.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">3</text><rect x="270" y="56" width="44" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="292.0" y="76.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">2</text><rect x="320" y="56" width="44" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="342.0" y="76.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">1</text><rect x="400" y="56" width="44" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="422.0" y="76.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">0</text><text x="456.0" y="78.0" text-anchor="start" style="fill:var(--danger);font-size:11px">淘汰 0：正好是下一个要访问的</text><text x="14.0" y="120.0" text-anchor="start" style="fill:var(--fg);font-size:12px">访问 0，未命中</text><rect x="170" y="98" width="44" height="32" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="192.0" y="118.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">0</text><rect x="220" y="98" width="44" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="242.0" y="118.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">4</text><rect x="270" y="98" width="44" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="292.0" y="118.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">3</text><rect x="320" y="98" width="44" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="342.0" y="118.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">2</text><rect x="400" y="98" width="44" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="422.0" y="118.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">1</text><text x="456.0" y="120.0" text-anchor="start" style="fill:var(--danger);font-size:11px">淘汰 1：正好是下一个要访问的</text><text x="14.0" y="162.0" text-anchor="start" style="fill:var(--fg);font-size:12px">访问 1，未命中</text><rect x="170" y="140" width="44" height="32" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="192.0" y="160.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">1</text><rect x="220" y="140" width="44" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="242.0" y="160.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">0</text><rect x="270" y="140" width="44" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="292.0" y="160.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">4</text><rect x="320" y="140" width="44" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="342.0" y="160.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">3</text><rect x="400" y="140" width="44" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="422.0" y="160.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">2</text><text x="456.0" y="162.0" text-anchor="start" style="fill:var(--danger);font-size:11px">淘汰 2：正好是下一个要访问的</text><text x="14.0" y="204.0" text-anchor="start" style="fill:var(--fg);font-size:12px">访问 2，未命中</text><rect x="170" y="182" width="44" height="32" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="192.0" y="202.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">2</text><rect x="220" y="182" width="44" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="242.0" y="202.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">1</text><rect x="270" y="182" width="44" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="292.0" y="202.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">0</text><rect x="320" y="182" width="44" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="342.0" y="202.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">4</text><rect x="400" y="182" width="44" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="422.0" y="202.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">3</text><text x="456.0" y="204.0" text-anchor="start" style="fill:var(--danger);font-size:11px">淘汰 3：正好是下一个要访问的</text><text x="170.0" y="240.0" text-anchor="start" style="fill:var(--muted);font-size:11px">实测：1100 个 key 循环访问、容量 1000，LRU 和 FIFO 命中率 0.0%，随机淘汰 81.6%</text>
  </g>
</svg>
</figure>

这不是构造出来的极端情况。数据库全表扫描、备份程序读一遍所有的文件，都是一次性地访问大量数据，会把缓存中真正热门的数据全部挤出去，扫描结束以后命中率要很久才能恢复。这叫**缓存污染**。

实际的系统都对 LRU 做了改进：

- **MySQL InnoDB 的缓冲池**：链表分成新、旧两段，新读入的页先放在旧段的头部，在旧段中停留超过一定时间后再被访问，才移到新段。全表扫描读入的页只在旧段中进出，不会冲掉新段中的热门页；
- **LRU-K**：最近被访问过 K 次（通常是 2 次）的才算热门，只访问过一次的优先淘汰；
- **Linux 的页缓存**：分成活跃和不活跃两个链表，原理相似；
- **Redis**：近似 LRU，每次随机取几个 key，淘汰其中最久没用的，不需要维护链表，省内存。还提供 LFU（按访问频率）。

## 6. 常见错误

1. **链表用了单向的**。删除中间的节点要找前驱，O(n)。
2. **节点中没有存 key**。淘汰尾部的节点时，不知道要从哈希表中删除哪一项。
3. **put 一个已经存在的 key 时，又新建了一个节点**。哈希表中有两项，链表中也有两个节点，`size` 多算了一个。应该先查找，存在就更新值并移到头部。
4. **get 命中时忘了移到头部**。这样就成了 FIFO。
5. **淘汰的顺序写反了**：从链表的头部淘汰，扔掉的是刚用过的。
6. **先淘汰再查找**。put 一个已经在缓存中的 key 时，缓存满了，结果把一个不需要淘汰的项淘汰了。

## 7. 小结

- LRU 淘汰最久没用过的数据，用过去的访问推测未来；
- 哈希表负责 O(1) 查找，双向链表负责 O(1) 维护使用顺序，哈希表中存的是指向链表节点的指针；
- 头尾哨兵让链表操作没有特殊情况；
- 在有热点的访问中，LRU 比 FIFO 和随机淘汰的命中率高；但遇到循环扫描，LRU 的命中率可能是 0，实际系统都做了改进。

## 8. 练习

- [LRU 缓存](/problems/lru-cache/)：本文的实现

思考题：

1. C++ 中怎么用标准库实现？
2. 怎么实现 **LFU**（淘汰访问次数最少的，次数相同时淘汰最久没用的），get 和 put 仍然是 O(1)？
3. 在多线程的程序中使用 LRU 缓存，每次 get 都要修改链表，为什么这会成为瓶颈？有什么办法？

<details>
<summary>点击查看答案</summary>

1. `std::list<std::pair<int, int>>` 存 key 和 value，`std::unordered_map<int, std::list<...>::iterator>` 存 key 到链表节点的迭代器。移到头部用 `list.splice(list.begin(), list, it)`，它只修改指针，迭代器仍然有效。

2. 每个访问次数对应一个双向链表（按最近使用的顺序），再用一个哈希表从次数映射到链表，一个哈希表从 key 映射到节点，并记录当前的最小次数 `minFreq`。访问一个 key：把它从次数 f 的链表中摘下，放到次数 f + 1 的链表头部；如果 f 的链表空了并且 f 是 `minFreq`，`minFreq` 加一。淘汰时取 `minFreq` 对应链表的尾部。新插入的 key 次数是 1，`minFreq` 设为 1。

3. get 本来是只读的操作，但 LRU 的 get 要把节点移到头部，修改了共享的链表，所有线程都必须抢同一把锁。办法：把缓存按 key 的哈希值分成多个分片，每个分片一把锁（分段锁）；或者 get 时只记录访问（比如设一个标记位、写入一个缓冲区），由后台批量调整顺序，或者像 Redis 那样用近似 LRU，根本不维护链表。

</details>
