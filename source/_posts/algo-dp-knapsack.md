---
title: 动态规划（二）：背包问题
date: 2026-11-05 10:20:00
updated: 2026-11-05 10:20:00
categories:
  - 数据结构
  - 算法设计
tags:
  - 算法
  - 动态规划
  - 背包
order: 3
description: 0-1 背包是最经典的动态规划问题：每件物品选或不选，容量有限，价值最大。本文先填出完整的二维表，再从表中还原出选了哪些物品；然后把二维压缩成一维，解释为什么容量必须倒着遍历：在随机生成的 10000 组数据中，正着遍历错了 8362 组，因为它悄悄地把同一件物品选了好几次，这恰好就是完全背包的写法。最后用 0-1 背包解决分割等和子集。
disableNunjucks: true
viz: [ds-algo]
---

> **本文目标**：会用二维表解决 0-1 背包，并还原出选择的方案；理解滚动数组的原理，以及一维写法中遍历顺序的含义；能区分 0-1 背包和完全背包；会把「能否选出一些数使和为 S」这类问题转化成背包问题。
>
> **前置知识**：[上一篇](/posts/algo-dp-basics/)。

## 1. 问题

有 n 件物品，第 i 件的重量是 w[i]，价值是 v[i]。背包最多能装重量 W。每件物品**只有一件**，要么放进去，要么不放（这就是「0-1」的意思）。怎样选，总价值最大？

本文的例子，容量 W = 8：

| 物品 | 重量 | 价值 |
|---|---|---|
| 1 | 1 | 15 |
| 2 | 3 | 20 |
| 3 | 4 | 30 |
| 4 | 5 | 55 |

先想一下贪心：按「价值 / 重量」从高到低放？物品 1 是 15，物品 4 是 11，物品 3 是 7.5，物品 2 是 6.7。放 1（重 1）、放 4（重 6）、放 3 放不下了、放 2 也放不下（重 9），总价值 70。但最优是 75（后面会看到）。贪心又不行了。

暴力枚举每件选或不选，2ⁿ 种，n = 40 就是一万亿种。DP 可以做到 O(n × W)。

## 2. 二维表

### 2.1 状态和转移

按物品一件一件地考虑：

> **dp[i][w] = 只考虑前 i 件物品、背包容量为 w 时，能得到的最大价值**

对于第 i 件物品，只有两种选择：

- **不选**：价值和只考虑前 i-1 件时一样：dp[i-1][w]；
- **选**（前提是放得下，w[i] ≤ w）：先腾出 w[i] 的空间给它，剩下的容量 w - w[i] 用前 i-1 件物品装：dp[i-1][w - w[i]] + v[i]。

> **dp[i][w] = max(dp[i-1][w], dp[i-1][w - w[i]] + v[i])**

边界：dp[0][w] = 0（没有物品，价值是 0）。

```c
for (int w = 0; w <= W; ++w) dp[0][w] = 0;
for (int i = 1; i <= n; ++i)
    for (int w = 0; w <= W; ++w) {
        dp[i][w] = dp[i - 1][w];                                             // 不选第 i 件
        if (wt[i - 1] <= w) dp[i][w] = max(dp[i][w], dp[i - 1][w - wt[i - 1]] + val[i - 1]);   // 选第 i 件
    }
```

（数组下标从 0 开始，第 i 件物品是 `wt[i-1]`、`val[i-1]`。）

### 2.2 填出来的表

| 物品 \ 容量 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|---|
| （无） | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 1：重 1 值 15 | 0 | 15 | 15 | 15 | 15 | 15 | 15 | 15 | 15 |
| 2：重 3 值 20 | 0 | 15 | 15 | 20 | 35 | 35 | 35 | 35 | 35 |
| 3：重 4 值 30 | 0 | 15 | 15 | 20 | 35 | 45 | 45 | 50 | 65 |
| 4：重 5 值 55 | 0 | 15 | 15 | 20 | 35 | 55 | 70 | 70 | **75** |

右下角就是答案：75。看几个格子是怎么来的：

- dp[2][4] = 35：容量 4，前两件。不选物品 2 是 15；选物品 2（重 3），剩下容量 1 用物品 1，dp[1][1] + 20 = 35。取 35；
- dp[4][8] = 75：不选物品 4 是 dp[3][8] = 65（物品 1、2、3，正好重 8）；选物品 4（重 5），剩下容量 3：dp[3][3] + 55 = 20 + 55 = 75。取 75。

