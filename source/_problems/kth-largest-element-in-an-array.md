---
id: 9
title: 数组中的第 K 个最大元素
difficulty: medium
tags: [数组, 堆, 快速选择]
related: [ds-heap, ds-quick-merge]
leetcode: https://leetcode.cn/problems/kth-largest-element-in-an-array/
function: findKthLargest
params: { nums: "int[]", k: int }
returns: int
examples:
  - in: [[3, 2, 1, 5, 6, 4], 2]
    out: 5
    explain: 从大到小排是 6, 5, 4, 3, 2, 1，第 2 个是 5。
  - in: [[3, 2, 3, 1, 2, 4, 5, 5, 6], 4]
    out: 4
    explain: 重复的元素分别计数：6, 5, 5, 4, …，第 4 个是 4。
tests:
  - { in: [[1], 1], out: 1 }
  - { in: [[2, 1], 2], out: 1 }
  - { in: [[7, 7, 7, 7], 3], out: 7 }
  - { in: [[-1, -5, -3, -2], 1], out: -1 }
  - { in: [[10, 9, 8, 7, 6, 5, 4, 3, 2, 1], 10], out: 1 }
  - { in: [[3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5], 6], out: 4 }
---
给定整数数组 `nums` 和整数 `k`，返回数组排序后的**第 `k` 个最大**的元素。

注意是排序后的第 `k` 大，不是第 `k` 个不同的值：重复的元素要分别计数。

**约束**

- `1 <= k <= nums.length <= 10^5`
- `-10^4 <= nums[i] <= 10^4`

**进阶**：直接排序是 O(n log n)。试试维护一个大小为 `k` 的小根堆（O(n log k)），或者用快速排序的划分思路做「快速选择」（平均 O(n)）。
