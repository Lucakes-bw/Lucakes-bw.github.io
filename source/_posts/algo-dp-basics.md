---
title: 动态规划（一）：从记忆化搜索到递推
date: 2026-11-05 10:10:00
updated: 2026-11-05 10:10:00
categories:
  - 数据结构
  - 算法设计
tags:
  - 算法
  - 动态规划
order: 2
description: 直接按定义递归计算斐波那契数，F(45) 要调用 36 亿次函数、用 2.9 秒；记下算过的结果，只要 89 次。动态规划就是从这里出发的：找出「大问题的答案由哪些小问题的答案推出来」，每个小问题只算一次。本文从记忆化搜索走到递推，总结出定义状态、写出转移、确定边界的三步法，用它解决零钱兑换（贪心在这里会出错）和最小路径和。
disableNunjucks: true
viz: [ds-algo]
---

> **本文目标**：理解重叠子问题为什么让递归变得极慢；会用记忆化搜索消除重复计算；会把记忆化搜索改写成递推；掌握动态规划的三步：状态、转移、边界；知道动态规划和贪心的区别。
>
> **前置知识**：[上一篇](/posts/algo-backtracking/)；《复杂度分析》。

## 1. 斐波那契：同一个问题被算了几十亿次

斐波那契数列：F(0) = 0，F(1) = 1，F(n) = F(n-1) + F(n-2)。照着定义写：

```c
static long long fibNaive(int n)            // 直接按定义递归
{
    if (n <= 1) return n;
    return fibNaive(n - 1) + fibNaive(n - 2);
}
```

统计一下函数被调用了多少次：

```
F(10) = 55，调用 177 次，0.000 秒
F(20) = 6765，调用 21891 次，0.000 秒
F(30) = 832040，调用 2692537 次，0.013 秒
F(35) = 9227465，调用 29860703 次，0.049 秒
F(40) = 102334155，调用 331160281 次，0.265 秒
F(45) = 1134903170，调用 3672623805 次，2.924 秒
```

n 每增加 5，调用次数大约变成 11 倍。F(45) 调用了 **36.7 亿次**。原因画出调用树就清楚了：

<figure class="diagram">
<svg viewBox="0 0 700 300" role="img" aria-label="F(5) 直接递归的调用树：F(5) 调用 F(4) 和 F(3)，F(4) 又调用 F(3) 和 F(2)……F(3) 被计算了两次，F(2) 三次，一共 15 次调用">
  <defs><marker id="fb-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="fb-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <line x1="96.0" y1="207" x2="60" y2="233" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="96.0" y1="207" x2="132" y2="233" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="150.0" y1="151" x2="96.0" y2="177" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="150.0" y1="151" x2="204" y2="177" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="312.0" y1="151" x2="276" y2="177" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="312.0" y1="151" x2="348" y2="177" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="231.0" y1="95" x2="150.0" y2="121" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="231.0" y1="95" x2="312.0" y2="121" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="456.0" y1="151" x2="420" y2="177" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="456.0" y1="151" x2="492" y2="177" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="510.0" y1="95" x2="456.0" y2="121" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="510.0" y1="95" x2="564" y2="121" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="370.5" y1="39" x2="231.0" y2="65" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="370.5" y1="39" x2="510.0" y2="65" style="stroke:var(--border-2);stroke-width:1.5"/><circle cx="60" cy="248" r="15" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="60.0" y="252.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">F1</text><circle cx="132" cy="248" r="15" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="132.0" y="252.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">F0</text><circle cx="96.0" cy="192" r="15" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="96.0" y="196.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">F2</text><circle cx="204" cy="192" r="15" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="204.0" y="196.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">F1</text><circle cx="150.0" cy="136" r="15" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="150.0" y="140.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">F3</text><circle cx="276" cy="192" r="15" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="276.0" y="196.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">F1</text><circle cx="348" cy="192" r="15" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="348.0" y="196.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">F0</text><circle cx="312.0" cy="136" r="15" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="312.0" y="140.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">F2</text><circle cx="231.0" cy="80" r="15" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="231.0" y="84.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">F4</text><circle cx="420" cy="192" r="15" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="420.0" y="196.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">F1</text><circle cx="492" cy="192" r="15" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="492.0" y="196.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">F0</text><circle cx="456.0" cy="136" r="15" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="456.0" y="140.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">F2</text><circle cx="564" cy="136" r="15" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="564.0" y="140.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">F1</text><circle cx="510.0" cy="80" r="15" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="510.0" y="84.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">F3</text><circle cx="370.5" cy="24" r="15" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="370.5" y="28.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">F5</text><text x="14.0" y="292.0" text-anchor="start" style="fill:var(--muted);font-size:11px">黄色：F(3) 算了 2 次；红框：F(2) 算了 3 次。记忆化以后，每个 F(k) 只算一次</text>
  </g>
