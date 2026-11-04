---
title: 动态规划（三）：序列问题
date: 2026-11-05 10:30:00
updated: 2026-11-05 10:30:00
categories:
  - 数据结构
  - 算法设计
tags:
  - 算法
  - 动态规划
  - 字符串
order: 4
description: 序列上的动态规划有两种典型的状态：「以第 i 个元素结尾」和「前 i 个、前 j 个」。本文用前一种解决最长递增子序列，再换一个巧妙的状态，配合二分查找把 O(n²) 降到 O(n log n)：16 万个数从 15 秒变成 3 毫秒；用后一种解决最长公共子序列和编辑距离，并从表中还原出具体的子序列和编辑步骤。
disableNunjucks: true
viz: [ds-algo]
---

> **本文目标**：掌握序列 DP 的两种状态定义；会用 O(n²) 和 O(n log n) 两种方法求最长递增子序列，理解后者的正确性；会用二维表求最长公共子序列和编辑距离，并还原出方案。
>
> **前置知识**：[上一篇](/posts/algo-dp-knapsack/)；《二分查找》（lowerBound）。

## 1. 最长递增子序列

> 给一个整数数组，找出最长的严格递增的**子序列**的长度。子序列不要求连续，但要保持原来的顺序。

[10, 9, 2, 5, 3, 7, 101, 18] 的最长递增子序列是 [2, 3, 7, 18]（或者 [2, 5, 7, 101] 等），长度 4。

### 1.1 O(n²)：以 a[i] 结尾

如果把状态定义成「前 i 个元素的最长递增子序列的长度」，转移时就不知道这个子序列的最后一个数是多少，后面的数能不能接上去。所以要把「结尾」放进状态：

> **dp[i] = 以 a[i] 结尾的最长递增子序列的长度**

a[i] 可以接在任何一个更早、更小的 a[j] 后面：dp[i] = max(dp[j] + 1)，其中 j < i 且 a[j] < a[i]；一个都没有时，dp[i] = 1（只有它自己）。答案是所有 dp[i] 中最大的。

```c
// dp[i] = 以 a[i] 结尾的最长递增子序列的长度
static int lisQuadratic(const int* a, int n, int* dp)
{
    int best = 0;
    for (int i = 0; i < n; ++i) {
        dp[i] = 1;
        for (int j = 0; j < i; ++j)
            if (a[j] < a[i] && dp[j] + 1 > dp[i]) dp[i] = dp[j] + 1;
        if (dp[i] > best) best = dp[i];
    }
    return best;
}
```

```
O(n²)：4，dp = [1 1 1 2 2 3 4 4]
```

「以第 i 个结尾」是序列 DP 中最常用的状态定义之一，[最大子数组和](/problems/maximum-subarray/)、[乘积最大子数组](/problems/maximum-product-subarray/) 都是这样定义的。

### 1.2 O(n log n)：换一个状态

换一个角度：

> **tails[k] = 所有长度为 k+1 的递增子序列中，最小的结尾**

为什么要「最小的结尾」？结尾越小，后面的数越容易接上去。长度相同的两个递增子序列，结尾是 5 的比结尾是 7 的更有潜力。

tails 有一个关键的性质：**它是严格递增的**。（如果长度为 3 的最小结尾是 7，那么长度为 2 的最小结尾一定小于 7：取那个长度为 3 的子序列的前两个，结尾就小于 7。）

依次处理每个数 x：

- 如果 x 比 tails 中所有的数都大：它可以接在最长的那个后面，tails 末尾追加 x，长度加 1；
- 否则，找到 tails 中**第一个 ≥ x** 的位置 k，把 tails[k] 换成 x：长度为 k+1 的子序列，现在有了一个更小的结尾。

因为 tails 是递增的，「第一个 ≥ x」可以用二分查找（就是《二分查找》中的 lowerBound）：

```c
// tails[k] = 所有长度为 k+1 的递增子序列中，最小的结尾。tails 是递增的，可以二分
static int lisFast(const int* a, int n, int* tails)
{
    int len = 0;
    for (int i = 0; i < n; ++i) {
        int lo = 0, hi = len;               // 找第一个 >= a[i] 的位置
        while (lo < hi) {
            int mid = lo + (hi - lo) / 2;
            if (tails[mid] < a[i]) lo = mid + 1;
            else hi = mid;
        }
        tails[lo] = a[i];                   // 替换它，或者接在最后
        if (lo == len) ++len;
    }
    return len;
}
```

每一步之后的 tails：

```
a[0]=10  tails = [10]
a[1]=9   tails = [9]
a[2]=2   tails = [2]
a[3]=5   tails = [2 5]
a[4]=3   tails = [2 3]
a[5]=7   tails = [2 3 7]
a[6]=101 tails = [2 3 7 101]
a[7]=18  tails = [2 3 7 18]
```

