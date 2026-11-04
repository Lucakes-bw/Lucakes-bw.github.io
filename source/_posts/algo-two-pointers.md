---
title: 双指针与滑动窗口
date: 2026-11-05 10:50:00
updated: 2026-11-05 10:50:00
categories:
  - 数据结构
  - 算法设计
tags:
  - 算法
  - 双指针
  - 滑动窗口
order: 6
description: 很多数组和字符串上的问题，暴力做法是枚举所有的 (i, j) 对，O(n²)。双指针的思路是：让两个下标按某种规则只向一个方向移动，每次移动都能排除掉一批不可能的组合，总共只走 O(n) 步。本文用它解决有序数组的两数之和、盛水最多的容器（16 万个数从 3.5 秒降到 0.26 毫秒），再用滑动窗口解决无重复字符的最长子串和最小覆盖子串，每个都说明「为什么可以排除」。
disableNunjucks: true
viz: [ds-algo]
---

> **本文目标**：会用左右两个指针向中间移动的方法解决有序数组上的问题；会用滑动窗口（两个指针同向移动）解决子串、子数组问题；最重要的是，能说清楚每一次移动为什么不会漏掉答案。
>
> **前置知识**：[上一篇](/posts/algo-greedy/)；《复杂度分析》。

## 1. 从 O(n²) 到 O(n)

数组上的很多问题，答案是一对下标 (i, j)，或者一个区间 [i, j]。暴力的做法是枚举所有的对，n(n-1)/2 种，O(n²)。

**双指针**的想法是：不枚举所有的对，而是让两个下标 i 和 j 按某个规则移动，**每一步都排除掉一批不可能是答案的对**。只要每个指针只朝一个方向移动、最多走 n 步，总时间就是 O(n)。

难点不在代码（代码通常只有十几行），而在于**证明每一步排除的都不可能是答案**。

## 2. 相向而行

### 2.1 有序数组的两数之和

> 在一个**升序**数组中找两个数，和为 target。

左指针指向最小的数，右指针指向最大的数，看它们的和：

- 等于 target：找到了；
- 小于 target：和太小，**左边的数换成大一点的**：`lo++`；
- 大于 target：和太大，**右边的数换成小一点的**：`hi--`。

```c
static bool twoSumSorted(const int* a, int n, int target, int* i, int* j)
{
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        int s = a[lo] + a[hi];
        if (s == target) {
            *i = lo;
            *j = hi;
            return true;
        }
        if (s < target) ++lo;               // 和太小：左边的数换大一点
        else --hi;                          // 和太大：右边的数换小一点
    }
    return false;
}
```

为什么 `lo++` 不会漏掉答案？此时 a[lo] + a[hi] < target，而 a[hi] 是剩下的数中最大的。a[lo] 和任何其他的数配对，和只会更小，**不可能**等于 target。所以 a[lo] 可以整个排除掉。`hi--` 同理。每一步排除一个数，最多 n 步。

### 2.2 盛水最多的容器

> 数组 h 中，h[i] 是第 i 条竖线的高度。选两条线，和 x 轴组成一个容器，最多能装多少水？面积 = (j - i) × min(h[i], h[j])。

h = [1, 8, 6, 2, 5, 4, 8, 3, 7]，答案是第 1 条（高 8）和第 8 条（高 7）：7 × 7 = 49。

从最宽的两条线（0 和 n-1）开始，每次**移动较矮的那一边**：

```c
static long long maxArea(const int* h, int n)
{
    int lo = 0, hi = n - 1;
    long long best = 0;
    while (lo < hi) {
        long long area = (long long)(hi - lo) * (h[lo] < h[hi] ? h[lo] : h[hi]);
        if (area > best) best = area;
        if (h[lo] < h[hi]) ++lo;
        else --hi;
    }
    return best;
}
```

```
lo=0 hi=8 面积=8
lo=1 hi=8 面积=49
lo=1 hi=7 面积=18
lo=1 hi=6 面积=40
lo=1 hi=5 面积=16
lo=1 hi=4 面积=15
lo=1 hi=3 面积=4
lo=1 hi=2 面积=6
最大面积 = 49
```

为什么移动较矮的一边？假设 h[lo] < h[hi]。以 lo 为一端的所有容器中，(lo, hi) 已经是最宽的了；把另一端换成 hi 左边的任何一条线，宽度变小，而高度不会超过 h[lo]（矮的那条决定了高度），面积**不可能**更大。所以 lo 这条线已经没有用了，可以排除。