</svg>
</figure>

F(5) 调用了 F(4) 和 F(3)；F(4) 又调用了 F(3)：**F(3) 被算了两次**，F(2) 被算了三次，越往下重复得越多。F(5) 一共调用了 15 次，而不同的问题只有 F(0) 到 F(5) 六个。

这种现象叫作**重叠子问题**：递归分解出来的小问题，有大量是相同的。上一篇的回溯中，每个结点代表一个不同的局面，不存在这个问题；这里不一样，同一个 F(3) 不管从哪里调用，答案都相同，没有必要算第二次。

## 2. 记忆化搜索

算过的结果记下来，下次直接用：

```c
static long long memo[100];
static long long fibMemo(int n)             // 记忆化搜索：算过的结果记下来
{
    if (n <= 1) return n;
    if (memo[n] != 0) return memo[n];
    return memo[n] = fibMemo(n - 1) + fibMemo(n - 2);
}
```

```
F(10) = 55，调用 19 次
F(50) = 12586269025，调用 99 次
F(90) = 2880067194370816120，调用 179 次
```

每个 F(k) 只真正计算一次，调用次数是 2n - 1，从指数级变成了线性。这就是**记忆化搜索**：递归的结构不变，只是加了一张备忘录。

## 3. 递推

记忆化搜索是从上往下的：要算 F(n)，先去算 F(n-1)……一直递归到 F(1)。既然最后总要把 F(0) 到 F(n) 都算一遍，不如**从下往上**直接算：

```c
static long long fibIter(int n)             // 递推：从小到大算，只保留最后两个
{
    long long a = 0, b = 1;                 // a = F(i)，b = F(i+1)
    for (int i = 0; i < n; ++i) {
        long long t = a + b;
        a = b;
        b = t;
    }
    return a;
}
```

没有递归，没有函数调用的开销，也不会因为 n 太大而栈溢出。而且算 F(i) 只需要 F(i-1) 和 F(i-2)，更早的可以扔掉，空间从 O(n) 降到 O(1)。

这就是**动态规划**（Dynamic Programming，DP）：把问题分解成子问题，按照「先小后大」的顺序计算，每个子问题只算一次，结果存在表中。记忆化搜索和递推是它的两种写法，前者更接近直觉，后者通常更快、更省空间。

## 4. 三步法：零钱兑换

真正的 DP 问题不会直接告诉你递推式，要自己找出来。来看一个经典问题：

> 硬币的面值是 1、3、4，凑出金额 6，最少要几枚硬币？

### 4.1 贪心为什么不行

直觉的做法是**贪心**：每次拿面值最大、又不超过剩余金额的硬币。6 → 拿 4，剩 2 → 拿 1，剩 1 → 拿 1，一共 3 枚。

但 3 + 3 = 6，只要 2 枚。贪心错了：先拿了 4，看起来离目标最近，却让剩下的 2 只能用两个 1 来凑。

程序对金额 1 到 1000 逐个比较：面值 {1, 3, 4} 时，**贪心有 249 个金额不是最优的**；换成人民币的面值 {1, 5, 10, 20, 50, 100}，贪心在 1 到 1000 中一次都没有错。贪心能不能用，取决于问题的结构（第 5 篇会讲怎样判断），不能想当然。