<div class="dsviz" data-viz="algo-lis"></div>

注意：**tails 本身不一定是一个递增子序列**，它只是每个长度的最小结尾，但它的长度就是答案。

随机生成的数组上，两种方法的时间：

| n | 最长递增子序列的长度 | O(n²) | O(n log n) |
|---|---|---|---|
| 10,000 | 185 | 0.056 秒 | 0.000175 秒 |
| 40,000 | 383 | 0.780 秒 | 0.000652 秒 |
| 160,000 | 794 | 15.356 秒 | 0.002952 秒 |

16 万个数：**15 秒对 3 毫秒**，相差 5000 倍。n 变成 4 倍时，O(n²) 的时间变成了十几倍：数据大到放不进 CPU 的缓存以后，内层循环每次都要从内存读，比纯粹的 n² 增长得还快。

## 2. 最长公共子序列

> 两个字符串的最长公共子序列（LCS）有多长？

"ABCBDAB" 和 "BDCABA" 的一个最长公共子序列是 "BCBA"，长度 4。`diff` 命令比较两个文件，核心就是求 LCS（以行为单位）。

两个序列的问题，状态通常是**两个前缀**：

> **dp[i][j] = s 的前 i 个字符和 t 的前 j 个字符的 LCS 长度**

看两个前缀的最后一个字符：

- 相同（s[i-1] == t[j-1]）：它一定可以作为 LCS 的最后一个字符：dp[i][j] = dp[i-1][j-1] + 1；
- 不同：至少有一个不在 LCS 的末尾，去掉 s 的最后一个或者 t 的最后一个，取较大的：dp[i][j] = max(dp[i-1][j], dp[i][j-1])。

边界：dp[0][j] = dp[i][0] = 0（空串和任何串的 LCS 是 0）。

```c
if (i == 0 || j == 0) dp[i][j] = 0;
else if (s[i - 1] == t[j - 1]) dp[i][j] = dp[i - 1][j - 1] + 1;
else dp[i][j] = max(dp[i - 1][j], dp[i][j - 1]);
```

```
       B  D  C  A  B  A
      0  0  0  0  0  0  0
  A   0  0  0  0  1  1  1
  B   0  1  1  1  1  2  2
  C   0  1  1  2  2  2  2
  B   0  1  1  2  2  3  3
  D   0  1  2  2  2  3  3
  A   0  1  2  2  3  3  4
  B   0  1  2  2  3  4  4
```

还原：从右下角开始，字符相同就记下这个字符、走向左上；不同就走向上面和左边中较大的那一格。倒着记下的字符反过来：

```
一个最长公共子序列：BCBA
```

