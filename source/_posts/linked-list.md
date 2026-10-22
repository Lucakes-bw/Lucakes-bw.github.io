---
title: 单链表的原理与实现
date: 2026-10-17 10:00:00
updated: 2026-10-17 10:00:00
categories:
  - 数据结构
  - 线性表
tags:
  - 数据结构
  - 链表
order: 1
description: 单链表的结构、头插法、遍历与删除，用 C 语言完整实现。
top_group_list: true
---

## 1. 链表与数组

| 对比项 | 数组 | 链表 |
| --- | --- | --- |
| 内存 | 连续 | 不连续，节点通过指针相连 |
| 随机访问 | O(1) | O(n) |
| 插入/删除 | O(n)，需要移动元素 | O(1)，已知位置时只改指针 |
| 大小 | 固定 | 动态增长 |

## 2. 节点定义

每个节点包含**数据域**和指向下一个节点的**指针域**：

```c
typedef struct Node {
    int data;
    struct Node *next;
} Node;
```

## 3. 基本操作

```c
#include <stdio.h>
#include <stdlib.h>

// 头插法：新节点插入到链表头部
Node *push_front(Node *head, int value)
{
    Node *node = malloc(sizeof(Node));
    node->data = value;
    node->next = head;
    return node;          // 新节点成为新的头
}

// 遍历
void print_list(const Node *head)
{
    for (const Node *p = head; p != NULL; p = p->next)
        printf("%d -> ", p->data);
    printf("NULL\n");
}

// 删除第一个值为 value 的节点
Node *remove_value(Node *head, int value)
{
    Node dummy = { 0, head };   // 哨兵节点，统一处理删除头节点的情况
    for (Node *prev = &dummy; prev->next; prev = prev->next) {
        if (prev->next->data == value) {
            Node *del = prev->next;
            prev->next = del->next;
            free(del);
            break;
        }
    }
    return dummy.next;
}

int main(void)
{
    Node *head = NULL;
    for (int i = 1; i <= 4; i++)
        head = push_front(head, i);
    print_list(head);          // 4 -> 3 -> 2 -> 1 -> NULL

    head = remove_value(head, 3);
    print_list(head);          // 4 -> 2 -> 1 -> NULL
    return 0;
}
```

## 4. 小结

- 链表用指针把分散的节点串起来，插入删除快，随机访问慢
- 使用**哨兵节点**可以避免对头节点做特殊判断
- 每次 `malloc` 的节点都要记得 `free`