### 4.2 第一步：定义状态

找到一个能描述子问题的量。这里的子问题是「凑出更小的金额」：

> **dp[x] = 凑出金额 x 最少要几枚硬币**

最终要的答案是 dp[6]。

### 4.3 第二步：写出转移

凑出金额 x 的最后一枚硬币，可能是 1、3 或 4。如果最后一枚是 c，那么前面的硬币凑出了 x - c，最少用了 dp[x - c] 枚。三种可能中取最小的：

> **dp[x] = min(dp[x - 1], dp[x - 3], dp[x - 4]) + 1**（只考虑 c ≤ x、并且 x - c 能凑出来的情况）

这一步是动态规划的核心：**大问题的最优解，由小问题的最优解推出来**。这个性质叫作**最优子结构**：凑出 x 的最优方案中，去掉最后一枚硬币，剩下的一定也是凑出 x - c 的最优方案（否则换成更优的，整体就更优了）。

### 4.4 第三步：确定边界和计算顺序

- 边界：dp[0] = 0，凑出 0 元不需要硬币；
- 顺序：dp[x] 依赖更小的金额，所以 x 从小到大算。

```c
// 动态规划：dp[x] = 凑出金额 x 最少要几枚硬币
static int coinDP(const int* coins, int k, int amount, int* dp)
{
    dp[0] = 0;
    for (int x = 1; x <= amount; ++x) {
        dp[x] = INT_MAX;                    // 表示凑不出来
        for (int i = 0; i < k; ++i) {
            if (coins[i] <= x && dp[x - coins[i]] != INT_MAX && dp[x - coins[i]] + 1 < dp[x]) {
                dp[x] = dp[x - coins[i]] + 1;
            }
        }
    }
    return dp[amount] == INT_MAX ? -1 : dp[amount];
}
```

```
x    0  1  2  3  4  5  6
dp   0  1  2  1  1  2  2
```

dp[6] = 2。下面的演示一格一格地填这张表，每一格看三个候选的位置：

<div class="dsviz" data-viz="algo-coin"></div>

时间复杂度是 O(金额 × 面值的种数)。和回溯的指数级相比，这是质的变化：回溯要枚举所有的凑法，而 DP 只关心「凑出每个金额最少要几枚」，不管具体是怎么凑的。

## 5. 二维的状态：最小路径和

> 一个 3×4 的网格，每格有一个数。从左上角走到右下角，每次只能向右或向下，经过的数字之和最小是多少？

```
1 3 1 2
1 5 1 3
4 2 1 1
```

三步：

1. **状态**：dp[r][c] = 从左上角走到 (r, c) 的最小路径和；
2. **转移**：走到 (r, c) 的上一步，要么在上面 (r-1, c)，要么在左边 (r, c-1)：dp[r][c] = min(dp[r-1][c], dp[r][c-1]) + g[r][c]；
3. **边界**：第一行只能从左边来，第一列只能从上面来；顺序：从上到下、从左到右。

```c
for (int r = 0; r < rows; ++r)
    for (int c = 0; c < cols; ++c) {
        if (r == 0 && c == 0) dp[r][c] = g[0][0];
        else if (r == 0) dp[r][c] = dp[r][c - 1] + g[r][c];      // 第一行只能从左边来
        else if (c == 0) dp[r][c] = dp[r - 1][c] + g[r][c];      // 第一列只能从上边来
        else dp[r][c] = min(dp[r - 1][c], dp[r][c - 1]) + g[r][c];
    }
```

算出来的表：

```
  1  4  5  7
  2  7  6  9
  6  8  7  8
```

最小路径和是 dp 的右下角，8。要还原出路径，从右下角往回走，每次走向上面和左边中 dp 较小的那一格：8 ← 7 ← 6 ← 5 ← 4 ← 1，也就是从左上角出发，右、右、下、下、右，经过的数是 1 + 3 + 1 + 1 + 1 + 1 = 8。

