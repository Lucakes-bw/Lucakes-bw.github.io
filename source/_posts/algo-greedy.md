---
title: 贪心
date: 2026-11-05 10:40:00
updated: 2026-11-05 10:40:00
categories:
  - 数据结构
  - 算法设计
tags:
  - 算法
  - 贪心
order: 5
description: 贪心是每一步都做当前看起来最好的选择，从不回头。它写起来最简单，也最容易错。本文用活动选择问题比较三种看起来都合理的贪心：最早开始、最短、最早结束。随机 10000 组数据和暴力枚举对比，前两种分别错了 4751 组和 586 组，只有最早结束一组都没错；然后用交换论证说明它为什么是对的，再看跳跃游戏和零钱兑换中贪心的对与错。
disableNunjucks: true
viz: [ds-algo]
notebook: true
---

> **本文目标**：理解贪心算法的思路，以及它和动态规划的区别；会用反例否定一个贪心策略；会用「交换论证」证明一个贪心策略是对的；会用暴力枚举在小数据上检验贪心。
>
> **前置知识**：[动态规划（一）](/posts/algo-dp-basics/)（零钱兑换中贪心出错的例子）。

## 1. 贪心是什么

**贪心**：每一步都做当前看起来最好的选择，做了就不再改变，期望最后得到全局最好的结果。

和动态规划比较：动态规划考虑了所有的子问题，再从中选最好的；贪心只走一条路。所以贪心通常更快、代码更短，但**只有在问题有特殊的结构时才是对的**。第 2 篇的零钱兑换中，面值 {1, 3, 4} 凑 6，贪心先拿 4，就错过了 3 + 3。

使用贪心最大的难点不是写代码，而是**判断它对不对**。

## 2. 活动选择

> 有 n 个活动，每个活动占用一段时间 [开始, 结束)。一个会议室同一时间只能举办一个活动。最多能安排几个活动？

每次选哪个活动？有好几种「看起来很合理」的贪心策略：

1. **最早开始**：先选开始得最早的；
2. **最短**：先选持续时间最短的，占用的时间少，留给别人的多；
3. **最早结束**：先选结束得最早的，会议室能尽快空出来。

### 2.1 用反例否定

一个贪心策略是错的，只需要一个反例：

<figure class="diagram">
<svg viewBox="0 0 700 230" role="img" aria-label="活动选择的两个反例：例一中最早开始的贪心选了 [0,10)，只有 1 个，而 [1,3)、[4,6)、[7,9) 可以选 3 个；例二中最短的贪心选了 [4,7)，只有 1 个，而 [0,5) 和 [6,11) 可以选 2 个">
  <defs><marker id="iv-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="iv-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="14.0" y="24.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">例一</text><rect x="150" y="10" width="258" height="20" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="279.0" y="24.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">[0,10)</text><text x="418.0" y="25.0" text-anchor="start" style="fill:var(--danger);font-size:11px">最早开始先选它 → 只有 1 个</text><rect x="176" y="34" width="50" height="20" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="201.0" y="48.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">[1,3)</text><rect x="254" y="58" width="50" height="20" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="279.0" y="72.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">[4,6)</text><rect x="332" y="82" width="50" height="20" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="357.0" y="96.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">[7,9)</text><text x="392.0" y="97.0" text-anchor="start" style="fill:var(--accent);font-size:11px">最早结束：3 个</text><text x="14.0" y="144.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">例二</text><rect x="150" y="130" width="128" height="20" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="214.0" y="144.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">[0,5)</text><rect x="254" y="154" width="76" height="20" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="292.0" y="168.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">[4,7)</text><text x="340.0" y="169.0" text-anchor="start" style="fill:var(--danger);font-size:11px">最短先选它 → 只有 1 个</text><rect x="306" y="178" width="128" height="20" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="370.0" y="192.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">[6,11)</text><text x="444.0" y="193.0" text-anchor="start" style="fill:var(--accent);font-size:11px">最早结束：2 个</text><text x="150.0" y="224.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">0</text><text x="202.0" y="224.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">2</text><text x="254.0" y="224.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">4</text><text x="306.0" y="224.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">6</text><text x="358.0" y="224.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">8</text><text x="410.0" y="224.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">10</text>
  </g>