<figure class="diagram">
<svg viewBox="0 0 700 230" role="img" aria-label="盛水最多的容器：h = [1,8,6,2,5,4,8,3,7]，第 1 条（高 8）和第 8 条（高 7）之间能装 7×7=49；两个指针从两端开始，每次移动较矮的一边">
  <defs><marker id="wt-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="wt-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="195.0" y="60" width="350" height="140" style="fill:var(--accent-soft);stroke:none"/><line x1="145.0" y1="200" x2="145.0" y2="180" style="stroke:var(--fg-2);stroke-width:3"/><text x="145.0" y="216.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">1</text><line x1="195.0" y1="200" x2="195.0" y2="40" style="stroke:var(--accent);stroke-width:4"/><text x="195.0" y="216.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">8</text><line x1="245.0" y1="200" x2="245.0" y2="80" style="stroke:var(--fg-2);stroke-width:3"/><text x="245.0" y="216.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">6</text><line x1="295.0" y1="200" x2="295.0" y2="160" style="stroke:var(--fg-2);stroke-width:3"/><text x="295.0" y="216.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">2</text><line x1="345.0" y1="200" x2="345.0" y2="100" style="stroke:var(--fg-2);stroke-width:3"/><text x="345.0" y="216.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">5</text><line x1="395.0" y1="200" x2="395.0" y2="120" style="stroke:var(--fg-2);stroke-width:3"/><text x="395.0" y="216.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">4</text><line x1="445.0" y1="200" x2="445.0" y2="40" style="stroke:var(--fg-2);stroke-width:3"/><text x="445.0" y="216.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">8</text><line x1="495.0" y1="200" x2="495.0" y2="140" style="stroke:var(--fg-2);stroke-width:3"/><text x="495.0" y="216.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">3</text><line x1="545.0" y1="200" x2="545.0" y2="60" style="stroke:var(--accent);stroke-width:4"/><text x="545.0" y="216.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">7</text><text x="370.0" y="140.0" text-anchor="middle" style="fill:var(--accent);font-size:13px">面积 = 7 × min(8, 7) = 49</text><text x="14.0" y="40.0" text-anchor="start" style="fill:var(--muted);font-size:11px">lo 和 hi 从两端出发</text><text x="14.0" y="58.0" text-anchor="start" style="fill:var(--muted);font-size:11px">h[lo] &lt; h[hi] 时，</text><text x="14.0" y="76.0" text-anchor="start" style="fill:var(--muted);font-size:11px">以 lo 为一端的容器</text><text x="14.0" y="94.0" text-anchor="start" style="fill:var(--muted);font-size:11px">不会比现在更大：</text><text x="14.0" y="112.0" text-anchor="start" style="fill:var(--link);font-size:12px">lo 右移</text><line x1="120" y1="200" x2="570" y2="200" style="stroke:var(--border-2);stroke-width:1.5"/>
  </g>
</svg>
</figure>

和暴力枚举比较（随机高度，两种方法的结果每次都相同）：

| n | 暴力 O(n²) | 双指针 O(n) |
|---|---|---|
| 10,000 | 0.042 秒 | 0.000032 秒 |
| 40,000 | 0.234 秒 | 0.000045 秒 |
| 160,000 | 3.527 秒 | 0.000262 秒 |

## 3. 同向而行：滑动窗口

另一类问题求的是满足某种条件的**子串**（连续的一段）。两个指针 left 和 right 都从左向右移动，[left, right] 是一个「窗口」：

- right 每次向右扩展一格，把新元素加进窗口；
- 窗口不满足条件（或者已经满足、可以更优）时，left 向右收缩。

两个指针都只向右走，各走最多 n 步，O(n)。

### 3.1 无重复字符的最长子串

> 找出字符串中不含重复字符的最长子串。

"abcabcbb" → "abc"，长度 3。

窗口 [left, i] 中没有重复的字符。right（这里是 i）向右走，遇到一个字符 c：

- 如果 c 不在窗口中，窗口直接变长；
- 如果 c 在窗口中（上一次出现在 last[c]），窗口中从 left 到 last[c] 的部分都不能要了，left 跳到 last[c] + 1。

```c
static int longestUnique(const char* s)
{
    int last[256];                          // last[c]：字符 c 上一次出现的位置
    for (int c = 0; c < 256; ++c) last[c] = -1;
    int left = 0, best = 0;
    for (int i = 0; s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (last[c] >= left) left = last[c] + 1;   // c 在窗口中出现过：左边界跳到它的后面
        last[c] = i;
        if (i - left + 1 > best) best = i - left + 1;
    }
    return best;
}
```

```
"abcabcbb"：3（"abc"）
"pwwkew"：3（"wke"）
"abba"：2（"ab"）
```

注意判断条件是 `last[c] >= left`，而不是 `last[c] != -1`。"abba" 是一个容易出错的例子：处理第二个 a 时，a 上一次出现在 0，但窗口已经是 [2, 3]（"ba"）了，那个 a 不在窗口中，left 不应该往回跳到 1。程序和暴力枚举在随机生成的 10000 个字符串上对比过，结果一致。

<div class="dsviz" data-viz="algo-window"></div>

### 3.2 最小覆盖子串

> 在 s 中找最短的子串，包含 t 中的所有字符（包括重复的）。

s = "ADOBECODEBANC"，t = "ABC" → "BANC"。