下面的演示一行一行地填这张表。每填一格，会标出它依赖的两格：正上方（不选）和上一行左边 w[i] 格（选）：

<div class="dsviz" data-viz="algo-knapsack"></div>

### 2.3 还原方案

表只告诉我们最大价值是 75，选了哪些物品要从右下角往回推：如果 dp[i][w] 和 dp[i-1][w] 不同，说明第 i 件被选了，容量减去它的重量，继续看第 i-1 件；相同就是没选。

```c
for (int i = n, w = W; i > 0; --i) {
    if (dp[i][w] != dp[i - 1][w]) {     // 和不选第 i 件不同，说明选了
        printf("物品%d ", i);
        w -= wt[i - 1];
    }
}
```

```
选了：物品4 物品2
```

物品 4（重 5，值 55）加物品 2（重 3，值 20），重 8，值 75。

## 3. 压缩成一维

### 3.1 滚动数组

第 i 行只用到第 i-1 行。所以只需要一行，算第 i 行时，就地覆盖：

```c
int dp[W + 1] = {0};
for (int i = 0; i < n; ++i)
    for (int w = W; w >= wt[i]; --w)            // 注意：倒序
        dp[w] = max(dp[w], dp[w - wt[i]] + val[i]);
```

空间从 O(nW) 降到 O(W)。代价是不能再还原方案了（中间的行被覆盖了）。

### 3.2 为什么必须倒序

更新 dp[w] 时要用的 dp[w - wt[i]]，应该是**上一行**的值（不含第 i 件物品）。

- **倒序**（w 从大到小）：更新 dp[w] 时，比它小的 dp[w - wt[i]] 还没有被这一轮更新过，仍然是上一行的值。正确；
- **正序**（w 从小到大）：dp[w - wt[i]] 在这一轮中已经更新过了，可能已经包含了第 i 件物品，再加一次 v[i]，就等于**把第 i 件物品放了两次**。

<figure class="diagram">
<svg viewBox="0 0 700 210" role="img" aria-label="一维 0-1 背包中容量的遍历顺序：倒序时，更新 dp[w] 用到的 dp[w-wt] 还是上一行的值；正序时，dp[w-wt] 已经在这一轮被更新过，包含了当前物品，于是同一件物品被选了两次">
  <defs><marker id="od-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="od-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="14.0" y="36.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">倒序 ✓</text><rect x="130" y="14" width="50" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="155.0" y="35.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">0</text><rect x="186" y="14" width="50" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="211.0" y="35.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">15</text><rect x="242" y="14" width="50" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="267.0" y="35.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">15</text><rect x="298" y="14" width="50" height="34" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="323.0" y="35.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">15</text><rect x="354" y="14" width="50" height="34" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="379.0" y="35.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">15</text><text x="130.0" y="66.0" text-anchor="start" style="fill:var(--muted);font-size:11px">物品 1（重 1，值 15）：先更新 dp[4]，用的 dp[3] 还是 0（上一行）</text><text x="14.0" y="130.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">正序 ✗</text><rect x="130" y="108" width="50" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="155.0" y="129.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">0</text><rect x="186" y="108" width="50" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="211.0" y="129.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">15</text><rect x="242" y="108" width="50" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="267.0" y="129.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">30</text><rect x="298" y="108" width="50" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="323.0" y="129.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">45</text><rect x="354" y="108" width="50" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="379.0" y="129.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">60</text><text x="130.0" y="160.0" text-anchor="start" style="fill:var(--muted);font-size:11px">先更新 dp[1] = 15，算 dp[2] 时用的 dp[1] 已经含有物品 1：再加一次，变成 30……</text><text x="155.0" y="206.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">w=0</text><text x="211.0" y="206.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">w=1</text><text x="267.0" y="206.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">w=2</text><text x="323.0" y="206.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">w=3</text><text x="379.0" y="206.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">w=4</text>
  </g>
</svg>
</figure>

用例子验证一下：

```
一维（倒序）= 75，一维（正序）= 120
```

120 是怎么来的？物品 1 重 1、值 15，正序时可以被选 8 次：8 × 15 = 120。

程序还随机生成了 10000 组数据（每组最多 12 件物品），和枚举所有 2ⁿ 种选法的暴力结果比较：

```
随机 10000 组：二维、一维倒序都和暴力枚举一致；一维正序错了 8362 组
```