</svg>
</figure>

- **最早开始**：活动 [0, 10)、[1, 3)、[4, 6)、[7, 9)。最早开始的是 [0, 10)，选了它，其他三个都和它冲突，只能安排 1 个；而选后面三个可以安排 3 个；
- **最短**：活动 [0, 5)、[4, 7)、[6, 11)。最短的是 [4, 7)，选了它，另外两个都和它冲突，只能安排 1 个；而 [0, 5) 和 [6, 11) 可以同时安排。

把三种策略写成同一个函数：按某种顺序排好以后，和已经选的都不重叠就选：

<!-- run: hidden -->
```c
typedef struct {
    int start, end;                         // 左闭右开 [start, end)
} Interval;

// 主关键字相同时再比较另一端，排序结果和 qsort 的实现无关（不同的 C 库结果一样）
static int byEnd(const void* a, const void* b)
{
    const Interval *x = a, *y = b;
    return x->end != y->end ? x->end - y->end : x->start - y->start;
}
static int byStart(const void* a, const void* b)
{
    const Interval *x = a, *y = b;
    return x->start != y->start ? x->start - y->start : x->end - y->end;
}
static int byLength(const void* a, const void* b)
{
    const Interval *x = a, *y = b;
    int lx = x->end - x->start, ly = y->end - y->start;
    return lx != ly ? lx - ly : x->start - y->start;
}

// 按某种顺序排好以后，能放就放
static int greedy(Interval* iv, int n, int (*cmp)(const void*, const void*), bool print)
{
    Interval tmp[32];
    memcpy(tmp, iv, sizeof(Interval) * (size_t)n);
    qsort(tmp, (size_t)n, sizeof(Interval), cmp);
    int count = 0;
    Interval picked[32];
    for (int i = 0; i < n; ++i) {
        bool ok = true;
        for (int k = 0; k < count; ++k)     // 和已经选的都不重叠才选
            if (tmp[i].start < picked[k].end && picked[k].start < tmp[i].end) ok = false;
        if (ok) picked[count++] = tmp[i];
    }
    if (print) {
        for (int k = 0; k < count; ++k) printf("[%d,%d) ", picked[k].start, picked[k].end);
        printf("共 %d 个\n", count);
    }
    return count;
}
```

```c
Interval ex1[] = {{0, 10}, {1, 3}, {4, 6}, {7, 9}};          // 最早开始：选了 [0,10)
Interval ex2[] = {{0, 5}, {4, 7}, {6, 11}};                   // 最短：选了 [4,7)
printf("最早开始，例一："); greedy(ex1, 4, byStart, true);
printf("最早结束，例一："); greedy(ex1, 4, byEnd, true);
printf("最短，例二："); greedy(ex2, 3, byLength, true);
printf("最早结束，例二："); greedy(ex2, 3, byEnd, true);
```

```
最早开始，例一：[0,10) 共 1 个
最早结束，例一：[1,3) [4,6) [7,9) 共 3 个
最短，例二：[4,7) 共 1 个
最早结束，例二：[0,5) [6,11) 共 2 个
```

### 2.2 用暴力枚举检验

找不到反例，不代表策略是对的，可能只是没想到。一个很实用的办法是**在小数据上和暴力枚举对比**：随机生成 10000 组数据，每组 12 个活动，暴力枚举所有 2¹² 个子集，求出真正的最大值，再看三种贪心各错了几组：

<!-- run: hidden -->
```c
static int brute(const Interval* iv, int n)  // 枚举所有子集
{
    int best = 0;
    for (int mask = 0; mask < (1 << n); ++mask) {
        int cnt = __builtin_popcount((unsigned)mask);
        if (cnt <= best) continue;
        bool ok = true;
        for (int i = 0; i < n && ok; ++i)
            for (int j = i + 1; j < n && ok; ++j)
                if ((mask >> i & 1) && (mask >> j & 1) && iv[i].start < iv[j].end && iv[j].start < iv[i].end) ok = false;
        if (ok) best = cnt;
    }
    return best;
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
int wrongStart = 0, wrongLength = 0, wrongEnd = 0;
for (int t = 0; t < 10000; ++t) {
    Interval iv[12];
    for (int i = 0; i < 12; ++i) {
        iv[i].start = randInt(0, 40);
        iv[i].end = iv[i].start + randInt(1, 15);
    }
    int best = brute(iv, 12);
    if (greedy(iv, 12, byStart, false) != best) ++wrongStart;
    if (greedy(iv, 12, byLength, false) != best) ++wrongLength;
    if (greedy(iv, 12, byEnd, false) != best) ++wrongEnd;
}
printf("最早开始：错 %d 组；最短：错 %d 组；最早结束：错 %d 组\n", wrongStart, wrongLength, wrongEnd);
```

