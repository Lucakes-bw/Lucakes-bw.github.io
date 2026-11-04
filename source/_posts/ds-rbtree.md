---
title: 红黑树
date: 2026-11-06 10:00:00
updated: 2026-11-06 10:00:00
categories:
  - 数据结构
  - 树
tags:
  - 数据结构
  - 树
  - 平衡树
  - 红黑树
order: 4
top_group_list: true
description: C++ 的 std::map、Java 的 TreeMap、Linux 内核的进程调度，用的都是红黑树而不是 AVL 树。它的平衡条件更宽松：最长的路径不超过最短的两倍。本文讲清楚五条性质为什么能保证高度是 O(log n)，实现插入时的三种情况和删除时的四种情况，用 20 万次随机操作检查性质；再和 AVL 树实测比较：有序插入 100 万个数时红黑树高 37、AVL 树高 20，随机插入时两者一样高，红黑树的旋转少了 17%。
disableNunjucks: true
viz: [ds-extra]
---

> **本文目标**：理解红黑树的五条性质，以及它们为什么保证了高度不超过 2log₂(n+1)；会实现插入后的修复（三种情况）；了解删除后的修复（四种情况）；知道红黑树和 AVL 树各自的优缺点。
>
> **前置知识**：《二叉搜索树》、《AVL 树》（旋转）。本文的完整代码在 [projects/ds-extra/rbtree.c](https://github.com/Lucakes-bw/Lucakes-bw.github.io/tree/main/projects/ds-extra/rbtree.c)。

## 1. 为什么还要一种平衡树

上一篇的 AVL 树要求每个节点左右子树的高度差不超过 1。这个条件很严格，树非常矮，查找很快；代价是插入、删除时经常要沿着路径检查、旋转，尤其是删除，最多可能要一路旋转到根。

**红黑树**放宽了平衡条件：不要求左右一样高，只保证**最长的路径不超过最短路径的 2 倍**。树会比 AVL 树稍高一点，但插入、删除时需要的调整更少：插入最多旋转 2 次，删除最多旋转 3 次。对于插入、删除很频繁的场景，这更划算。所以 C++ 的 `std::map` 和 `std::set`、Java 的 `TreeMap`、Linux 内核的完全公平调度器（CFS）和内存管理，用的都是红黑树。

## 2. 五条性质

红黑树是一棵二叉搜索树，每个节点多存一位：红色或者黑色。它满足：

1. 每个节点是红色或者黑色；
2. 根是黑色；
3. 所有的叶子（NIL，也就是空指针的位置）都是黑色；
4. **红色节点的两个孩子都是黑色**（不能有两个相邻的红色节点）；
5. **从任一节点到它下面每个叶子的所有路径，经过的黑色节点数相同**。

从一个节点出发（不算它自己）到叶子的路径上的黑色节点数（叶子 NIL 也算），叫作这个节点的**黑高**。

### 2.1 为什么高度是 O(log n)

- 由性质 5，从根出发，每条路径上的黑色节点数都等于根的黑高 bh；
- 由性质 4，红色节点不能相邻，所以一条路径上红色节点最多和黑色节点一样多。最长的路径最多 2·bh 个节点，最短的路径至少 bh 个节点：**最长不超过最短的 2 倍**；
- 可以证明，黑高为 bh 的子树至少有 2^bh - 1 个内部节点（归纳法：两个孩子的黑高至少是 bh - 1）。所以 n ≥ 2^bh - 1，bh ≤ log₂(n+1)，树高 ≤ 2·bh ≤ **2log₂(n+1)**。

AVL 树的高度上限大约是 1.44log₂n，红黑树是 2log₂n，都是 O(log n)。

<figure class="diagram">
<svg viewBox="0 0 700 270" role="img" aria-label="一棵红黑树：根 13 是黑色，红色节点 8、17、6、22、27 的孩子都是黑色；从 13 出发到任意一个 NIL，经过的黑色节点（不算 13，算 NIL）都是 2 个，所以 13 的黑高是 2">
  <defs><marker id="rbp-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rbp-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <line x1="55" y1="142" x2="34" y2="198" style="stroke:var(--border);stroke-width:1"/><line x1="145" y1="142" x2="130" y2="198" style="stroke:var(--border);stroke-width:1"/><line x1="145" y1="142" x2="162" y2="198" style="stroke:var(--border);stroke-width:1"/><line x1="235" y1="142" x2="218" y2="198" style="stroke:var(--border);stroke-width:1"/><line x1="235" y1="142" x2="252" y2="198" style="stroke:var(--border);stroke-width:1"/><line x1="80" y1="198" x2="66" y2="250" style="stroke:var(--border);stroke-width:1"/><line x1="80" y1="198" x2="94" y2="250" style="stroke:var(--border);stroke-width:1"/><line x1="300" y1="198" x2="286" y2="250" style="stroke:var(--border);stroke-width:1"/><line x1="300" y1="198" x2="314" y2="250" style="stroke:var(--border);stroke-width:1"/><line x1="350" y1="198" x2="336" y2="250" style="stroke:var(--border);stroke-width:1"/><line x1="350" y1="198" x2="364" y2="250" style="stroke:var(--border);stroke-width:1"/><line x1="190" y1="30" x2="100" y2="86" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="190" y1="30" x2="280" y2="86" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="100" y1="86" x2="55" y2="142" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="100" y1="86" x2="145" y2="142" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="280" y1="86" x2="235" y2="142" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="280" y1="86" x2="325" y2="142" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="55" y1="142" x2="80" y2="198" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="325" y1="142" x2="300" y2="198" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="325" y1="142" x2="350" y2="198" style="stroke:var(--border-2);stroke-width:1.5"/><circle cx="190" cy="30" r="16" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="190" y="34.6" text-anchor="middle" style="fill:var(--panel);font-size:12px">13</text><circle cx="100" cy="86" r="16" style="fill:var(--panel);stroke:var(--danger);stroke-width:2.5"/><text x="100" y="90.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">8</text><circle cx="280" cy="86" r="16" style="fill:var(--panel);stroke:var(--danger);stroke-width:2.5"/><text x="280" y="90.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">17</text><circle cx="55" cy="142" r="16" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="55" y="146.6" text-anchor="middle" style="fill:var(--panel);font-size:12px">1</text><circle cx="145" cy="142" r="16" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="145" y="146.6" text-anchor="middle" style="fill:var(--panel);font-size:12px">11</text><circle cx="235" cy="142" r="16" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="235" y="146.6" text-anchor="middle" style="fill:var(--panel);font-size:12px">15</text><circle cx="325" cy="142" r="16" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="325" y="146.6" text-anchor="middle" style="fill:var(--panel);font-size:12px">25</text><circle cx="80" cy="198" r="16" style="fill:var(--panel);stroke:var(--danger);stroke-width:2.5"/><text x="80" y="202.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">6</text><circle cx="300" cy="198" r="16" style="fill:var(--panel);stroke:var(--danger);stroke-width:2.5"/><text x="300" y="202.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">22</text><circle cx="350" cy="198" r="16" style="fill:var(--panel);stroke:var(--danger);stroke-width:2.5"/><text x="350" y="202.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">27</text><rect x="23" y="190" width="22" height="16" rx="3" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="34" y="201.5" text-anchor="middle" style="fill:var(--panel);font-size:8px">NIL</text><rect x="119" y="190" width="22" height="16" rx="3" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="130" y="201.5" text-anchor="middle" style="fill:var(--panel);font-size:8px">NIL</text><rect x="151" y="190" width="22" height="16" rx="3" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="162" y="201.5" text-anchor="middle" style="fill:var(--panel);font-size:8px">NIL</text><rect x="207" y="190" width="22" height="16" rx="3" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="218" y="201.5" text-anchor="middle" style="fill:var(--panel);font-size:8px">NIL</text><rect x="241" y="190" width="22" height="16" rx="3" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="252" y="201.5" text-anchor="middle" style="fill:var(--panel);font-size:8px">NIL</text><rect x="55" y="242" width="22" height="16" rx="3" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="66" y="253.5" text-anchor="middle" style="fill:var(--panel);font-size:8px">NIL</text><rect x="83" y="242" width="22" height="16" rx="3" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="94" y="253.5" text-anchor="middle" style="fill:var(--panel);font-size:8px">NIL</text><rect x="275" y="242" width="22" height="16" rx="3" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="286" y="253.5" text-anchor="middle" style="fill:var(--panel);font-size:8px">NIL</text><rect x="303" y="242" width="22" height="16" rx="3" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="314" y="253.5" text-anchor="middle" style="fill:var(--panel);font-size:8px">NIL</text><rect x="325" y="242" width="22" height="16" rx="3" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="336" y="253.5" text-anchor="middle" style="fill:var(--panel);font-size:8px">NIL</text><rect x="353" y="242" width="22" height="16" rx="3" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="364" y="253.5" text-anchor="middle" style="fill:var(--panel);font-size:8px">NIL</text><text x="410.0" y="40.0" text-anchor="start" style="fill:var(--fg);font-size:12px">1. 每个节点是红色或黑色</text><text x="410.0" y="64.0" text-anchor="start" style="fill:var(--fg);font-size:12px">2. 根是黑色</text><text x="410.0" y="88.0" text-anchor="start" style="fill:var(--fg);font-size:12px">3. 叶子（NIL）是黑色</text><text x="410.0" y="112.0" text-anchor="start" style="fill:var(--fg);font-size:12px">4. 红色节点的孩子都是黑色</text><text x="410.0" y="136.0" text-anchor="start" style="fill:var(--fg);font-size:12px">   （不能有两个相邻的红色节点）</text><text x="410.0" y="160.0" text-anchor="start" style="fill:var(--fg);font-size:12px">5. 从任一节点到它下面的每个 NIL，</text><text x="410.0" y="184.0" text-anchor="start" style="fill:var(--fg);font-size:12px">   经过的黑色节点数都相同</text><text x="410.0" y="222.0" text-anchor="start" style="fill:var(--muted);font-size:11px">13 → 8 → 1 → 6 → NIL：黑色 1、NIL，2 个</text><text x="410.0" y="242.0" text-anchor="start" style="fill:var(--muted);font-size:11px">13 → 17 → 15 → NIL：  黑色 15、NIL，2 个</text><text x="410.0" y="262.0" text-anchor="start" style="fill:var(--accent);font-size:12px">最长的路径不超过最短的 2 倍</text>
  </g>
</svg>
</figure>

## 3. 实现的准备

### 3.1 节点和哨兵

```c
typedef enum { RED, BLACK } Color;

typedef struct RBNode {
    int key;
    Color color;
    struct RBNode *left, *right, *parent;
} RBNode;

static RBNode nilNode = {0, BLACK, NULL, NULL, NULL};
#define NIL (&nilNode)
```

和 AVL 树的递归写法不同，红黑树的修复要从下往上走，并且要看父亲、祖父、叔叔，所以每个节点存一个 `parent` 指针。

所有的空指针都换成一个公共的**哨兵节点** NIL，它是黑色的（性质 3）。这样代码中访问 `x->left->color` 时不用先判断是不是空指针。

### 3.2 旋转

旋转和 AVL 树中一样，只是还要维护 `parent` 指针：

```c
// 左旋：x 的右孩子 y 成为这棵子树的新根，x 成为 y 的左孩子
static void leftRotate(RBTree* t, RBNode* x)
{
    RBNode* y = x->right;
    x->right = y->left;
    if (y->left != NIL) y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == NIL) t->root = y;
    else if (x == x->parent->left) x->parent->left = y;
    else x->parent->right = y;
    y->left = x;
    x->parent = y;
}
```

旋转不破坏二叉搜索树的顺序，只改变形状。右旋是它的镜像。

## 4. 插入

### 4.1 新节点是红色的

先按普通二叉搜索树的方法插入，**新节点涂成红色**。为什么是红色？

- 涂成黑色，它所在的路径就多了一个黑色节点，性质 5 被破坏，而且影响到所有经过它的路径，很难修复；
- 涂成红色，性质 5 不受影响。只有一种可能出问题：它的父亲也是红色，违反了性质 4（两个红色相邻）。这是一个局部的问题。

所以插入后只需要处理「红色节点 z 的父亲也是红色」这一种情况。父亲是红色，说明父亲不是根（根是黑色），祖父一定存在，并且是黑色（不然祖父和父亲就是两个相邻的红色了，插入之前就违反了性质 4）。

### 4.2 三种情况

设父亲是祖父的左孩子（右孩子的情况是镜像）。关键看**叔叔**（父亲的兄弟）的颜色：

**情况 1：叔叔是红色。** 把父亲和叔叔涂黑、祖父涂红。祖父下面的每条路径，黑色节点数不变（父亲或叔叔中的一个变黑，祖父变红，一加一减），性质 5 保持。但祖父变红了，可能又和它的父亲冲突，所以**把 z 设为祖父，继续向上检查**。

**情况 2：叔叔是黑色，z 是父亲的右孩子**（z、父亲、祖父成一个「折线」）。对父亲左旋，变成情况 3（z 变成了原来的父亲）。

**情况 3：叔叔是黑色，z 是父亲的左孩子**（三个节点成一条「直线」）。对祖父右旋，再把原来的父亲涂黑、原来的祖父涂红。父亲成了这棵子树的新根，而且是黑色的，上面不会再有冲突，**修复结束**。

<figure class="diagram">
<svg viewBox="0 0 700 380" role="img" aria-label="插入调整的三种情况（父亲是祖父的左孩子）。情况 1：叔叔是红色，父亲和叔叔涂黑、祖父涂红，问题上移到祖父。情况 2：叔叔是黑色、z 是右孩子，对父亲左旋变成情况 3。情况 3：叔叔是黑色、z 是左孩子，对祖父右旋，再交换父亲和祖父的颜色，结束">
  <defs><marker id="rbc-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rbc-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="14.0" y="16.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">情况 1</text><line x1="210" y1="20" x2="170" y2="60" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="210" y1="20" x2="250" y2="60" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="170" y1="60" x2="150" y2="100" style="stroke:var(--border-2);stroke-width:1.5"/><circle cx="210" cy="20" r="14" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="210" y="24.6" text-anchor="middle" style="fill:var(--panel);font-size:12px">g</text><circle cx="170" cy="60" r="14" style="fill:var(--panel);stroke:var(--danger);stroke-width:2.5"/><text x="170" y="64.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">p</text><circle cx="250" cy="60" r="14" style="fill:var(--panel);stroke:var(--danger);stroke-width:2.5"/><text x="250" y="64.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">u</text><circle cx="150" cy="100" r="14" style="fill:var(--panel);stroke:var(--danger);stroke-width:2.5"/><text x="150" y="104.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">z</text><line x1="300" y1="50" x2="380" y2="50" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rbc-a)"/><text x="340.0" y="42.0" text-anchor="middle" style="fill:var(--link);font-size:11px">变色</text><line x1="500" y1="20" x2="460" y2="60" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="500" y1="20" x2="540" y2="60" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="460" y1="60" x2="440" y2="100" style="stroke:var(--border-2);stroke-width:1.5"/><circle cx="500" cy="20" r="14" style="fill:var(--panel);stroke:var(--danger);stroke-width:2.5"/><text x="500" y="24.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">g</text><circle cx="460" cy="60" r="14" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="460" y="64.6" text-anchor="middle" style="fill:var(--panel);font-size:12px">p</text><circle cx="540" cy="60" r="14" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="540" y="64.6" text-anchor="middle" style="fill:var(--panel);font-size:12px">u</text><circle cx="440" cy="100" r="14" style="fill:var(--panel);stroke:var(--danger);stroke-width:2.5"/><text x="440" y="104.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">z</text><text x="600.0" y="40.0" text-anchor="start" style="fill:var(--muted);font-size:11px">z 上移到 g</text><text x="600.0" y="58.0" text-anchor="start" style="fill:var(--muted);font-size:11px">继续检查</text><text x="14.0" y="145.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">情况 2</text><line x1="210" y1="149" x2="170" y2="189" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="210" y1="149" x2="250" y2="189" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="170" y1="189" x2="195" y2="229" style="stroke:var(--border-2);stroke-width:1.5"/><circle cx="210" cy="149" r="14" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="210" y="153.6" text-anchor="middle" style="fill:var(--panel);font-size:12px">g</text><circle cx="170" cy="189" r="14" style="fill:var(--panel);stroke:var(--danger);stroke-width:2.5"/><text x="170" y="193.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">p</text><circle cx="250" cy="189" r="14" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="250" y="193.6" text-anchor="middle" style="fill:var(--panel);font-size:12px">u</text><circle cx="195" cy="229" r="14" style="fill:var(--panel);stroke:var(--danger);stroke-width:2.5"/><text x="195" y="233.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">z</text><line x1="300" y1="179" x2="380" y2="179" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rbc-a)"/><text x="340.0" y="171.0" text-anchor="middle" style="fill:var(--link);font-size:11px">左旋 p</text><line x1="500" y1="149" x2="460" y2="189" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="500" y1="149" x2="540" y2="189" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="460" y1="189" x2="440" y2="229" style="stroke:var(--border-2);stroke-width:1.5"/><circle cx="500" cy="149" r="14" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="500" y="153.6" text-anchor="middle" style="fill:var(--panel);font-size:12px">g</text><circle cx="460" cy="189" r="14" style="fill:var(--panel);stroke:var(--danger);stroke-width:2.5"/><text x="460" y="193.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">z</text><circle cx="540" cy="189" r="14" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="540" y="193.6" text-anchor="middle" style="fill:var(--panel);font-size:12px">u</text><circle cx="440" cy="229" r="14" style="fill:var(--panel);stroke:var(--danger);stroke-width:2.5"/><text x="440" y="233.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">p</text><text x="600.0" y="169.0" text-anchor="start" style="fill:var(--muted);font-size:11px">变成情况 3</text><text x="600.0" y="187.0" text-anchor="start" style="fill:var(--muted);font-size:11px">（p 成了 z）</text><text x="14.0" y="270.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">情况 3</text><line x1="210" y1="274" x2="170" y2="314" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="210" y1="274" x2="250" y2="314" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="170" y1="314" x2="150" y2="354" style="stroke:var(--border-2);stroke-width:1.5"/><circle cx="210" cy="274" r="14" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="210" y="278.6" text-anchor="middle" style="fill:var(--panel);font-size:12px">g</text><circle cx="170" cy="314" r="14" style="fill:var(--panel);stroke:var(--danger);stroke-width:2.5"/><text x="170" y="318.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">p</text><circle cx="250" cy="314" r="14" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="250" y="318.6" text-anchor="middle" style="fill:var(--panel);font-size:12px">u</text><circle cx="150" cy="354" r="14" style="fill:var(--panel);stroke:var(--danger);stroke-width:2.5"/><text x="150" y="358.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">z</text><line x1="300" y1="304" x2="380" y2="304" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#rbc-a)"/><text x="340.0" y="296.0" text-anchor="middle" style="fill:var(--link);font-size:11px">右旋 g、变色</text><line x1="490" y1="274" x2="450" y2="314" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="490" y1="274" x2="530" y2="314" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="530" y1="314" x2="560" y2="354" style="stroke:var(--border-2);stroke-width:1.5"/><circle cx="490" cy="274" r="14" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="490" y="278.6" text-anchor="middle" style="fill:var(--panel);font-size:12px">p</text><circle cx="450" cy="314" r="14" style="fill:var(--panel);stroke:var(--danger);stroke-width:2.5"/><text x="450" y="318.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">z</text><circle cx="530" cy="314" r="14" style="fill:var(--panel);stroke:var(--danger);stroke-width:2.5"/><text x="530" y="318.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">g</text><circle cx="560" cy="354" r="14" style="fill:var(--fg);stroke:var(--fg);stroke-width:1.5"/><text x="560" y="358.6" text-anchor="middle" style="fill:var(--panel);font-size:12px">u</text><text x="600.0" y="294.0" text-anchor="start" style="fill:var(--muted);font-size:11px">子树的根是黑色</text><text x="600.0" y="312.0" text-anchor="start" style="fill:var(--muted);font-size:11px">修复结束</text><line x1="10" y1="128" x2="690" y2="128" style="stroke:var(--border);stroke-width:1;stroke-dasharray:3 4"/><line x1="10" y1="253" x2="690" y2="253" style="stroke:var(--border);stroke-width:1;stroke-dasharray:3 4"/>
  </g>
