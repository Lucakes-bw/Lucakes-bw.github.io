---
title: 字符串匹配：KMP
date: 2026-11-05 11:00:00
updated: 2026-11-05 11:00:00
categories:
  - 数据结构
  - 算法设计
tags:
  - 算法
  - 字符串
  - KMP
order: 7
description: 在一个字符串中找另一个字符串，最直接的方法是每个起点都从头比较一遍。在 1000 万个 a 中找「999 个 a 加一个 b」，朴素算法比较了将近 100 亿次、用了 2.2 秒；KMP 只比较了 2000 万次、6 毫秒。秘密在于失配时文本的指针不回退，模式根据 next 数组跳到合适的位置继续比较。本文讲清楚 next 数组的含义和构造方法。
disableNunjucks: true
viz: [ds-algo]
---

> **本文目标**：知道朴素字符串匹配的最坏情况；理解 KMP 的核心思想：利用已经匹配的部分，失配时文本的指针不回退；理解 next 数组的含义，会构造它；知道 KMP 的复杂度为什么是 O(n + m)。
>
> **前置知识**：《字符串》；[上一篇](/posts/algo-two-pointers/)（「每一步排除一批不可能的情况」的思路）。

## 1. 朴素算法

在文本 text（长 n）中找模式 pat（长 m）第一次出现的位置。最直接的方法：从每一个起点 i 开始，逐个字符比较，全部相同就找到了；中途不同，就换下一个起点：

```c
static int naiveSearch(const char* text, int n, const char* pat, int m)
{
    for (int i = 0; i + m <= n; ++i) {
        int j = 0;
        while (j < m && text[i + j] == pat[j]) ++j;
        if (j == m) return i;
    }
    return -1;
}
```

平常的文本中，大部分起点比较一两个字符就失配了，速度很快。但在最坏的情况下，每个起点都要比较到最后一个字符才失配：文本是 n 个 a，模式是 m-1 个 a 加一个 b。比较次数接近 n × m：

```
n=  100000 m=1000：朴素    99001000 次 0.042 秒
n= 1000000 m=1000：朴素   999001000 次 0.225 秒
n=10000000 m=1000：朴素  9999001000 次 2.191 秒
```

1000 万个字符，比较了将近 **100 亿**次。这种输入看起来很刻意，但在 DNA 序列（只有 4 个字母）、日志中重复的模式、或者有人故意构造的输入中，确实会出现。

## 2. KMP 的思想

看朴素算法浪费在哪里。在 text = "ABCDAB ABCDABCDABDE" 中找 pat = "ABCDABD"：

```
text: A B C D A B _ ...
pat:  A B C D A B D
                  ↑ 第 7 个字符失配
```

前 6 个字符 "ABCDAB" 已经匹配了。朴素算法接下来把模式右移一格，从 text 的第 2 个字符重新比较。但我们**已经知道**这 6 个字符是什么了：它们就是 "ABCDAB"。

"ABCDAB" 的结尾 "AB" 和它的开头 "AB" 相同。所以模式可以直接右移 4 格，让开头的 "AB" 对准结尾的 "AB"，**这两个字符不用再比较**，从模式的第 3 个字符（C）接着和 text 中刚才失配的那个字符比较：

```
text: A B C D A B _ ...
pat:          A B C D A B D
                  ↑ 文本的指针不动，模式从 C 开始继续比较
```

这就是 KMP（Knuth–Morris–Pratt）的核心：**文本的指针永远不回退**，失配时只移动模式，移动多少由模式本身决定，和文本无关，可以事先算好。

## 3. next 数组

### 3.1 含义

> **next[j] = 模式的前 j+1 个字符 pat[0..j] 中，最长的「相等的真前缀和真后缀」的长度**

「真」前缀、后缀是指不等于整个字符串本身的。比如 "ABCDAB"：前缀有 A、AB、ABC、ABCD、ABCDA，后缀有 B、AB、DAB、CDAB、BCDAB，相等的最长的是 "AB"，长度 2。

```
ababaca    next = [0 0 1 2 3 0 1]
aabaaab    next = [0 1 0 1 2 2 3]
abcdabd    next = [0 0 0 0 1 2 0]
aaaa       next = [0 1 2 3]
```

匹配时，如果 pat[j] 和 text[i] 失配，说明 pat[0..j-1] 已经匹配了。把模式移动到让它的长度为 next[j-1] 的前缀对准这一段的后缀，然后用 pat[next[j-1]] 继续和 text[i] 比较。如果还是失配，就再跳 next，直到 j 变成 0。

### 3.2 匹配

```c
static int kmpSearch(const char* text, int n, const char* pat, int m, const int* next)
{
    for (int i = 0, j = 0; i < n; ++i) {    // i 只往前走，从不回退
        while (j > 0 && text[i] != pat[j]) j = next[j - 1];
        if (text[i] == pat[j]) ++j;
        if (j == m) return i - m + 1;
    }
    return -1;
}
```