```
最早开始：错 4751 组；最短：错 586 组；最早结束：错 0 组
```

「最短」的反例并不容易想到，但在随机数据中，它错了 6%；「最早结束」一组都没错。这说明它很可能是对的，但还不是证明。

下面的演示按结束时间排序，依次处理每个活动：和上一个选中的不冲突就选。

<div class="dsviz" data-viz="algo-greedy"></div>

### 2.3 交换论证：为什么最早结束是对的

证明贪心正确的一种常用方法是**交换论证**：拿任意一个最优解，证明可以一步步把它换成贪心的解，而活动的个数不会减少。

设贪心按结束时间选出了 g₁, g₂, …, gₖ，某个最优解按结束时间排列是 o₁, o₂, …, oₘ（m ≥ k）。

1. g₁ 是所有活动中结束最早的，所以 g₁ 的结束 ≤ o₁ 的结束。把最优解中的 o₁ 换成 g₁：g₁ 结束得不比 o₁ 晚，不会和 o₂ 冲突，换完以后仍然是一个合法的解，个数不变；
2. 现在最优解和贪心解的第一个活动相同了。对剩下的活动（开始时间不早于 g₁ 结束的那些），同样的道理，可以把 o₂ 换成 g₂……
3. 一直换下去，最优解的前 k 个都换成了贪心的 g₁ … gₖ。如果 m > k，说明在 gₖ 结束以后还有活动 oₖ₊₁ 不冲突，但贪心在 gₖ 之后没有找到任何不冲突的活动，矛盾。所以 m = k。

核心是第一步：**结束最早的活动，总可以出现在某个最优解中**。这种「贪心的选择是安全的」的性质，叫作**贪心选择性质**。

按结束时间排序后，只需要和上一个选中的活动比较，复杂度是 O(n log n)：

```c
static int earliestEnd(Interval* iv, int n)
{
    qsort(iv, (size_t)n, sizeof(Interval), byEnd);
    int count = 0, lastEnd = -1000000;
    for (int i = 0; i < n; ++i)
        if (iv[i].start >= lastEnd) {           // 和上一个选中的不冲突
            ++count;
            lastEnd = iv[i].end;
        }
    return count;
}
```

用 2.1 节的两个例子试一下：

```c
printf("例一 %d 个，例二 %d 个\n", earliestEnd(ex1, 4), earliestEnd(ex2, 3));
```

```
例一 3 个，例二 2 个
```

## 3. 跳跃游戏

> 数组 nums 中，nums[i] 表示在位置 i 最多能往前跳几步。从位置 0 出发，能不能到达最后一个位置？

[2, 3, 1, 1, 4]：0 → 1 → 4，能。[3, 2, 1, 0, 4]：不管怎么跳，都会停在位置 3（那里只能跳 0 步），不能。

可以用 DP（dp[i] = 能否到达 i），但有一个更简单的贪心：维护**目前能到达的最远位置**。从左到右扫描，只要当前位置 i 能到达（i ≤ farthest），就用 i + nums[i] 更新 farthest：

```c
static bool canJump(const int* nums, int n)
{
    int farthest = 0;                       // 目前能到达的最远位置
    for (int i = 0; i < n; ++i) {
        if (i > farthest) return false;     // 到不了 i
        if (i + nums[i] > farthest) farthest = i + nums[i];
    }
    return true;
}
```

```c
int j1[] = {2, 3, 1, 1, 4}, j2[] = {3, 2, 1, 0, 4};
printf("[2,3,1,1,4]：%s\n", canJump(j1, 5) ? "能" : "不能");
printf("[3,2,1,0,4]：%s\n", canJump(j2, 5) ? "能" : "不能");
```