<figure class="diagram">
<svg viewBox="0 0 700 250" role="img" aria-label="最长公共子序列的表和还原路径：从右下角出发，字符相同就走向左上并记下这个字符，不同就走向上面和左边中较大的那一格，记下的 A、B、C、B 倒过来就是 BCBA">
  <defs><marker id="lc-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="lc-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="242.0" y="22.0" text-anchor="middle" style="fill:var(--muted);font-size:12px">B</text><text x="278.0" y="22.0" text-anchor="middle" style="fill:var(--muted);font-size:12px">D</text><text x="314.0" y="22.0" text-anchor="middle" style="fill:var(--muted);font-size:12px">C</text><text x="350.0" y="22.0" text-anchor="middle" style="fill:var(--muted);font-size:12px">A</text><text x="386.0" y="22.0" text-anchor="middle" style="fill:var(--muted);font-size:12px">B</text><text x="422.0" y="22.0" text-anchor="middle" style="fill:var(--muted);font-size:12px">A</text><rect x="190" y="30" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="206.0" y="45.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0</text><rect x="226" y="30" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="242.0" y="45.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0</text><rect x="262" y="30" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="278.0" y="45.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0</text><rect x="298" y="30" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="314.0" y="45.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0</text><rect x="334" y="30" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="350.0" y="45.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0</text><rect x="370" y="30" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="386.0" y="45.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0</text><rect x="406" y="30" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="422.0" y="45.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0</text><text x="180.0" y="74.0" text-anchor="end" style="fill:var(--muted);font-size:12px">A</text><rect x="190" y="56" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="206.0" y="71.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0</text><rect x="226" y="56" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="242.0" y="71.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0</text><rect x="262" y="56" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="278.0" y="71.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0</text><rect x="298" y="56" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="314.0" y="71.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0</text><rect x="334" y="56" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="350.0" y="71.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">1</text><rect x="370" y="56" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="386.0" y="71.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">1</text><rect x="406" y="56" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="422.0" y="71.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">1</text><text x="180.0" y="100.0" text-anchor="end" style="fill:var(--muted);font-size:12px">B</text><rect x="190" y="82" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="206.0" y="97.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0</text><rect x="226" y="82" width="32" height="22" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="242.0" y="97.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">1</text><rect x="262" y="82" width="32" height="22" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="278.0" y="97.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">1</text><rect x="298" y="82" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="314.0" y="97.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">1</text><rect x="334" y="82" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="350.0" y="97.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">1</text><rect x="370" y="82" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="386.0" y="97.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><rect x="406" y="82" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="422.0" y="97.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><text x="180.0" y="126.0" text-anchor="end" style="fill:var(--muted);font-size:12px">C</text><rect x="190" y="108" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="206.0" y="123.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0</text><rect x="226" y="108" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="242.0" y="123.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">1</text><rect x="262" y="108" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="278.0" y="123.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">1</text><rect x="298" y="108" width="32" height="22" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="314.0" y="123.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><rect x="334" y="108" width="32" height="22" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="350.0" y="123.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><rect x="370" y="108" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="386.0" y="123.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><rect x="406" y="108" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="422.0" y="123.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><text x="180.0" y="152.0" text-anchor="end" style="fill:var(--muted);font-size:12px">B</text><rect x="190" y="134" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="206.0" y="149.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0</text><rect x="226" y="134" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="242.0" y="149.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">1</text><rect x="262" y="134" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="278.0" y="149.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">1</text><rect x="298" y="134" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="314.0" y="149.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><rect x="334" y="134" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="350.0" y="149.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><rect x="370" y="134" width="32" height="22" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="386.0" y="149.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">3</text><rect x="406" y="134" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="422.0" y="149.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">3</text><text x="180.0" y="178.0" text-anchor="end" style="fill:var(--muted);font-size:12px">D</text><rect x="190" y="160" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="206.0" y="175.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0</text><rect x="226" y="160" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="242.0" y="175.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">1</text><rect x="262" y="160" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="278.0" y="175.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><rect x="298" y="160" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="314.0" y="175.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><rect x="334" y="160" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="350.0" y="175.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><rect x="370" y="160" width="32" height="22" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="386.0" y="175.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">3</text><rect x="406" y="160" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="422.0" y="175.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">3</text><text x="180.0" y="204.0" text-anchor="end" style="fill:var(--muted);font-size:12px">A</text><rect x="190" y="186" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="206.0" y="201.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0</text><rect x="226" y="186" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="242.0" y="201.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">1</text><rect x="262" y="186" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="278.0" y="201.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><rect x="298" y="186" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="314.0" y="201.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><rect x="334" y="186" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="350.0" y="201.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">3</text><rect x="370" y="186" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="386.0" y="201.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">3</text><rect x="406" y="186" width="32" height="22" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="422.0" y="201.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">4</text><text x="180.0" y="230.0" text-anchor="end" style="fill:var(--muted);font-size:12px">B</text><rect x="190" y="212" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="206.0" y="227.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">0</text><rect x="226" y="212" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="242.0" y="227.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">1</text><rect x="262" y="212" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="278.0" y="227.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><rect x="298" y="212" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="314.0" y="227.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><rect x="334" y="212" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="350.0" y="227.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">3</text><rect x="370" y="212" width="32" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="386.0" y="227.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">4</text><rect x="406" y="212" width="32" height="22" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="422.0" y="227.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">4</text><text x="14.0" y="60.0" text-anchor="start" style="fill:var(--muted);font-size:11px">绿色：字符相同，</text><text x="14.0" y="76.0" text-anchor="start" style="fill:var(--muted);font-size:11px">记下它，走向左上</text><text x="14.0" y="104.0" text-anchor="start" style="fill:var(--muted);font-size:11px">黄色：不同，走向</text><text x="14.0" y="120.0" text-anchor="start" style="fill:var(--muted);font-size:11px">较大的一边</text><text x="14.0" y="150.0" text-anchor="start" style="fill:var(--muted);font-size:11px">记下 A、B、C、B</text><text x="14.0" y="166.0" text-anchor="start" style="fill:var(--accent);font-size:12px">倒过来：BCBA</text>
  </g>
</svg>
</figure>

## 3. 编辑距离

> 把字符串 s 变成 t，每次可以插入、删除或替换一个字符，最少要几步？

这是拼写检查、DNA 比对中常用的「两个字符串有多不像」的度量（也叫 Levenshtein 距离）。

> **dp[i][j] = 把 s 的前 i 个字符变成 t 的前 j 个字符，最少要几步**

最后一个字符：