</svg>
</figure>

```c
static void insertFixup(RBTree* t, RBNode* z)
{
    while (z->parent->color == RED) {
        RBNode* g = z->parent->parent;                  // 父亲是红色，一定不是根，所以祖父存在
        if (z->parent == g->left) {
            RBNode* uncle = g->right;
            if (uncle->color == RED) {                  // 情况 1：叔叔是红色，变色，问题上移到祖父
                z->parent->color = BLACK;
                uncle->color = BLACK;
                g->color = RED;
                z = g;
            } else {
                if (z == z->parent->right) {            // 情况 2：z 是右孩子，左旋父亲，变成情况 3
                    z = z->parent;
                    leftRotate(t, z);
                }
                z->parent->color = BLACK;               // 情况 3：z 是左孩子，右旋祖父，变色
                g->color = RED;
                rightRotate(t, g);
            }
        } else {
            /* 对称：父亲是右孩子，左右互换 */
        }
    }
    t->root->color = BLACK;                             // 根总是黑色
}
```

情况 1 只变色、不旋转，可能沿路径上移 O(log n) 次；情况 2、3 最多旋转两次，然后就结束了。所以**插入最多旋转 2 次**。最后一行把根涂黑：情况 1 可能一直上移到根，把根涂成了红色，涂回黑色只是让所有路径的黑色节点数同时加一，不破坏任何性质。

