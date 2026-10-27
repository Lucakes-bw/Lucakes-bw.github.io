---
id: 39
title: 翻转二叉树
difficulty: easy
tags: [树, 二叉树, DFS, BFS]
related: [ds-binary-tree]
leetcode: https://leetcode.cn/problems/invert-binary-tree/
function: invertTree
params: { root: TreeNode }
returns: TreeNode
examples:
  - in: [[4, 2, 7, 1, 3, 6, 9]]
    out: [4, 7, 2, 9, 6, 3, 1]
  - in: [[2, 1, 3]]
    out: [2, 3, 1]
  - in: [[]]
    out: []
tests:
  - { in: [[1]], out: [1] }
  - { in: [[1, 2]], out: [1, null, 2] }
  - { in: [[1, null, 2]], out: [1, 2] }
  - { in: [[1, 2, 3, 4, null, null, 5]], out: [1, 3, 2, 5, null, null, 4] }
  - { in: [[1, 2, null, 3, null, 4, null, 5, null, 6, null, 7, null, 8, null, 9, null, 10, null, 11, null, 12, null, 13, null, 14, null, 15, null, 16, null, 17, null, 18, null, 19, null, 20, null, 21, null]], out: [1, null, 2, null, 3, null, 4, null, 5, null, 6, null, 7, null, 8, null, 9, null, 10, null, 11, null, 12, null, 13, null, 14, null, 15, null, 16, null, 17, null, 18, null, 19, null, 20, null, 21] }
  - { in: [[34, -58, 81, 49, -77, -70, -14, -71, -23, 17, 44, -44, 36, null, 38, -86, -31, -70, -89, -62, -14, 57, -92, -47, 64, null, -21, null, null, null, 31, -77, null, -35, -99, -13, null, 96, 49, -3, 11, 44, 49, 94, -64, -65, -5, -79, null, 46, null, 51, -19, -1, null, 12, 87, -28, 59, 32, null, -72, null, null, -75, 62, null, 31, null, null, null, null, null, null, null, null, 91, null, null, 61, null, -42, -88, -80, null, 55, 24, null, null, -63, null, -84, -6, 92, 92, 5, 8, 92, -84, null, -32, 67, null, null, null, 36, -48, null, null, null, null, null, -50, null, null, null, -94, null, null, -20, null, null, null, null, null, -55, null, null, null, 68, null, null, null, null, 48, -77, 10, null, null, null, -38, null, -24, 78, null, -53, 97, null, null, 12, 78, null, null, null, 24, 38, null, null, null, null, null, null, 4, 82, -39, null, null, 43, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, null, -46, null, null, -89, 1, -30, null, null, null, null, null, null, 82]], out: [34, 81, -58, -14, -70, -77, 49, 38, null, 36, -44, 44, 17, -23, -71, null, null, -21, null, 64, -47, -92, 57, -14, -62, -89, -70, -31, -86, null, 46, null, -79, -5, -65, -64, 94, 49, 44, 11, -3, 49, 96, null, -13, -99, -35, null, -77, 31, null, null, -80, -88, -42, null, 61, null, null, 91, null, null, null, null, null, null, null, null, 31, null, 62, -75, null, null, -72, null, 32, 59, -28, 87, 12, null, -1, -19, 51, null, null, -94, null, null, null, -50, null, null, null, null, null, -48, 36, null, null, null, 67, -32, null, -84, 92, 8, 5, 92, 92, -6, -84, null, -63, null, null, 24, 55, null, null, 78, 12, null, null, 97, -53, null, 78, -24, null, -38, null, null, null, 10, -77, 48, null, null, null, null, 68, null, null, null, -55, null, null, null, null, null, -20, null, null, null, null, null, null, null, null, null, null, null, 43, null, null, -39, 82, 4, null, null, null, null, null, null, 38, 24, null, null, -30, 1, -89, null, null, -46, null, null, null, null, null, null, null, 82] }
---
给定二叉树的根节点 `root`，把它**左右翻转**（每个节点的左右子树互换），返回翻转后的根节点。

**约束**

- 节点数在 `[0, 100]` 范围内
- `-100 <= Node.val <= 100`

**提示**：递归：先翻转左右两棵子树，再交换它们。也可以用队列逐个节点交换左右孩子。
