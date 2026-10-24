---
id: 7
title: 二叉树的最大深度
difficulty: easy
tags: [树, 二叉树, 递归]
related: [ds-binary-tree]
leetcode: https://leetcode.cn/problems/maximum-depth-of-binary-tree/
function: maxDepth
params: { root: TreeNode }
returns: int
examples:
  - in: [[3, 9, 20, null, null, 15, 7]]
    out: 3
    explain: 最长的路径是 3 → 20 → 15（或 3 → 20 → 7），一共 3 个节点。
  - in: [[1, null, 2]]
    out: 2
tests:
  - { in: [[]], out: 0 }
  - { in: [[0]], out: 1 }
  - { in: [[1, 2, 3, 4, 5, 6, 7]], out: 3 }
  - { in: [[1, 2, null, 3, null, 4, null, 5]], out: 5 }
  - { in: [[1, null, 2, null, 3, null, 4]], out: 4 }
  - { in: [[1, 2, 3, null, 4, null, null, 5, 6, null, 7]], out: 5 }
---
给定二叉树的根节点 `root`，返回它的**最大深度**：从根节点到最远叶子节点的最长路径上的**节点个数**。

测试用例里的二叉树按**层序**写成数组，空节点写 `null`，例如 `[3,9,20,null,null,15,7]` 表示：

```
    3
   / \
  9  20
    /  \
   15   7
```

**约束**

- 节点数在 `[0, 10^4]` 范围内
- `-100 <= Node.val <= 100`

**提示**：一棵树的深度 = 1 + 左右子树深度的较大值。也可以用层序遍历数一共有几层。