这一次窗口要**先扩展到满足条件，再尽量收缩**：

```c
int need[256] = {0}, missing = 0;           // need[c]：窗口中还缺几个 c（可以是负数，表示多了）
for (int i = 0; t[i]; ++i) {
    ++need[(unsigned char)t[i]];
    ++missing;                              // 一共还缺几个字符
}
int left = 0, bestLen = 1 << 30, bestLeft = 0;
for (int right = 0; s[right]; ++right) {
    if (need[(unsigned char)s[right]]-- > 0) --missing;      // 这个字符还缺，现在补上了一个
    while (missing == 0) {                                   // 窗口已经覆盖了 t：尽量收缩左边
        if (right - left + 1 < bestLen) {
            bestLen = right - left + 1;
            bestLeft = left;
        }
        if (++need[(unsigned char)s[left]] > 0) ++missing;   // 移走的字符是必需的，又缺了
        ++left;
    }
}
```

```
s="ADOBECODEBANC" t="ABC"："BANC"
s="a" t="aa"：没有
```

`missing` 记录还缺几个字符，判断「窗口是否覆盖了 t」从 O(字符集大小) 变成了 O(1)。

为什么收缩 left 不会漏掉答案？对于当前的 right，left 收缩到再缩就不满足条件为止，得到的是以 right 结尾的最短窗口。之后 right 继续往右走，left 不需要回退：以更右的位置结尾的满足条件的窗口，左端不会比现在的 left 更靠左还更短。

## 4. 什么时候想到双指针

- **有序数组**中找满足条件的一对：相向而行；
- 求满足条件的**最长 / 最短的连续子串、子数组**，并且条件具有单调性（窗口变大，某个量只增不减）：滑动窗口；
- 链表中：快慢指针找中点、判断有没有环（《单链表》）；
- 原地修改数组：一个指针读、一个指针写，比如[移动零](/problems/move-zeroes/)。

滑动窗口需要单调性。比如「和为 k 的子数组」，如果数组中有负数，窗口变大和不一定变大，滑动窗口就不适用了（要用前缀和加哈希表）。

## 5. 常见错误

1. **在无序数组上用相向的双指针**。排除的依据是「a[hi] 是剩下的数中最大的」，没有排序就不成立。
2. **滑动窗口的左边界往回走了**。比如 "abba" 中没有判断 `last[c] >= left`。
3. **收缩的条件写成 `if` 而不是 `while`**。一次可能要收缩好几格。
4. **面积、和的计算溢出**。盛水问题中 (j - i) × 高度可能超过 int 的范围，用 `long long`。
5. **说不清楚为什么可以移动**。双指针的代码很容易写成「看起来对」的样子，每次移动前都要问自己：被排除的那些组合，真的不可能是答案吗？

## 6. 练习

- [盛最多水的容器](/problems/container-with-most-water/)、[三数之和](/problems/3sum/)、[接雨水](/problems/trapping-rain-water/)：相向而行
- [无重复字符的最长子串](/problems/longest-substring-without-repeating-characters/)、[最小覆盖子串](/problems/minimum-window-substring/)、[找到字符串中所有字母异位词](/problems/find-all-anagrams-in-a-string/)：滑动窗口
- [滑动窗口最大值](/problems/sliding-window-maximum/)：窗口 + 单调队列（《单调栈与单调队列》）
- [移动零](/problems/move-zeroes/)、[颜色分类](/problems/sort-colors/)：读写指针

思考题：

1. 三数之和：排序后固定第一个数，剩下的两个数在后面用相向的双指针找。复杂度是多少？怎样去重？
2. 长度最小的子数组：正整数数组中找和 ≥ target 的最短连续子数组。写出滑动窗口的代码。如果数组中有负数呢？
3. 接雨水：每一格能接的水 = min(左边最高, 右边最高) - 自己的高度。怎样用双指针做到 O(1) 的额外空间？

<details>
<summary>点击查看答案</summary>

1. 排序 O(n log n)，外层 n 次，每次双指针 O(n)，总共 O(n²)。去重：外层跳过和前一个相同的数；找到一组答案后，lo 和 hi 都跳过相同的值再继续。

2. right 向右扩展，sum 加上新元素；`while (sum >= target)` 时记录长度，sum 减去 a[left]，left 右移。正整数保证了窗口变大和变大、变小和变小（单调性）。有负数时单调性不成立，可以用前缀和：找最大的 i < j 使 prefix[i] ≤ prefix[j] - target，用单调队列维护，O(n)。

3. 维护 leftMax（左边已经扫描过的最高）和 rightMax。如果 leftMax < rightMax，左指针那一格能接的水只取决于 leftMax（右边一定有更高的挡住），计算后左指针右移；否则处理右指针。每格只处理一次，O(n) 时间、O(1) 空间。

</details>

下一篇：[字符串匹配：KMP](/posts/algo-kmp/)。