## 6. 什么问题可以用动态规划

1. **最优子结构**：大问题的最优解包含小问题的最优解；
2. **重叠子问题**：分解出来的子问题有大量重复。没有重复的话，DP 和普通的递归一样快（比如归并排序也是分解成子问题，但没有重复，那叫分治）；
3. **无后效性**：一个状态一旦确定，后面怎么走只和这个状态有关，和「怎么到达这个状态的」无关。比如最小路径和中，走到 (r, c) 以后，后面的最优走法和前面怎么走过来的无关。

遇到求最大、最小、方案数，并且 n 是几百到几千的问题，就可以想一想能不能用 DP。最难的是第一步：**怎样定义状态**。状态定义好了，转移通常就自然出来了。下两篇会看更多的例子。

## 7. 常见错误

1. **初始值设错**。求最小值时初始化成 0，`min` 永远是 0。用一个足够大的值（`INT_MAX`）表示「还没有算出来」或「做不到」，并且在加 1 之前检查它，否则 `INT_MAX + 1` 溢出。
2. **计算顺序不对**。用到的子问题还没有算。递推时要保证依赖的格子先算好。
3. **记忆化用 0 表示「没算过」，而 0 恰好是合法的答案**。比如某个子问题的答案就是 0，每次都会重新计算，退化成指数级。用 -1，或者一个单独的 `bool` 数组。
4. **把贪心当成 DP**。贪心只看当前，DP 考虑所有的子问题；贪心对不对，要证明，或者至少和 DP 的结果对比。
5. **状态定义不完整**，存在后效性。比如路径问题中如果「不能连续向右走两次」，只记录位置就不够了，还要记录上一步的方向。

## 8. 练习

刷题板块中的入门 DP：

- [爬楼梯](/problems/climbing-stairs/)：斐波那契的换个说法
- [零钱兑换](/problems/coin-change/)：本文的例子
- [最小路径和](/problems/minimum-path-sum/)、[不同路径](/problems/unique-paths/)
- [打家劫舍](/problems/house-robber/)：相邻的房子不能同时偷，状态怎么定义？
- [最大子数组和](/problems/maximum-subarray/)：dp[i] = 以 i 结尾的最大和
- [单词拆分](/problems/word-break/)

思考题：

1. 零钱兑换中，如果要求的是「凑出金额 x 有多少种不同的方法」（不考虑顺序），状态和转移要怎么改？
2. 最小路径和只用一个一维数组可以吗？
3. 打家劫舍：一排房子，每个房子里有一些钱，相邻的两个不能都偷。写出状态和转移。

<details>
<summary>点击查看答案</summary>

1. 改成 ways[x] = 凑出 x 的方法数，ways[0] = 1。为了不把 1+3 和 3+1 算成两种，要**外层循环硬币、内层循环金额**：`for (c : coins) for (x = c; x <= amount; ++x) ways[x] += ways[x - c];`。这样每种组合中的硬币是按面值的顺序加入的，只会被数一次。如果外层循环金额，算出来的是排列数（考虑顺序）。这个区别和下一篇背包问题的遍历顺序是同一回事。

2. 可以。dp[r][c] 只依赖上一行的 dp[r-1][c] 和这一行的 dp[r][c-1]。用一个长度为列数的数组 dp，从左到右更新：更新前 dp[c] 是上一行的值（上面），dp[c-1] 已经是这一行的值（左边），正好：`dp[c] = min(dp[c], dp[c-1]) + g[r][c]`。

3. dp[i] = 只考虑前 i 个房子能偷到的最多的钱。第 i 个房子要么不偷（dp[i-1]），要么偷（那第 i-1 个就不能偷：dp[i-2] + money[i]）。dp[i] = max(dp[i-1], dp[i-2] + money[i])，dp[0] = 0，dp[1] = money[1]。和斐波那契的结构一样，只需要保留最后两个值。

</details>

下一篇：[动态规划（二）：背包问题](/posts/algo-dp-knapsack/)。
