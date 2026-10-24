---
id: 1
title: 两数之和
difficulty: easy
tags: [数组, 哈希表]
related: [ds-hash-table]
leetcode: https://leetcode.cn/problems/two-sum/
function: twoSum
params: { nums: "int[]", target: int }
returns: "int[]"
compare: unordered
examples:
  - in: [[2, 7, 11, 15], 9]
    out: [0, 1]
    explain: nums[0] + nums[1] = 2 + 7 = 9，所以返回 [0, 1]。
  - in: [[3, 2, 4], 6]
    out: [1, 2]
  - in: [[3, 3], 6]
    out: [0, 1]
tests:
  - { in: [[1, 5, 9, 13], 14], out: [1, 2] }
  - { in: [[-3, 4, 3, 90], 0], out: [0, 2] }
  - { in: [[0, 4, 3, 0], 0], out: [0, 3] }
  - { in: [[1000000000, 3, -999999993], 7], out: [0, 2] }
  - { in: [[5, 75, 25], 100], out: [1, 2] }
  - { in: [[2, 5, 5, 11], 10], out: [1, 2] }
---
给定一个整数数组 `nums` 和一个目标值 `target`，在数组中找出**和等于 `target` 的两个数**，返回它们的下标。

每组输入保证**恰好有一个**答案；同一个元素不能使用两次。返回的两个下标顺序不限。

**约束**

- `2 <= nums.length <= 10^4`
- `-10^9 <= nums[i], target <= 10^9`
- 只存在一个有效答案

**进阶**：暴力枚举是 O(n²)，你能用哈希表做到 O(n) 吗？
