---
id: 5
title: 二分查找
difficulty: easy
tags: [数组, 二分查找]
related: [ds-binary-search]
leetcode: https://leetcode.cn/problems/binary-search/
function: search
params: { nums: "int[]", target: int }
returns: int
examples:
  - in: [[-1, 0, 3, 5, 9, 12], 9]
    out: 4
    explain: 9 在数组里，下标是 4。
  - in: [[-1, 0, 3, 5, 9, 12], 2]
    out: -1
    explain: 2 不在数组里，返回 -1。
tests:
  - { in: [[5], 5], out: 0 }
  - { in: [[5], -5], out: -1 }
  - { in: [[1, 3], 3], out: 1 }
  - { in: [[1, 3], 1], out: 0 }
  - { in: [[1, 3, 5, 7, 9, 11, 13], 13], out: 6 }
  - { in: [[1, 3, 5, 7, 9, 11, 13], 0], out: -1 }
  - { in: [[1, 3, 5, 7, 9, 11, 13], 14], out: -1 }
  - { in: [[-9999, -100, 0, 42, 9999], 42], out: 3 }
---
给定一个**升序**排列、元素各不相同的整数数组 `nums` 和目标值 `target`，在数组中查找 `target`：找到就返回它的下标，否则返回 `-1`。

要求时间复杂度为 O(log n)。

**约束**

- `1 <= nums.length <= 10^4`
- `-10^4 < nums[i], target < 10^4`
- `nums` 中的元素互不相同，且按升序排列

**提示**：注意区间的开闭和循环条件，`left <= right` 还是 `left < right`，要和区间的定义对应上。
