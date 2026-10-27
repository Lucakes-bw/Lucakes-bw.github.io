---
id: 77
title: 零钱兑换
difficulty: medium
tags: [数组, 动态规划, BFS]
leetcode: https://leetcode.cn/problems/coin-change/
function: coinChange
params: { coins: "int[]", amount: int }
returns: int
examples:
  - in: [[1, 2, 5], 11]
    out: 3
    explain: "11 = 5 + 5 + 1。"
  - in: [[2], 3]
    out: -1
  - in: [[1], 0]
    out: 0
tests:
  - { in: [[1], 1], out: 1 }
  - { in: [[1], 2], out: 2 }
  - { in: [[2], 1], out: -1 }
  - { in: [[2, 5, 10, 1], 27], out: 4 }
  - { in: [[186, 419, 83, 408], 6249], out: 20 }
  - { in: [[3, 7, 405, 436], 8839], out: 25 }
  - { in: [[2147483647], 2], out: -1 }
  - { in: [[5, 7], 1], out: -1 }
  - { in: [[5, 7], 24], out: 4 }
  - { in: [[411, 412, 413, 414, 415, 416, 417, 418, 419, 420, 421, 422], 9864], out: 24 }
  - { in: [[1, 2147483647], 10000], out: 10000 }
---
给定不同面额的硬币 `coins` 和一个总金额 `amount`，计算凑成总金额所需的**最少硬币个数**。每种硬币的数量不限。如果无论怎样都凑不出，返回 `-1`。

**约束**

- `1 <= coins.length <= 12`
- `1 <= coins[i] <= 2^31 - 1`
- `0 <= amount <= 10^4`

**提示**：完全背包：`f[x] = min(f[x - c] + 1)`，对每个面额 `c` 取最小值，`f[0] = 0`。注意面额可能非常大，计算 `x - c` 时别溢出。