下面的演示可以逐个插入，观察每种情况。红色边框的节点是红色的。试试有序插入 1 到 10：

<div class="dsviz" data-viz="rb-insert"></div>

有序插入 1 到 10 以后的树（横着打印，右子树在上面）：

```
                        10(红)
                  9(黑)
            8(红)
                  7(黑)
      6(黑)
            5(黑)
4(黑)
            3(黑)
      2(黑)
            1(黑)
黑高 3，高度 5，旋转 5 次
```

普通的二叉搜索树会退化成高度为 10 的链表，红黑树的高度是 5。

## 5. 删除

删除比插入复杂得多，这里只讲思路，完整的代码在示例程序中。

1. 先按二叉搜索树的方法删除：没有孩子或只有一个孩子，直接用孩子顶替；有两个孩子，用后继节点 y 顶替它的位置（y 涂成被删节点的颜色），相当于删掉的是 y 原来的位置；
2. 如果真正被移走的节点是**红色**的，什么都不用做：黑色节点数没有变；
3. 如果是**黑色**的，经过它的路径少了一个黑色节点。顶替它的节点 x 被看成「多背了一层黑色」（双重黑色），然后看 x 的兄弟 w，分四种情况：

| 情况 | 兄弟 w | 处理 |
|---|---|---|
| 1 | 红色 | 旋转父亲，把兄弟变成黑色的，转入下面的情况 |
| 2 | 黑色，两个孩子都是黑色 | 兄弟涂红，多出的黑色交给父亲，继续向上 |
| 3 | 黑色，近侄子红、远侄子黑 | 旋转兄弟，转成情况 4 |
| 4 | 黑色，远侄子红 | 旋转父亲，变色，结束 |

