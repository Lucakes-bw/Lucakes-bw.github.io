---
id: 45
title: 二叉树的右视图
difficulty: medium
tags: [树, 二叉树, BFS, DFS]
related: [ds-binary-tree, ds-queue]
leetcode: https://leetcode.cn/problems/binary-tree-right-side-view/
function: rightSideView
params: { root: TreeNode }
returns: "int[]"
examples:
  - in: [[1, 2, 3, null, 5, null, 4]]
    out: [1, 3, 4]
    explain: "从右边看，每层能看到最右边的节点：1、3、4。"
  - in: [[1, 2, 3, 4, null, null, null, 5]]
    out: [1, 3, 4, 5]
    explain: "第 3、4 层最右边的节点在左子树里。"
  - in: [[1, null, 3]]
    out: [1, 3]
  - in: [[]]
    out: []
tests:
  - { in: [[1]], out: [1] }
  - { in: [[1, 2]], out: [1, 2] }
  - { in: [[1, 2, 3, 4]], out: [1, 3, 4] }
  - { in: [[1, 2, 3, null, 5, 6]], out: [1, 3, 6] }
  - { in: [[1, 2, null, 3, null, 4, null, 5, null, 6, null, 7, null, 8, null, 9, null, 10, null, 11, null, 12, null, 13, null, 14, null, 15, null, 16, null, 17, null, 18, null, 19, null, 20, null, 21, null, 22, null, 23, null, 24, null, 25, null, 26, null, 27, null, 28, null, 29, null, 30, null, 31, null, 32, null, 33, null, 34, null, 35, null, 36, null, 37, null, 38, null, 39, null, 40, null, 41, null, 42, null, 43, null, 44, null, 45, null, 46, null, 47, null, 48, null, 49, null, 50, null, 51]], out: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51] }
  - { in: [[11, -20, -14, 84, null, 64, -68, 41, 75, 91, -22, 19, -42, null, null, -68, 73, null, 75, -79, 80, -85, null, -78, 61, 3, -75, -28, null, 48, 14, 16, null, -97, -3, null, null, -32, 99, -98, -88, 71, null, null, null, 85, null, null, null, -67, -87, -34, 79, 72, -8, 46, -25, -49, 30, null, null, -80, null, -5, -67, null, 40, 68, null, null, null, null, null, null, -25, -25, null, 82, -42, null, 83, 25, null, null, 58, -51, null, -49, 65, -56, null, null, null, 83, 57, null, 94, null, 42, 95, -49, null, null, 88, -69, 28, 25, -26, 71, null, 5, null, 63, 12, null, null, null, -70, null, null, null, 55, null, null, -64, null, null, null, null, null, null, 30, null, -67, -77, null, 37, 19, null, null, null, null, null, null, -27, null, -16, null, null, null, null, null, null, -57, -92, 75, null, null, null, null, -57, null, null, -10, null, null, -60, 66, null, null, null, null, -45, null, null, null, null, -33, -17, -80, -75, null, null, null, null, null, -46, null, null, 22, null, null, null, null, null, 81]], out: [11, -14, -68, -42, 61, -88, -67, 57, -64, 75, -45, -46, 81] }
---
想象你站在一棵二叉树的**右侧**，从上到下，返回你能看到的节点值：也就是每一层最右边的那个节点。

**约束**

- 节点数在 `[0, 100]` 范围内
- `-100 <= Node.val <= 100`

**提示**：层序遍历，取每层的最后一个节点。也可以 DFS 先访问右子树，每一层第一个访问到的节点就是看得到的那个。
