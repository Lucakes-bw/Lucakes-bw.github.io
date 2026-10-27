---
id: 46
title: 二叉树展开为链表
difficulty: medium
tags: [树, 二叉树, 链表, DFS]
related: [ds-binary-tree]
leetcode: https://leetcode.cn/problems/flatten-binary-tree-to-linked-list/
function: flatten
params: { root: TreeNode }
returns: void
output: root
examples:
  - in: [[1, 2, 5, 3, 4, null, 6]]
    out: [1, null, 2, null, 3, null, 4, null, 5, null, 6]
    explain: "先序遍历顺序是 1 2 3 4 5 6，展开后每个节点只有右孩子；用层序数组表示就是 [1, null, 2, null, 3, …]。"
  - in: [[]]
    out: []
  - in: [[0]]
    out: [0]
tests:
  - { in: [[1, 2]], out: [1, null, 2] }
  - { in: [[1, null, 2]], out: [1, null, 2] }
  - { in: [[1, 2, 3]], out: [1, null, 2, null, 3] }
  - { in: [[1, 2, null, 3]], out: [1, null, 2, null, 3] }
  - { in: [[3, 1, 4, null, 2]], out: [3, null, 1, null, 2, null, 4] }
  - { in: [[-98, 71, 19, 69, -34, 48, -64, 38, null, -84, null, -40, -38, null, -36, null, null, -86, 41, null, null, 83, -42, 85, null, -73, 86, -79, null, -82, 78, 73, -7, 87, null, 65, -12, -2, -40, -12, -44, 5, 7, 3, -46, null, -69, null, 16, null, null, -25, 66, 24, 12, -85, -80, 31, 85, 0, null, null, 50, null, -79, null, null, null, null, -22, -80, -13, null, null, null, null, -80, 97, 19, -38, 2, null, 27, 25, -34, null, null, null, null, -31, -56, null, -78, null, null, null, 96, -84, -77, null, 74, 41, -12, null, null, 0, 7, 27, null, -43, -44, 72, 15, null, null, -25, null, null, null, null, 33, 68, 45, null, null, null, -22, 88, null, null, null, null, null, null, null, null, null, null, 91, -51, -30, null, 28, null, null, 79, null, null, null, -5, null, 70, null, 73, null, null, null, null, 61, null, null, null, null, null, 91, null, null, -10, -77, null, null, null, null, null, 82, null, -65, null, null, -14, null, null, null, null, null, null, 9, null, null, null, null, null, null, null, 68, 60, -8]], out: [-98, null, 71, null, 69, null, 38, null, -34, null, -84, null, -86, null, -73, null, 65, null, -25, null, -80, null, 66, null, 97, null, 0, null, 91, null, 91, null, 7, null, -51, null, -30, null, -10, null, -77, null, 9, null, 68, null, 60, null, -8, null, 19, null, 27, null, 28, null, -12, null, 24, null, -38, null, -43, null, -44, null, 79, null, 2, null, 72, null, 15, null, -5, null, 82, null, 12, null, 27, null, 86, null, -2, null, -85, null, 25, null, -25, null, 70, null, -65, null, -34, null, -80, null, -40, null, 31, null, 85, null, -31, null, 33, null, 73, null, -56, null, 68, null, 45, null, 61, null, -14, null, 41, null, -79, null, -12, null, 0, null, -78, null, -44, null, 50, null, 19, null, 48, null, -40, null, -38, null, 83, null, -82, null, 5, null, -79, null, 96, null, -22, null, 7, null, 78, null, 3, null, -46, null, -22, null, -84, null, 88, null, -77, null, -80, null, 74, null, -42, null, 73, null, -69, null, -13, null, 41, null, -12, null, -7, null, 16, null, -64, null, -36, null, 85, null, 87] }
---
给定二叉树的根节点 `root`，把它**原地**展开成一个「链表」：

- 展开后仍然使用 `TreeNode`，`right` 指向链表中的下一个节点，`left` 一律为 `null`
- 节点顺序和二叉树的**先序遍历**相同

函数不需要返回值，判题时检查调用之后的 `root`（按层序数组输出）。

**约束**

- 节点数在 `[0, 2000]` 范围内
- `-100 <= Node.val <= 100`

**进阶**：你能用 O(1) 的额外空间原地展开吗？

**提示**：对每个有左子树的节点：找到左子树最右边的节点，把当前节点的右子树接到它后面，再把左子树整体挪到右边。