情况 2 只变色、可能向上传递；情况 1、3、4 各旋转一次，而且 4 结束后不再继续。所以**删除最多旋转 3 次**。AVL 树的删除最坏要旋转 O(log n) 次。

正确性怎么检查？程序随机插入、删除 20 万次（key 在 1 到 1999 之间，所以会频繁地删除已有的元素），每 1000 次检查一遍：

```c
// 检查五条性质，返回黑高；有问题时返回 -1
static int checkRB(const RBNode* n, int lo, int hi, int* count)
{
    if (n == NIL) return 1;
    if (n->key <= lo || n->key >= hi) return -1;                              // 二叉搜索树
    if (n->color == RED && (n->left->color == RED || n->right->color == RED)) return -1;   // 红色节点的孩子是黑色
    ++*count;
    int l = checkRB(n->left, lo, n->key, count), r = checkRB(n->right, n->key, hi, count);
    if (l < 0 || r < 0 || l != r) return -1;                                  // 每条路径的黑色节点数相同
    return l + (n->color == BLACK);
}
```

同时和一个布尔数组对比每个 key 是否存在。结果：

```
随机插入、删除 20 万次，每 1000 次检查一遍五条性质和全部元素：通过
```

程序也在 AddressSanitizer 下运行过，没有内存错误。