一行代码的遍历方向，决定了结果是对是错，而且错的时候不会崩溃，只是悄悄地给出一个更大的数。写一维的背包时，一定要想清楚这个方向。

### 3.3 完全背包

反过来，如果每件物品**有无限件**（完全背包），正序恰好就是对的：dp[w - wt[i]] 已经可以包含第 i 件，再选一次，正是我们要的。

```c
for (int i = 0; i < n; ++i)
    for (int w = wt[i]; w <= W; ++w)            // 正序：同一件物品可以选多次
        dp[w] = max(dp[w], dp[w - wt[i]] + val[i]);
```

上一篇的零钱兑换就是一个完全背包：每种硬币可以用任意多枚。

| | 0-1 背包 | 完全背包 |
|---|---|---|
| 每件物品 | 最多选一次 | 可以选任意多次 |
| 一维写法中容量的方向 | 从大到小 | 从小到大 |

## 4. 换个样子的背包：分割等和子集

> 给一个正整数数组，能不能把它分成两个子集，两边的和相等？

{1, 5, 11, 5}：总和 22，{11} 和 {1, 5, 5} 都是 11，可以。{1, 2, 3, 5}：总和 11 是奇数，不可以。

两边相等，就是要从数组中**选出一些数，和恰好是总和的一半**。每个数选或不选：这是一个 0-1 背包，容量是总和的一半，只是问的不是「最大价值」，而是「能不能正好装满」：

```c
// ok[s]：能否凑出和 s
bool ok[target + 1] = {true};               // ok[0] = true：什么都不选，和是 0
for (int i = 0; i < n; ++i)
    for (int s = target; s >= a[i]; --s)    // 每个数只能用一次：倒序
        ok[s] = ok[s] || ok[s - a[i]];
return ok[target];
```

```
{1,5,11,5}：可以
{1,2,3,5}：不可以
```

很多问题初看和背包无关，但只要能说成「每个东西选或不选，受某个总量的限制」，就可以套用背包的方法。

## 5. 常见错误

1. **一维 0-1 背包正序遍历容量**。物品被重复选择，结果偏大。
2. **二维写法中忘了「不选」的情况**，只在放得下时赋值，放不下时 dp[i][w] 是随机值。
3. **容量很大时直接开 O(nW) 的二维数组**。W = 10⁹ 时这个方法就不能用了，背包 DP 的复杂度和 W 有关，这叫**伪多项式**时间。
4. **分割等和子集中忘了判断总和的奇偶**。奇数直接返回 false，否则 sum / 2 会向下取整，结果错误。
5. **想还原方案却用了一维数组**。要还原方案，保留二维表，或者额外记录每一步的选择。

## 6. 练习

- [分割等和子集](/problems/partition-equal-subset-sum/)：本文的例子
- [零钱兑换](/problems/coin-change/)：完全背包求最少数量
- [完全平方数](/problems/perfect-squares/)：完全背包，物品是 1、4、9、16……
- [单词拆分](/problems/word-break/)：「物品」是字典中的单词，「容量」是字符串的长度

思考题：

1. 如果每件物品最多可以选 k[i] 次（多重背包），最直接的做法是什么？复杂度是多少？
2. 背包问题如果要求「恰好装满」并且价值最大，初始值应该怎么设？
3. 为什么 0-1 背包的二维写法中，w 的遍历方向无所谓？

<details>
<summary>点击查看答案</summary>

1. 把第 i 件物品看成 k[i] 件一样的物品，每件都做一次 0-1 背包。复杂度是 O(W × Σk[i])。优化的方法是**二进制拆分**：把 k 件拆成 1、2、4、……、剩余这几份，每份当成一件物品，任意数量 0～k 都能由其中若干份组合出来，物品数变成 O(log k)。

2. dp[0] = 0，其余 dp[w] 都设为「不可能」（比如负无穷），表示容量为 w 时还没有办法正好装满。转移时，只有 dp[w - wt[i]] 不是「不可能」才能转移。最后看 dp[W] 是不是「不可能」。原来的写法初始值全是 0，表示「装不满也行」，求的是「不超过 W」的最大价值。

3. 二维写法中，第 i 行只读第 i-1 行，写第 i 行，读和写在不同的行中，不会互相干扰，所以先算哪一格都行。一维写法把读和写放进了同一个数组，顺序才变得重要。

</details>

下一篇：[动态规划（三）：序列问题](/posts/algo-dp-sequence/)。