- 相同：不用操作，dp[i][j] = dp[i-1][j-1]；
- 不同，三种选择取最小，再加 1：
  - **替换** s[i-1] 为 t[j-1]：dp[i-1][j-1]；
  - **删除** s[i-1]：dp[i-1][j]；
  - **插入** t[j-1]：dp[i][j-1]。

边界：dp[i][0] = i（删掉 i 个字符），dp[0][j] = j（插入 j 个字符）。

```c
for (int i = 0; i <= m; ++i) dp[i][0] = i;   // s 的前 i 个字符变成空串：删 i 次
for (int j = 0; j <= n; ++j) dp[0][j] = j;   // 空串变成 t 的前 j 个字符：插 j 次
for (int i = 1; i <= m; ++i)
    for (int j = 1; j <= n; ++j) {
        if (s[i - 1] == t[j - 1]) dp[i][j] = dp[i - 1][j - 1];
        else dp[i][j] = 1 + min3(dp[i - 1][j - 1],   // 替换
                                 dp[i - 1][j],       // 删除 s[i-1]
                                 dp[i][j - 1]);      // 插入 t[j-1]
    }
```

同样可以从右下角往回推，得到具体的步骤：

```
horse → ros：3 步：把 h 替换成 r，删除 r，删除 e
intention → execution：5 步：把 i 替换成 e，把 n 替换成 x，把 t 替换成 e，把 e 替换成 c，把 n 替换成 u
kitten → sitting：3 步：把 k 替换成 s，把 e 替换成 i，插入 g
```

最优的步骤往往不止一种。比如 intention → execution 也可以是「删除 t，替换 i→e、n→x、n→c，插入 u」，同样是 5 步。

## 4. 两种状态的总结

| 状态 | 适合的问题 | 例子 |
|---|---|---|
| dp[i]：以第 i 个结尾 | 一个序列，子序列 / 子数组的最优值 | 最长递增子序列、最大子数组和 |
| dp[i][j]：前 i 个和前 j 个 | 两个序列之间的关系 | 最长公共子序列、编辑距离 |
| dp[i][j]：区间 [i, j] | 一个序列的区间 | 最长回文子串、戳气球 |

## 5. 常见错误

1. **子序列和子串（子数组）弄混**。子序列可以不连续，子串必须连续。最长公共子串的转移是：字符不同时 dp[i][j] = 0，而不是取 max。
2. **以为 tails 就是一个最长递增子序列**。它只给出长度。要还原具体的子序列，需要额外记录每个元素的前驱。
3. **「严格递增」和「不下降」用错了二分**。严格递增找第一个 ≥ x 的位置（lowerBound）；允许相等时找第一个 > x 的位置（upperBound）。
4. **下标多一或少一**。dp 表比字符串多一行一列（空前缀），访问字符时是 s[i-1]。

## 6. 练习

- [最长递增子序列](/problems/longest-increasing-subsequence/)、[最长公共子序列](/problems/longest-common-subsequence/)、[编辑距离](/problems/edit-distance/)：本文的三个问题
- [最大子数组和](/problems/maximum-subarray/)、[乘积最大子数组](/problems/maximum-product-subarray/)：以 i 结尾
- [最长回文子串](/problems/longest-palindromic-substring/)：区间 DP
- [最长有效括号](/problems/longest-valid-parentheses/)

思考题：

1. 编辑距离只用两行（或一行加一个变量）可以吗？
2. 一组信封，每个有宽和高，一个信封能装进另一个，当且仅当宽和高都更小。最多能套几层？
3. 怎样修改 `lisFast`，输出一个具体的最长递增子序列？

<details>
<summary>点击查看答案</summary>

1. 可以。dp[i][j] 只依赖上一行的 dp[i-1][j-1]、dp[i-1][j] 和这一行的 dp[i][j-1]。用一个一维数组，从左到右更新时，dp[j-1] 已经是这一行的值，dp[j] 还是上一行的值，只差 dp[i-1][j-1]：在覆盖 dp[j] 之前，用一个变量保存它，下一格使用。空间从 O(mn) 降到 O(n)。

2. 这是二维的最长递增子序列。按宽度从小到大排序，**宽度相同时按高度从大到小排**，然后对高度求最长严格递增子序列。宽度相同的信封高度倒序排列，保证了它们不会被同时选进递增子序列（不能互相装）。

3. 除了 tails 的值，再记录 tailsIndex[k]（tails[k] 是原数组中哪个下标），以及每个元素的前驱 prev[i] = tailsIndex[k-1]（它接在长度为 k 的那个子序列后面）。最后从 tailsIndex[len-1] 开始沿着 prev 往回走，倒过来就是一个最长递增子序列。

</details>

下一篇：[贪心](/posts/algo-greedy/)。