## 6. 和 AVL 树比较

100 万个数，分别有序插入和随机插入，再查找每个数、删除一半（`-O2`，AVL 树和《AVL 树》一文的写法相同，加上了删除）：

| | | 高度 | 插入的旋转次数 | 插入 | 查找 | 删除一半的旋转 | 删除一半 |
|---|---|---|---|---|---|---|---|
| 有序插入 | 红黑树 | **37** | 999,963 | 0.084 s | 0.027 s | 249,999 | 0.012 s |
| | AVL 树 | **20** | 999,980 | 0.057 s | 0.026 s | 249,996 | 0.021 s |
| 随机插入 | 红黑树 | 24 | **582,685** | 0.135 s | 0.060 s | 219,503 | 0.061 s |
| | AVL 树 | 24 | **698,528** | 0.158 s | 0.062 s | 211,376 | 0.123 s |

（AVL 树的双旋转记为两次。）

- **有序插入**是红黑树最「吃亏」的情况：高度 37，接近上限 2log₂(n+1) ≈ 40；AVL 树是 20。但查找时间几乎一样：37 层和 20 层的差别，在 100 万个节点、大部分访问都要读内存的情况下，被内存延迟掩盖了；
- **随机插入**时两者高度相同（24），红黑树的旋转少了 17%，插入也快一些；
- **删除**：红黑树快了一倍。部分原因是删除时旋转少，部分原因是这里的 AVL 树用递归实现、沿路径更新每个节点的高度。

