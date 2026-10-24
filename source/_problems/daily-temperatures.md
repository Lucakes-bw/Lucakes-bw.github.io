---
id: 6
title: 每日温度
difficulty: medium
tags: [栈, 单调栈, 数组]
related: [ds-monotonic]
leetcode: https://leetcode.cn/problems/daily-temperatures/
function: dailyTemperatures
params: { temperatures: "int[]" }
returns: "int[]"
examples:
  - in: [[73, 74, 75, 71, 69, 72, 76, 73]]
    out: [1, 1, 4, 2, 1, 1, 0, 0]
    explain: 第 3 天是 75 度，要等到第 7 天的 76 度才更暖和，所以答案是 4。
  - in: [[30, 40, 50, 60]]
    out: [1, 1, 1, 0]
  - in: [[30, 60, 90]]
    out: [1, 1, 0]
tests:
  - { in: [[50]], out: [0] }
  - { in: [[90, 80, 70, 60]], out: [0, 0, 0, 0] }
  - { in: [[55, 55, 55, 56]], out: [3, 2, 1, 0] }
  - { in: [[34, 80, 80, 34, 34, 80, 80, 80, 80, 34]], out: [1, 0, 0, 2, 1, 0, 0, 0, 0, 0] }
  - { in: [[89, 62, 70, 58, 47, 47, 46, 76, 100, 70]], out: [8, 1, 5, 4, 3, 2, 1, 1, 0, 0] }
---
给定一个整数数组 `temperatures`，表示每天的气温。返回一个数组 `answer`，其中 `answer[i]` 表示：从第 `i` 天起，**至少还要等几天**才会出现比当天更高的气温。如果之后都不会更高，`answer[i]` 为 `0`。

**约束**

- `1 <= temperatures.length <= 10^5`
- `30 <= temperatures[i] <= 100`

**提示**：对每一天都往后扫一遍是 O(n²)。用一个栈存「还没找到更高气温的日子」，栈里的温度从栈底到栈顶是单调不升的，每天只进出栈一次，就是 O(n)。
