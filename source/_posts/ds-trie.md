---
title: Trie（字典树）
date: 2026-11-06 10:20:00
updated: 2026-11-06 10:20:00
categories:
  - 数据结构
  - 树
tags:
  - 数据结构
  - 树
  - 字符串
order: 7
top_group_list: true
description: 输入法的联想、搜索框的自动补全，要回答的问题是「以这个前缀开头的词有哪些」。Trie 把字符串拆成字符，共同的前缀只存一次，查找的时间只和字符串的长度有关。本文实现 Trie，用系统词典的 21 万个单词实测：查找比二分查找快 7 倍多，但要占 143 MB，而单词本身只有 2 MB；再换一种节点的写法，内存降到 16 MB，查找慢了不到 3 倍。
disableNunjucks: true
viz: [ds-extra]
---

> **本文目标**：理解 Trie 的结构；会实现插入、查找、前缀计数和按前缀列出单词；知道 Trie 的时间优势和空间代价，以及几种节省空间的节点写法。
>
> **前置知识**：《二叉树与遍历》、《哈希表》。本文的完整代码在 [projects/ds-extra/trie.c](https://github.com/Lucakes-bw/Lucakes-bw.github.io/tree/main/projects/ds-extra/trie.c)。

## 1. 前缀问题

哈希表能回答「这个单词在不在」，平均 O(1)；但回答不了「以 pre 开头的单词有哪些」，因为哈希会把相近的字符串打散（[地铁项目的第五篇](/posts/ds-project-metro-m5/)讨论过这一点，那里用的是排序加二分查找）。

**Trie**（读作 try，也叫字典树、前缀树）专门为前缀设计：

- 每条边代表一个字符；
- 从根到某个节点的路径，就是一个前缀；
- 以这个前缀开头的所有单词，都在这个节点的子树中。

插入 tea、ten、to、inn、in、a 以后：

<figure class="diagram">
<svg viewBox="0 0 700 270" role="img" aria-label="插入 tea、ten、to、inn、in、a 后的 Trie：边上是字符，节点中的数字是经过它的单词数 pass，绿色节点表示有单词在这里结束；tea 和 ten 共用 te，inn 和 in 共用 in">
  <defs><marker id="tsh-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="tsh-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <line x1="330" y1="28" x2="170" y2="92" style="stroke:var(--border-2);stroke-width:1.5"/><text x="243.0" y="62.0" text-anchor="end" style="fill:var(--link);font-size:12px">t</text><line x1="330" y1="28" x2="390" y2="92" style="stroke:var(--border-2);stroke-width:1.5"/><text x="367.0" y="62.0" text-anchor="start" style="fill:var(--link);font-size:12px">i</text><line x1="330" y1="28" x2="520" y2="92" style="stroke:var(--border-2);stroke-width:1.5"/><text x="432.0" y="62.0" text-anchor="start" style="fill:var(--link);font-size:12px">a</text><line x1="170" y1="92" x2="120" y2="156" style="stroke:var(--border-2);stroke-width:1.5"/><text x="138.0" y="126.0" text-anchor="end" style="fill:var(--link);font-size:12px">e</text><line x1="170" y1="92" x2="230" y2="156" style="stroke:var(--border-2);stroke-width:1.5"/><text x="207.0" y="126.0" text-anchor="start" style="fill:var(--link);font-size:12px">o</text><line x1="390" y1="92" x2="390" y2="156" style="stroke:var(--border-2);stroke-width:1.5"/><text x="397.0" y="126.0" text-anchor="start" style="fill:var(--link);font-size:12px">n</text><line x1="120" y1="156" x2="80" y2="220" style="stroke:var(--border-2);stroke-width:1.5"/><text x="93.0" y="190.0" text-anchor="end" style="fill:var(--link);font-size:12px">a</text><line x1="120" y1="156" x2="160" y2="220" style="stroke:var(--border-2);stroke-width:1.5"/><text x="147.0" y="190.0" text-anchor="start" style="fill:var(--link);font-size:12px">n</text><line x1="390" y1="156" x2="390" y2="220" style="stroke:var(--border-2);stroke-width:1.5"/><text x="397.0" y="190.0" text-anchor="start" style="fill:var(--link);font-size:12px">n</text><circle cx="330" cy="28" r="15" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="330" y="32.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">6</text><circle cx="170" cy="92" r="15" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="170" y="96.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">3</text><circle cx="390" cy="92" r="15" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="390" y="96.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">2</text><circle cx="520" cy="92" r="15" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="520" y="96.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">1</text><circle cx="120" cy="156" r="15" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="120" y="160.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">2</text><circle cx="230" cy="156" r="15" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="230" y="160.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">1</text><circle cx="390" cy="156" r="15" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="390" y="160.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">2</text><circle cx="80" cy="220" r="15" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="80" y="224.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">1</text><circle cx="160" cy="220" r="15" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="160" y="224.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">1</text><circle cx="390" cy="220" r="15" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="390" y="224.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">1</text><text x="520.0" y="122.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">a</text><text x="230.0" y="186.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">to</text><text x="80.0" y="250.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">tea</text><text x="160.0" y="250.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">ten</text><text x="390.0" y="250.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">inn</text><text x="368.0" y="160.0" text-anchor="end" style="fill:var(--muted);font-size:11px">in</text><text x="360.0" y="22.0" text-anchor="start" style="fill:var(--muted);font-size:11px">root</text><text x="500.0" y="160.0" text-anchor="start" style="fill:var(--fg);font-size:12px">countPrefix(&quot;te&quot;) = 2：</text><text x="500.0" y="178.0" text-anchor="start" style="fill:var(--muted);font-size:11px">走到 te，读它的 pass</text><text x="500.0" y="208.0" text-anchor="start" style="fill:var(--fg);font-size:12px">contains(&quot;i&quot;) = 0：</text><text x="500.0" y="226.0" text-anchor="start" style="fill:var(--muted);font-size:11px">路径存在，但 i 不是绿色</text>
  </g>
</svg>
</figure>

tea 和 ten 共用了前缀 te，inn 和 in 共用了 in。in 本身也是一个单词，但它的节点下面还有 inn，所以每个节点要有一个标记 `end`，表示「有单词在这里结束」。

## 2. 实现

### 2.1 节点

单词只含小写字母时，最简单的写法是每个节点 26 个孩子指针：

```c
typedef struct TrieNode {
    struct TrieNode* child[26];
    int pass;                               // 经过这个节点的单词数：以这个前缀开头的单词有几个
    bool end;                               // 有没有单词在这里结束
} TrieNode;
```

找某个字符对应的孩子，直接用 `child[c - 'a']`，O(1)。`pass` 记录有多少个单词经过这个节点，用来快速回答「以这个前缀开头的单词有几个」。

### 2.2 插入和查找

```c
static bool trieInsert(TrieNode* root, const char* w)
{
    TrieNode* x = root;
    for (const char* p = w; *p; ++p) {
        int c = *p - 'a';
        if (!x->child[c]) x->child[c] = newTrieNode();  // 没有这条边就新建
        x = x->child[c];
    }
    if (x->end) return false;               // 已经有这个单词了，不重复计数
    x->end = true;
    for (const char* p = w; ; ++p) {        // 确认是新单词以后，再沿路径把 pass 加一
        root->pass++;
        if (!*p) break;
        root = root->child[*p - 'a'];
    }
    return true;
}

// 走到前缀对应的节点，不存在返回 NULL
static TrieNode* walk(TrieNode* root, const char* prefix)
{
    TrieNode* x = root;
    for (const char* p = prefix; *p && x; ++p) x = x->child[*p - 'a'];
    return x;
}

static bool trieContains(TrieNode* root, const char* w)
{
    TrieNode* x = walk(root, w);
    return x && x->end;                     // 路径存在，并且有单词在这里结束
}

static int countPrefix(TrieNode* root, const char* prefix)
{
    TrieNode* x = walk(root, prefix);
    return x ? x->pass : 0;
}
```

插入和查找都只沿着单词的字符走一遍，**时间是 O(单词的长度)**，和词典中有多少个单词无关。

注意 `contains("i")` 要返回 false：路径存在（i 是 in 的前缀），但没有单词在那里结束。

```
以 te 开头：2 个；以 t 开头：3 个；以 x 开头：0 个
contains(in)=1 contains(i)=0 contains(tea)=1
```

<div class="dsviz" data-viz="trie"></div>

### 2.3 按前缀列出单词

先走到前缀的节点，再对它的子树做深度优先遍历。孩子按字母顺序访问，列出的单词也就是按字母顺序的：

```c
static int listWords(TrieNode* x, char* buf, int len, int limit)
{
    int printed = 0;
    if (x->end) {
        buf[len] = '\0';
        printf("%s ", buf);
        ++printed;
    }
    for (int c = 0; c < 26 && printed < limit; ++c) {
        if (!x->child[c]) continue;
        buf[len] = (char)('a' + c);
        printed += listWords(x->child[c], buf, len + 1, limit - printed);
    }
    return printed;
}
```

`buf` 中保存着从根到当前节点的路径，也就是当前的前缀。这就是自动补全的基本原理。

## 3. 用真实的词典测一测

macOS 和很多 Linux 发行版都带有一个英文词典 `/usr/share/dict/words`。取其中全是小写字母的单词：

```
单词 210773 个（全小写），不重复的 210773 个，共 2039158 个字母
建树 0.022 秒；查找 2107730 次：Trie 0.038 秒，排序数组二分查找 0.296 秒
以 pre   开头： 3018 个；前几个：preabdomen preabsorb preabsorbent preabstract preabundance
以 inter 开头： 1181 个；前几个：inter interabsorption interacademic interaccessory interaccuse
以 zy    开头：  115 个；前几个：zyga zygadenine zygaenid zygal zygantra
以 xylo  开头：   47 个；前几个：xylobalsamum xylocarp xylocarpous xylocopid xylogen
```

查找 210 万次，Trie 比在排好序的数组上二分查找快了 7 倍多。二分查找要比较 log₂21 万 ≈ 18 次，每次是一次完整的字符串比较；Trie 只沿着单词的字符走一遍，每一步是一次数组下标访问。

但是看一下内存：

```
Trie 节点 696835 个，每个节点 216 字节，共 143.5 MB；单词本身只有 2.1 MB
孩子指针一共 18117710 个，实际用到 696834 个（3.8%）
```

**143.5 MB 存 2.1 MB 的单词**，将近 70 倍。每个节点有 26 个指针（208 字节），但除了根附近的几层，大部分节点只有一两个孩子，**96% 的指针都是空的**。这是这种写法的根本问题：字符集越大越严重，如果要支持 Unicode（几万个字符），每个节点一个完整的数组是不可能的。

## 4. 节省空间

### 4.1 孩子用链表连起来

每个节点只存「第一个孩子」和「下一个兄弟」两个指针，同一个父亲的孩子按字母顺序连成一个链表（这就是《树》中把任意的树转成二叉树的「左孩子右兄弟」表示法）：

```c
typedef struct LNode {
    struct LNode *child, *sibling;          // 第一个孩子；同一个父亲的下一个孩子
    char c;
    bool end;
} LNode;
```

找一个字符对应的孩子，要沿着兄弟链表顺序查找。实测：

```
孩子用链表：节点 696834 个，每个 24 字节，共 15.9 MB；建树 0.015 秒，查找 0.107 秒
```

内存从 143.5 MB 降到 **15.9 MB**，只有原来的九分之一；查找从 0.038 秒变成 0.107 秒，慢了不到 3 倍，仍然比二分查找快。这就是典型的空间换时间：数组写法用空间换来了 O(1) 找孩子。

<figure class="diagram">
<svg viewBox="0 0 700 300" role="img" aria-label="两种节点的写法。上：每个节点 26 个孩子指针，te 节点只用到 a 和 n 两格，其余 24 格是空的。下：孩子用链表连起来，te 的第一个孩子是 a，a 的兄弟是 n，每个节点只有两个指针。实测 21 万个单词：数组写法 143.5 MB、查找 0.038 秒；链表写法 15.9 MB、查找 0.107 秒；单词本身 2.1 MB">
  <defs><marker id="tsp-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="tsp-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="14.0" y="24.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">数组写法：te 节点的 child[26]</text><rect x="30" y="36" width="22" height="26" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="41.0" y="76.0" text-anchor="middle" style="fill:var(--link);font-size:10px">a</text><rect x="54" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="65.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">b</text><rect x="78" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="89.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">c</text><rect x="102" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="113.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">d</text><rect x="126" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="137.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">e</text><rect x="150" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="161.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">f</text><rect x="174" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="185.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">g</text><rect x="198" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="209.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">h</text><rect x="222" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="233.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">i</text><rect x="246" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="257.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">j</text><rect x="270" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="281.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">k</text><rect x="294" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="305.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">l</text><rect x="318" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="329.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">m</text><rect x="342" y="36" width="22" height="26" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="353.0" y="76.0" text-anchor="middle" style="fill:var(--link);font-size:10px">n</text><rect x="366" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="377.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">o</text><rect x="390" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="401.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">p</text><rect x="414" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="425.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">q</text><rect x="438" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="449.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">r</text><rect x="462" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="473.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">s</text><rect x="486" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="497.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">t</text><rect x="510" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="521.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">u</text><rect x="534" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="545.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">v</text><rect x="558" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="569.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">w</text><rect x="582" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="593.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">x</text><rect x="606" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="617.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">y</text><rect x="630" y="36" width="22" height="26" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="641.0" y="76.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">z</text><text x="30.0" y="98.0" text-anchor="start" style="fill:var(--muted);font-size:11px">26 × 8 = 208 字节的指针，只用到 2 个；整棵树中 96% 的指针是空的</text><text x="14.0" y="134.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">链表写法：第一个孩子 + 下一个兄弟</text><rect x="40" y="148" width="64" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="72.0" y="168.9" text-anchor="middle" style="fill:var(--fg);font-size:13px">te</text><rect x="150" y="198" width="64" height="32" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="182.0" y="218.9" text-anchor="middle" style="fill:var(--fg);font-size:13px">tea</text><rect x="270" y="198" width="64" height="32" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="302.0" y="218.9" text-anchor="middle" style="fill:var(--fg);font-size:13px">ten</text><line x1="72" y1="180" x2="150" y2="210" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#tsp-a)"/><text x="96.0" y="206.0" text-anchor="middle" style="fill:var(--link);font-size:11px">child</text><line x1="214" y1="214" x2="268" y2="214" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#tsp-a)"/><text x="241.0" y="208.0" text-anchor="middle" style="fill:var(--link);font-size:11px">sibling</text><text x="360.0" y="218.0" text-anchor="start" style="fill:var(--muted);font-size:11px">每个节点 24 字节；找孩子要沿兄弟链表走</text><text x="90.0" y="257.0" text-anchor="end" style="fill:var(--fg);font-size:11px">数组写法</text><rect x="100" y="246" width="300.0" height="13" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="408.0" y="257.0" text-anchor="start" style="fill:var(--muted);font-size:11px">143.5 MB，查找 0.038 秒</text><text x="90.0" y="275.0" text-anchor="end" style="fill:var(--fg);font-size:11px">链表写法</text><rect x="100" y="264" width="33.2" height="13" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="141.2" y="275.0" text-anchor="start" style="fill:var(--muted);font-size:11px">15.9 MB，查找 0.107 秒</text><text x="90.0" y="293.0" text-anchor="end" style="fill:var(--fg);font-size:11px">单词本身</text><rect x="100" y="282" width="4.4" height="13" rx="2" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="112.4" y="293.0" text-anchor="start" style="fill:var(--muted);font-size:11px">2.1 MB（排序数组二分：0.296 秒）</text>
  </g>
</svg>
</figure>

### 4.2 其他的写法

- **孩子用哈希表或有序数组**：适合大字符集（中文），孩子多的节点用哈希表，孩子少的用小数组；
- **压缩 Trie**（基数树，radix tree）：只有一个孩子的连续节点合并成一个，边上存一个字符串而不是一个字符。英文单词的后半部分往往没有分叉（xylocarpous 的 carpous），合并以后节点数大大减少。Linux 内核的 IP 路由表、Redis 的 Stream、很多 HTTP 框架的路由都用基数树；
- **双数组 Trie**：把整棵树压缩进两个整数数组，查找很快、内存很省，但插入很麻烦，适合词典固定不变的场景（比如分词、输入法的词库）。

## 5. Trie 适合什么

| 需求 | Trie | 哈希表 | 排序数组 + 二分 |
|---|---|---|---|
| 精确查找 | O(长度) | 平均 O(长度)（算哈希） | O(长度 × log n) |
| 前缀查询、自动补全 | ✓，直接走到子树 | ✗ | ✓，两次二分 |
| 按字典序遍历 | ✓ | ✗ | ✓ |
| 最长公共前缀 | ✓ | ✗ | 比较首尾两个 |
| 内存 | 大（取决于节点的写法） | 中 | 小 |

除了自动补全，Trie 的典型用途还有：**最长前缀匹配**（IP 路由：找和目标地址前缀最长的那条路由）、**敏感词过滤**（把所有敏感词建成 Trie，扫描文本时同时匹配所有的词；再加上类似 KMP 的失配指针，就是 Aho-Corasick 自动机）、**统计词频**（每个节点记一个计数）。

## 6. 常见错误

1. **没有 `end` 标记**，用「是不是叶子」判断单词结束。in 和 inn 都在词典中时，in 不是叶子，会被漏掉。
2. **字符不在 a～z 中**，`*p - 'a'` 成了负数或者大于 25，数组越界。插入之前过滤，或者用更大的字符集。
3. **重复插入同一个单词时 `pass` 加了两次**，前缀计数偏大。先确认是新单词，再更新计数（本文的写法）。
4. **删除单词时直接释放节点**，把别的单词的前缀也删掉了。删除时只把 `end` 设为 false，路径上 `pass` 减一，`pass` 变成 0 的节点才能释放。
5. **低估了内存**。每个节点一个完整的孩子数组，节点数一多，内存是数据本身的几十倍。

## 7. 练习

- [实现 Trie（前缀树）](/problems/implement-trie-prefix-tree/)：本文的插入、查找、前缀判断
- [单词拆分](/problems/word-break/)：用 Trie 加速「从位置 i 开始，哪些单词能匹配」

思考题：

1. 给一组字符串，求它们的最长公共前缀。用 Trie 怎么做？
2. 在一个 m × n 的字母网格中找出所有在词典中出现的单词（相邻的格子可以连成一个单词）。为什么用 Trie 比逐个单词搜索快？
3. 实现删除操作，并且释放不再需要的节点。

<details>
<summary>点击查看答案</summary>

1. 把所有字符串插入 Trie，从根开始往下走，只要当前节点只有一个孩子、并且不是某个单词的结尾，就继续往下走。停下来时走过的路径就是最长公共前缀。（不用 Trie 的话，排序后比较第一个和最后一个字符串更简单。）

2. 逐个单词搜索时，每个单词都要在网格中从每个格子开始回溯一遍。用 Trie，从每个格子出发回溯一次，同时在 Trie 上往下走：网格中走到的路径不是任何单词的前缀时（Trie 中没有这条边），立即剪枝。所有单词共享同一次回溯，而且共同的前缀只走一次。这是回溯（《递归与回溯》）和 Trie 的结合。

3. 先 `walk` 到单词的节点，确认 `end` 为 true；把 `end` 设为 false；再从根沿路径往下，每个节点的 `pass` 减一，如果某个孩子的 `pass` 减到了 0，说明它的子树中已经没有任何单词，把这棵子树整个释放，并把父亲的孩子指针设为 NULL，然后停止。

</details>
