---
id: 76
title: 完全平方数
difficulty: medium
tags: [数学, 动态规划, BFS]
leetcode: https://leetcode.cn/problems/perfect-squares/
function: numSquares
params: { n: int }
returns: int
examples:
  - in: [12]
    out: 3
    explain: "12 = 4 + 4 + 4。"
  - in: [13]
    out: 2
    explain: "13 = 4 + 9。"
tests:
  - { in: [1], out: 1 }
  - { in: [2], out: 2 }
  - { in: [3], out: 3 }
  - { in: [4], out: 1 }
  - { in: [7], out: 4 }
  - { in: [28], out: 4 }
  - { in: [43], out: 3 }
  - { in: [100], out: 1 }
  - { in: [9973], out: 2 }
  - { in: [9999], out: 4 }
  - { in: [10000], out: 1 }
  - { in: [8935], out: 4 }
  - { in: [6175], out: 4 }
---
给定正整数 `n`，返回和为 `n` 的**完全平方数**的最少个数。

完全平方数是某个整数的平方，比如 `1`、`4`、`9`、`16`。同一个完全平方数可以使用多次。

**约束**

- `1 <= n <= 10^4`

**提示**：完全背包：`f[i] = min(f[i - j*j] + 1)`，`j*j <= i`。也可以把它看成从 `n` 出发、每步减去一个完全平方数的最短路，用 BFS。（数学上还有四平方和定理：答案一定不超过 4。）