```
[2,3,1,1,4]：能
[3,2,1,0,4]：不能
```

为什么对？能到达的位置一定是从 0 开始连续的一段（能到达 farthest，就能到达它前面的所有位置，因为途中每一步都可以少跳一点）。所以只需要记住这一段的右端点。O(n) 时间，O(1) 空间。

## 4. 什么时候用贪心

| | 贪心 | 动态规划 |
|---|---|---|
| 每一步 | 只做当前最好的选择 | 考虑所有的选择 |
| 做出的选择 | 不再改变 | 保留所有子问题的结果 |
| 需要的性质 | 贪心选择性质 + 最优子结构 | 最优子结构 + 重叠子问题 |
| 正确性 | 需要证明 | 由状态和转移保证 |
| 例子 | 活动选择、哈夫曼编码、Dijkstra、Prim、Kruskal | 背包、编辑距离、最长递增子序列 |

前面的章节中已经见过好几个贪心算法：《哈夫曼树》每次合并两个最小的，《最短路径》中的 Dijkstra 每次确定距离最小的顶点，《最小生成树》中的 Prim 和 Kruskal 每次选最短的边。它们都有严格的证明。

实际解题时的建议：

1. 先想贪心：如果有一个自然的贪心策略，试着找反例；
2. 找不到反例，写一个暴力枚举，在小数据上对比（本文的做法）；
3. 对比通过，再尝试用交换论证证明；
4. 有反例，或者证明不了，就用动态规划。

## 5. 常见错误

1. **凭直觉认定贪心是对的**。「最短」这个策略听起来很合理，但在随机数据中错了 6%。
2. **排序的依据选错了**。活动选择中按开始时间、按长度排序都是错的。
3. **零钱兑换用贪心**。只对特定的面值组合（比如人民币）是对的，一般情况下要用 DP。
4. **区间的端点开闭搞错**。[1, 3) 和 [3, 5) 不冲突，[1, 3] 和 [3, 5] 冲突。比较时用 `>=` 还是 `>` 要和问题的定义一致。

## 6. 练习

- [跳跃游戏](/problems/jump-game/)、[跳跃游戏 II](/problems/jump-game-ii/)：第二题求最少跳几次，贪心是「每一跳都在能到达的范围内，选下一跳能到得最远的」
- [合并区间](/problems/merge-intervals/)：按开始时间排序
- [划分字母区间](/problems/partition-labels/)
- [买卖股票的最佳时机](/problems/best-time-to-buy-and-sell-stock/)
- [盛最多水的容器](/problems/container-with-most-water/)：下一篇的双指针，本质上也是贪心

思考题：

1. 活动选择中，如果每个活动有一个权重（比如收入），要求总权重最大，最早结束的贪心还对吗？应该怎么做？
2. 给出一组硬币面值，怎样判断「贪心找零」对所有金额都是最优的？（提示：用本文的方法，和 DP 对比。要检查到多大的金额？）
3. 为什么「最早结束」排序以后，只需要和**上一个**选中的活动比较，而不用和所有选中的比较？

<details>
<summary>点击查看答案</summary>

1. 不对了。比如 [0, 2) 权重 1，[1, 10) 权重 100：最早结束会选前者。带权的活动选择要用动态规划：按结束时间排序，dp[i] = max(dp[i-1], w[i] + dp[p(i)])，其中 p(i) 是结束时间不晚于第 i 个活动开始时间的最后一个活动（可以二分查找）。

2. 对每个金额，比较贪心和 DP 的结果。理论上可以证明，如果贪心不是最优的，最小的反例一定小于最大的两个面值之和，所以只需要检查到这个金额。本文对人民币面值检查了 1 到 1000，一次都没有错；对 {1, 3, 4}，金额 6 就出错了，6 < 3 + 4。

3. 按结束时间排序以后，选中的活动结束时间是递增的，上一个选中的活动结束得最晚。新活动只要不和它冲突（开始时间 ≥ 它的结束时间），就一定也不和更早选中的冲突。

</details>

下一篇：[双指针与滑动窗口](/posts/algo-two-pointers/)。