j 是当前已经匹配了模式的几个字符。每一轮 text[i] 和 pat[j] 比较：相同，j 加 1；不同，j 沿着 next 往回跳，直到相同或者 j 是 0。

<div class="dsviz" data-viz="algo-kmp"></div>

### 3.3 构造 next 数组

构造 next 数组，其实就是**拿模式和它自己匹配**：

```c
// next[j]：pat[0..j] 这一段，最长的「相等的真前缀和真后缀」的长度
static void buildNext(const char* pat, int m, int* next)
{
    next[0] = 0;
    for (int i = 1, len = 0; i < m; ++i) {
        while (len > 0 && pat[i] != pat[len]) len = next[len - 1];   // 回退到更短的前缀
        if (pat[i] == pat[len]) ++len;
        next[i] = len;
    }
}
```

len 是 pat[0..i-1] 的最长相等前后缀的长度。处理 pat[i] 时：

- 如果 pat[i] == pat[len]：前缀可以延长一个字符，next[i] = len + 1；
- 否则，退而求其次：在长度为 len 的那个前缀内部，再找它的最长相等前后缀（next[len-1]），看能不能延长……直到 len 为 0。

这和匹配的代码几乎一模一样，只是「文本」换成了模式自己。

<figure class="diagram">
<svg viewBox="0 0 700 200" role="img" aria-label="next 数组的含义：模式 ABCDABD 的前 6 个字符 ABCDAB 中，最长相等的真前缀和真后缀是 AB，长度 2，所以 next[5] = 2；在第 6 个字符失配时，模式右移 4 格，让开头的 AB 对准结尾的 AB，从 C 继续比较">
  <defs><marker id="nx-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="nx-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="150" y="30" width="36" height="34" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="168.0" y="51.9" text-anchor="middle" style="fill:var(--fg);font-size:13px">A</text><text x="168.0" y="80.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">0</text><rect x="190" y="30" width="36" height="34" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="208.0" y="51.9" text-anchor="middle" style="fill:var(--fg);font-size:13px">B</text><text x="208.0" y="80.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1</text><rect x="230" y="30" width="36" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="248.0" y="51.9" text-anchor="middle" style="fill:var(--fg);font-size:13px">C</text><text x="248.0" y="80.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">2</text><rect x="270" y="30" width="36" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="288.0" y="51.9" text-anchor="middle" style="fill:var(--fg);font-size:13px">D</text><text x="288.0" y="80.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">3</text><rect x="310" y="30" width="36" height="34" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="328.0" y="51.9" text-anchor="middle" style="fill:var(--fg);font-size:13px">A</text><text x="328.0" y="80.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">4</text><rect x="350" y="30" width="36" height="34" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="368.0" y="51.9" text-anchor="middle" style="fill:var(--fg);font-size:13px">B</text><text x="368.0" y="80.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">5</text><rect x="390" y="30" width="36" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="408.0" y="51.9" text-anchor="middle" style="fill:var(--fg);font-size:13px">D</text><text x="408.0" y="80.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">6</text><text x="14.0" y="52.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">模式</text><text x="150.0" y="104.0" text-anchor="start" style="fill:var(--fg);font-size:12px">前 6 个字符 ABCDAB：开头的 AB（绿）= 结尾的 AB（黄），next[5] = 2</text><text x="14.0" y="146.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">失配后</text><rect x="310" y="124" width="36" height="34" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="328.0" y="145.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">A</text><rect x="350" y="124" width="36" height="34" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="368.0" y="145.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">B</text><rect x="390" y="124" width="36" height="34" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="408.0" y="145.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">C</text><rect x="430" y="124" width="36" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="448.0" y="145.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">D</text><rect x="470" y="124" width="36" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="488.0" y="145.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">A</text><rect x="510" y="124" width="36" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="528.0" y="145.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">B</text><rect x="550" y="124" width="36" height="34" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="568.0" y="145.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">D</text><text x="150.0" y="184.0" text-anchor="start" style="fill:var(--muted);font-size:11px">模式右移 6 - 2 = 4 格，开头的 AB 不用再比较，从 C 和文本中失配的那个字符继续比</text>
  </g>
</svg>
</figure>

## 4. 复杂度

匹配时，i 从 0 走到 n，只增不减；j 每次最多加 1，`while` 中每跳一次 next 至少减 1，而 j 不会小于 0，所以 j 减少的总次数不超过增加的总次数，也就是不超过 n。总的比较次数不超过 2n。构造 next 数组同理，不超过 2m。**KMP 的时间复杂度是 O(n + m)**，和模式的样子无关。

最坏情况下实测：

