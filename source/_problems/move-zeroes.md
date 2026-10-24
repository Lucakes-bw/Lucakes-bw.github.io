---
id: 4
title: 移动零
difficulty: easy
tags: [数组, 双指针]
related: [ds-seqlist]
leetcode: https://leetcode.cn/problems/move-zeroes/
function: moveZeroes
params: { nums: "int[]" }
returns: void
output: nums
examples:
  - in: [[0, 1, 0, 3, 12]]
    out: [1, 3, 12, 0, 0]
  - in: [[0]]
    out: [0]
tests:
  - { in: [[1, 2, 3]], out: [1, 2, 3] }
  - { in: [[0, 0, 1]], out: [1, 0, 0] }
  - { in: [[4, 0, 0, 0, 5, 0, 6]], out: [4, 5, 6, 0, 0, 0, 0] }
  - { in: [[-1, 0, -2, 0, 0, 3]], out: [-1, -2, 3, 0, 0, 0] }
  - { in: [[0, 0, 0, 0]], out: [0, 0, 0, 0] }
  - { in: [[2, 1]], out: [2, 1] }
---
给定数组 `nums`，把其中所有的 `0` 移到数组末尾，同时保持非零元素原来的**相对顺序**。

必须**原地**修改数组，不能新建一个数组再拷贝回去。函数不需要返回值，判题时检查调用之后的 `nums`。

**约束**

- `1 <= nums.length <= 10^4`
- `-2^31 <= nums[i] <= 2^31 - 1`

**提示**：用一个指针记录「下一个非零元素应该放的位置」，扫描一遍就够了。
