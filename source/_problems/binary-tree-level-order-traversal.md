---
id: 8
title: 二叉树的层序遍历
difficulty: medium
tags: [树, 二叉树, 队列, BFS]
related: [ds-binary-tree, ds-queue]
leetcode: https://leetcode.cn/problems/binary-tree-level-order-traversal/
function: levelOrder
params: { root: TreeNode }
returns: "int[][]"
examples:
  - in: [[3, 9, 20, null, null, 15, 7]]
    out: [[3], [9, 20], [15, 7]]
  - in: [[1]]
    out: [[1]]
  - in: [[]]
    out: []
tests:
  - { in: [[1, 2, 3, 4, 5, 6, 7]], out: [[1], [2, 3], [4, 5, 6, 7]] }
  - { in: [[1, 2, null, 3, null, 4]], out: [[1], [2], [3], [4]] }
  - { in: [[1, null, 2, 3]], out: [[1], [2], [3]] }
  - { in: [[5, 4, 8, 11, null, 13, 4, 7, 2, null, null, 5, 1]], out: [[5], [4, 8], [11, 13, 4], [7, 2, 5, 1]] }
---
给定二叉树的根节点 `root`，按**从上到下、每层从左到右**的顺序访问所有节点，每一层的节点值放在一个数组里，返回所有层组成的二维数组。

**约束**

- 节点数在 `[0, 2000]` 范围内
- `-1000 <= Node.val <= 1000`

**提示**：用队列做广度优先搜索。处理每一层之前先记下队列的长度，就知道这一层有几个节点。