| n | 朴素：比较次数 | 时间 | KMP：比较次数 | 时间 | strstr |
|---|---|---|---|---|---|
| 10 万 | 9900 万 | 0.042 s | 19.9 万 | 0.0001 s | 0.0026 s |
| 100 万 | 9.99 亿 | 0.225 s | 200 万 | 0.0006 s | 0.0173 s |
| 1000 万 | 99.99 亿 | 2.191 s | 2000 万 | 0.0064 s | 0.1737 s |

（模式长 1000：999 个 a 加一个 b。）KMP 的比较次数正好接近 2n。

C 标准库的 `strstr` 在这个输入上也比朴素算法快得多，但比我们的 KMP 慢。各种 C 库的 `strstr` 实现不同，常见的是 Two-Way 算法，它同样保证线性时间，并且不需要额外的 O(m) 空间；在普通的文本上，库函数通常更快（用了向量指令等优化）。**实际工作中直接用库函数**，自己写 KMP 的意义在于理解它的思想，以及在库函数不能用的场合（比如在一个数据流中匹配，数据一段一段地到达）使用它：KMP 的文本指针从不回退，天然适合流式的数据。

程序用随机的短字符串（字母只有 a、b、c，更容易出现部分匹配）对比了 10 万组 KMP 和朴素算法的结果，全部一致。

## 5. 常见错误

1. **next 数组的定义有好几种版本**。有的教材整体右移一位、next[0] = -1，有的从 1 开始编号。代码要和定义一致，不要混用不同版本的代码和公式。
2. **失配时用 `if` 而不是 `while`**。只跳一次 next 不一定够，要一直跳到匹配或者 j 为 0。
3. **构造 next 时从 i = 0 开始**。next[0] 一定是 0（一个字符没有真前缀），循环从 1 开始。
4. **找所有出现的位置时，匹配成功后没有正确地继续**。找到一个以后，令 j = next[m-1]，继续匹配，这样能找到重叠的出现（比如在 "aaaa" 中找 "aa" 有 3 处）。

## 6. 小结（整章）

这一章的七篇讲了几种基本的算法设计思路：

| 思路 | 核心 | 典型的复杂度 |
|---|---|---|
| 回溯 | 深度优先地枚举所有选择，走不通就退回；剪枝 | 指数级 |
| 动态规划 | 定义状态，每个子问题只算一次 | 状态数 × 每个状态的转移 |
| 贪心 | 每一步做当前最好的选择，需要证明 | 通常 O(n log n) |
| 双指针 | 每一步排除一批不可能的组合 | O(n) |
| KMP | 利用已经匹配的信息，不回退 | O(n + m) |

遇到一个新问题，可以按这个顺序想：能不能贪心（找反例、和暴力对比）？有没有重叠的子问题（DP）？是不是数组、字符串上的区间问题（双指针）？都不是，n 又很小，就回溯。不管用哪种方法，**写一个暴力的版本，在小数据上对比**，是检验算法是否正确最有效的办法，本章的每个程序都是这样做的。

## 7. 练习

- [找出字符串中第一个匹配项的下标](https://leetcode.cn/problems/find-the-index-of-the-first-occurrence-in-a-string/)（LeetCode 28）：直接用 KMP
- [最长回文子串](/problems/longest-palindromic-substring/)：另一种字符串问题，可以用中心扩展或 DP

思考题：

1. 一个字符串是否由它的某个子串重复多次构成（比如 "abcabcabc"）？怎样用 next 数组判断？
2. next 数组还可以怎样优化？（提示：如果 pat[j] == pat[next[j-1]]，失配以后跳过去还会失配。）
3. 在一个不断到达的数据流（比如网络连接收到的数据）中检测一个关键词，数据一段一段地到达。KMP 要怎样改？

<details>
<summary>点击查看答案</summary>

1. 设长度为 n，k = next[n-1]。如果 k > 0 并且 n % (n - k) == 0，那么它由长度为 n - k 的子串重复构成。比如 "abcabcabc"，next[8] = 6，9 % 3 == 0，由 "abc" 重复 3 次构成。原因是：最长相等前后缀错开了 n - k 个字符，前缀和后缀相等意味着字符串以 n - k 为周期。

2. 这就是 KMP 的改进版（有时叫 nextval）：构造时，如果 pat[i] 和跳过去的位置 pat[next[...]] 的字符相同，就直接继续跳，存最终的位置。这样匹配时每次失配只需要跳一次。渐近复杂度不变，但常数更小。

3. 把 j（已经匹配了几个字符）作为状态保存下来。每收到一段数据，用保存的 j 接着匹配这段数据中的每个字符，处理完以后再保存 j。因为文本的指针从不回退，不需要保存之前到达的数据。这正是 KMP 适合流式匹配的原因：它本质上是一个以 j 为状态的有限自动机。

</details>
