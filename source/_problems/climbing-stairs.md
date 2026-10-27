---
id: 73
title: 爬楼梯
difficulty: easy
tags: [动态规划, 数学, 记忆化搜索]
leetcode: https://leetcode.cn/problems/climbing-stairs/
function: climbStairs
params: { n: int }
returns: int
examples:
  - in: [2]
    out: 2
    explain: "1 + 1 或者一次爬 2 阶，共 2 种。"
  - in: [3]
    out: 3
    explain: "1 + 1 + 1、1 + 2、2 + 1，共 3 种。"
tests:
  - { in: [1], out: 1 }
  - { in: [4], out: 5 }
  - { in: [5], out: 8 }
  - { in: [10], out: 89 }
  - { in: [20], out: 10946 }
  - { in: [44], out: 1134903170 }
  - { in: [45], out: 1836311903 }
---
你在爬一段有 `n` 阶的楼梯，每次可以爬 `1` 阶或 `2` 阶。一共有多少种不同的方法可以爬到楼顶？

**约束**

- `1 <= n <= 45`

**提示**：最后一步要么爬 1 阶、要么爬 2 阶，所以 `f(n) = f(n - 1) + f(n - 2)`。只需要保存前两项。