两者的差别没有想象中那么大。实际中选择红黑树，更多是因为它**插入、删除的旋转次数有常数上限**，最坏情况更可控；而 AVL 树在「查找远多于修改」的场景中更有优势。

## 7. 另一个视角：2-3-4 树

红黑树有一个很漂亮的解释：把每个红色节点和它的黑色父亲「合并」成一个大节点，红黑树就变成了一棵 **2-3-4 树**：每个节点有 1 到 3 个 key、2 到 4 个孩子，并且**所有的叶子都在同一层**（这对应性质 5）。

- 插入时情况 1（叔叔是红色，变色上移）对应 2-3-4 树中一个满了的节点（4 个孩子）分裂、中间的 key 上移；
- 情况 2、3（旋转）对应把新 key 放进一个还有空位的节点。

2-3-4 树是 B 树的一种。数据库的 B+ 树（《B+ 树索引》）也是这个思路：节点更大，每个节点有几百个孩子，树只有三四层。

## 8. 常见错误

1. **新节点涂成黑色**。性质 5 立即被破坏，修复非常困难。
2. **忘了维护 `parent` 指针**。旋转、`transplant` 时漏掉一个，之后往上走时就走错了。
3. **没有用哨兵，访问空指针的颜色**。`uncle->color` 中 uncle 可能是空的。要么用哨兵，要么写一个 `colorOf(n)` 函数把空指针当成黑色。
4. **情况 2 之后忘了更新 z**。左旋父亲以后，z 要指向原来的父亲（现在在下面），再按情况 3 处理。
5. **最后忘了把根涂黑**。
6. **删除时哨兵的 `parent` 没有设置**。删除修复要从 x 往上走，而 x 可能就是 NIL，`transplant` 中要给它设置 `parent`（《算法导论》的写法正是利用了这一点）。

