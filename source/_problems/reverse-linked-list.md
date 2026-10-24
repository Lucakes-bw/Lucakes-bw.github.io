---
id: 3
title: 反转链表
difficulty: easy
tags: [链表]
related: [linked-list]
leetcode: https://leetcode.cn/problems/reverse-linked-list/
function: reverseList
params: { head: ListNode }
returns: ListNode
examples:
  - in: [[1, 2, 3, 4, 5]]
    out: [5, 4, 3, 2, 1]
  - in: [[1, 2]]
    out: [2, 1]
  - in: [[]]
    out: []
    explain: 空链表反转后还是空链表。
tests:
  - { in: [[7]], out: [7] }
  - { in: [[1, 1, 2, 2]], out: [2, 2, 1, 1] }
  - { in: [[-5, 0, 5, 10, 15, 20]], out: [20, 15, 10, 5, 0, -5] }
  - { in: [[3, 1, 4, 1, 5, 9, 2, 6]], out: [6, 2, 9, 5, 1, 4, 1, 3] }
---
给你单链表的头节点 `head`，把整个链表**反转**，返回反转后的头节点。

测试用例里的链表写成数组，例如 `[1,2,3]` 表示 `1 → 2 → 3`。

**约束**

- 节点数在 `[0, 5000]` 范围内
- `-5000 <= Node.val <= 5000`

**进阶**：分别用迭代和递归各写一遍。迭代版只需要 O(1) 的额外空间。
