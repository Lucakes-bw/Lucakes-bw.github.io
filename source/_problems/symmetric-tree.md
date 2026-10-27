---
id: 40
title: 对称二叉树
difficulty: easy
tags: [树, 二叉树, DFS, BFS]
related: [ds-binary-tree]
leetcode: https://leetcode.cn/problems/symmetric-tree/
function: isSymmetric
params: { root: TreeNode }
returns: bool
examples:
  - in: [[1, 2, 2, 3, 4, 4, 3]]
    out: true
  - in: [[1, 2, 2, null, 3, null, 3]]
    out: false
tests:
  - { in: [[1]], out: true }
  - { in: [[1, 2]], out: false }
  - { in: [[1, 2, 2]], out: true }
  - { in: [[1, 2, 3]], out: false }
  - { in: [[1, 2, 2, 2, null, 2]], out: false }
  - { in: [[1, 2, 2, null, 3, 3]], out: true }
  - { in: [[2, 3, 3, 4, 5, 5, 4, null, null, 8, 9, 9, 8]], out: true }
  - { in: [[2, 3, 3, 4, 5, 5, 4, null, null, 8, 9, 8, 9]], out: false }
  - { in: [[5, 6, 6, 6, 1, 1, 6, 3, 2, 4, 2, 2, 4, 2, 3, 9, 7, 7, null, 7, 3, 5, null, null, 5, 3, 7, null, 7, 7, 9, 4, null, 1, 1, null, null, 1, null, 8, 9, 3, null, null, 3, 9, 8, null, 1, null, null, 1, 1, null, 4, null, null, 4, null, 8, null, 4, 8, 6, null, null, null, null, null, null, null, null, null, null, 6, 8, 4, null, 8, null, 4, null, null, null, null, 2, null, 4, 7, null, null, null, null, null, null, null, null, 7, 4, null, 2, null, null, null, null, null, null, 5, null, null, 5, null, null, null, null, 2, null, null, 2]], out: true }
  - { in: [[5, 6, 6, 6, 1, 1, 6, 3, 2, 4, 2, 2, 4, 2, 3, 9, 7, 7, null, 7, 3, 5, null, null, 5, 3, 7, null, 7, 7, 9, 4, null, 1, 1, null, null, 1, null, 8, 9, 3, null, null, 3, 9, 8, null, 1, null, null, 1, 1, null, 4, null, null, 4, null, 8, null, 4, 8, 6, null, null, null, null, null, null, null, null, null, null, 6, 8, 4, null, 8, null, 4, null, null, null, null, 2, null, 4, 7, null, null, null, null, null, null, null, null, 7, 4, null, 2, null, null, null, null, null, null, 5, null, null, 5, null, null, null, null, 2, null, null, 3]], out: false }
---
给定二叉树的根节点 `root`，判断它是不是**轴对称**的：沿着通过根节点的竖直线翻折，左右两边的结构和节点值完全重合。

**约束**

- 节点数在 `[1, 1000]` 范围内
- `-100 <= Node.val <= 100`

**提示**：写一个函数判断两棵树是否互为镜像：根的值相等，并且 A 的左子树和 B 的右子树互为镜像、A 的右子树和 B 的左子树互为镜像。