## 9. 小结

- 红黑树用颜色和五条性质保证最长路径不超过最短路径的 2 倍，高度不超过 2log₂(n+1)；
- 插入：新节点涂红，只可能违反「红色不相邻」；叔叔红色就变色上移，叔叔黑色就旋转（最多 2 次）后结束；
- 删除：移走的是黑色节点时，按兄弟的颜色分四种情况修复，最多旋转 3 次；
- 和 AVL 树相比，红黑树稍高，但插入删除的调整更少、有常数上限；实测中两者的查找速度几乎一样；
- 红黑树等价于 2-3-4 树。

## 10. 练习

1. 一棵红黑树，黑高是 3。它最少有几个节点？最多有几个？
2. 为什么插入时，情况 3 处理完以后可以直接结束，而情况 1 要继续向上？
3. 有序插入时，为什么红黑树的高度会比随机插入时高这么多（37 对 24）？

<details>
<summary>点击查看答案</summary>

1. 最少：每条路径上只有黑色节点，是一棵满二叉树，2³ - 1 = 7 个节点。最多：每条路径上黑红交替，从根（黑）开始高度为 6，是一棵高度为 6 的满二叉树，2⁶ - 1 = 63 个节点。

2. 情况 3 旋转、变色以后，这棵子树的新根是黑色的，并且这棵子树中每条路径的黑色节点数和修复之前相同。对上面的节点来说，这棵子树「看起来」和插入之前一样（根是黑色，黑高不变），不会再有冲突。情况 1 把祖父涂成了红色，祖父的父亲可能也是红色，问题只是从下面移到了上面。

3. 有序插入时，新节点总是插在最右边，情况 1（变色）和情况 3（旋转）交替发生。左边的子树早早地就稳定了，并且全是黑色；右边的路径上红黑交替。最终最左的路径全是黑色（最短），最右的路径红黑交替（最长，接近两倍）。随机插入时各个方向都有插入，树更均衡。这也说明红黑树的「2 倍」上限在实际中是可能接近的。

</details>
