---
id: 83
title: 不同路径
difficulty: medium
tags: [数学, 动态规划, 组合数学]
leetcode: https://leetcode.cn/problems/unique-paths/
function: uniquePaths
params: { m: int, n: int }
returns: int
examples:
  - in: [3, 7]
    out: 28
  - in: [3, 2]
    out: 3
    explain: "从左上角出发，可以走 右→下→下、下→下→右、下→右→下，共 3 条。"
tests:
  - { in: [1, 1], out: 1 }
  - { in: [1, 100], out: 1 }
  - { in: [100, 1], out: 1 }
  - { in: [2, 2], out: 2 }
  - { in: [7, 3], out: 28 }
  - { in: [10, 10], out: 48620 }
  - { in: [23, 12], out: 193536720 }
  - { in: [16, 16], out: 155117520 }
  - { in: [17, 18], out: 1166803110 }
---
一个机器人位于 `m x n` 网格的左上角，每次只能**向下**或者**向右**移动一步。机器人要到达网格的右下角，一共有多少条不同的路径？

题目保证答案不超过 `2 * 10^9`。

**约束**

- `1 <= m, n <= 100`

**提示**：到达格子 `(i, j)` 只能从上面或左边来，`f[i][j] = f[i-1][j] + f[i][j-1]`，用一维数组滚动即可。从组合数学看，答案就是在 `m + n - 2` 步中选出 `m - 1` 步向下：C(m + n - 2, m - 1)。
