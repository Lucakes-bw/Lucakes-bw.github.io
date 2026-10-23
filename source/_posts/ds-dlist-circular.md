---
title: 双向链表与循环链表
date: 2026-10-23 20:00:00
updated: 2026-10-23 20:00:00
categories:
  - 数据结构
  - 线性表
tags:
  - 数据结构
  - 链表
order: 4
description: 单链表只能往后走，删除一个节点还得先找到它的前驱。双向链表给每个节点加上 prev 指针，实测在已知节点地址时删除 10 万个节点只用了 0.94 毫秒，而单链表用了 1.7 秒。本文实现双向链表的插入和删除，并用循环链表解决约瑟夫问题，都配有逐步演示。
top_group_list: true
disableNunjucks: true
viz: [ds-dlist]
---

> **本文目标**：理解单链表的局限；会实现双向链表的插入和删除，理解指针修改的顺序；理解循环链表，会用它解决约瑟夫问题；了解带哨兵的双向循环链表。
>
> **前置知识**：《单链表》。

## 1. 单链表的局限

单链表的节点只知道它的后继，于是：

- **只能从前往后遍历**；
- **删除一个节点必须先找到它的前驱**。即使手上已经有了这个节点的指针，也要从头查找一遍，O(n)。

第二点在实际中很常见。比如一个缓存系统用链表记录数据的使用顺序，某条数据被访问时，要把它的节点从链表中间摘下来，移到最前面。此时我们通过哈希表已经拿到了这个节点的指针，如果还要花 O(n) 去找前驱，就太慢了。

## 2. 双向链表

给每个节点再加一个指向**前驱**的指针 `prev`：

```c
typedef struct DNode {
    int value;
    struct DNode* prev;
    struct DNode* next;
} DNode;

typedef struct {
    DNode* head;                                // 头结点
    size_t size;
} DList;

int dlistInit(DList* list)
{
    list->head = malloc(sizeof(DNode));
    if (list->head == NULL) return 0;
    list->head->prev = list->head->next = NULL;
    list->size = 0;
    return 1;
}
```

代价是每个节点多占一个指针的空间（64 位系统上 8 字节），每次插入删除要多维护一个指针。

### 2.1 插入

在已知节点 `p` 之后插入一个新节点，需要修改 4 个指针：

```c
DNode* insertAfter(DList* list, DNode* p, int value)   // 在节点 p 之后插入
{
    DNode* node = malloc(sizeof(DNode));
    if (node == NULL) return NULL;
    node->value = value;
    node->prev = p;                             // ① 新节点的 prev 指向 p
    node->next = p->next;                       // ② 新节点的 next 指向 p 的后继
    if (p->next != NULL) {
        p->next->prev = node;                   // ③ 后继的 prev 指向新节点
    }
    p->next = node;                             // ④ p 的 next 指向新节点
    list->size++;
    return node;
}
```

演示中，每个节点上方的箭头是 `next`，下方的箭头是 `prev`：

<div class="dsviz" data-viz="dl-insert"></div>

顺序的原则和单链表一样：**先设置新节点自己的指针（①②），再修改链表中原有的指针（③④）**。其中 ③ 必须在 ④ 之前，因为 ③ 要通过 `p->next` 找到原来的后继，执行 ④ 之后 `p->next` 就变成了新节点。另外，如果 `p` 是最后一个节点，没有后继，就要跳过 ③，否则会解引用 `NULL`。

### 2.2 删除

已知要删除的节点 `node`，它的前驱就是 `node->prev`，不需要查找：

```c
void removeNode(DList* list, DNode* node)       // 删除一个已知的节点：O(1)
{
    node->prev->next = node->next;
    if (node->next != NULL) {
        node->next->prev = node->prev;
    }
    free(node);
    list->size--;
}
```

<div class="dsviz" data-viz="dl-remove"></div>

因为有头结点，每个数据节点都有前驱，`node->prev` 永远不是 `NULL`，第一行不需要判断。

### 2.3 测试

```c
int main(void)
{
    DList list;
    if (!dlistInit(&list)) return 1;
    DNode* tail = list.head;
    for (int i = 1; i <= 4; ++i) tail = insertAfter(&list, tail, i * 10);
    printBoth(&list);
    DNode* second = list.head->next->next;
    insertAfter(&list, second, 25);
    printf("在 20 之后插入 25: "); printBoth(&list);
    removeNode(&list, second);
    printf("删除 20:          "); printBoth(&list);
    removeNode(&list, list.head->next);
    printf("删除第一个:       "); printBoth(&list);
    dlistFree(&list);
    return 0;
}
```

`printBoth` 先沿着 `next` 正向遍历，再从最后一个节点沿着 `prev` 反向遍历，检查两个方向的指针是否一致：

```
正向: 10 20 30 40  反向: 40 30 20 10
在 20 之后插入 25: 正向: 10 20 25 30 40  反向: 40 30 25 20 10
删除 20:          正向: 10 25 30 40  反向: 40 30 25 10
删除第一个:       正向: 25 30 40  反向: 40 30 25
```

`-fsanitize=address,undefined` 没有报错，`leaks` 报告 `0 leaks`。**同时检查正向和反向遍历**是测试双向链表的好方法：只要某处漏改了一个 `prev`，反向遍历的结果就会出错。

### 2.4 实测：删除已知节点

建立 n 个节点的单链表和双向链表，记下每个节点的地址，然后按随机顺序删除所有节点（`-O2`）：

```c
for (int k = 0; k < n; ++k) {           // 单链表：要从头找到前驱
    SNode* target = sp[order[k]];
    SNode* prev = shead;
    while (prev->next != target) prev = prev->next;
    prev->next = target->next;
    free(target);
}

for (int k = 0; k < n; ++k) {           // 双向链表：前驱就在 prev 中
    DNode* target = dp[order[k]];
    target->prev->next = target->next;
    if (target->next) target->next->prev = target->prev;
    free(target);
}
```

```
n = 10000   单链表      40.3 ms，双向链表   0.14 ms
n = 100000  单链表    1740.1 ms，双向链表   0.94 ms
```

单链表每次删除 O(n)，n 次就是 O(n²)，10 万个节点用了 1.7 秒；双向链表每次 O(1)，只用了不到 1 毫秒，**快了将近 2000 倍**。C++ 的 `std::list` 就是双向链表。

## 3. 循环链表

把单链表最后一个节点的 `next` 指回第一个节点，就成了**循环链表**。从任何一个节点出发，都能走遍整个链表。判断是否走完了一圈，不能再用 `p != NULL`，而要判断是否回到了出发的节点。

循环链表适合处理「轮流」的问题，比如操作系统轮流给每个进程分配时间片、多人游戏中轮流出牌。

### 3.1 约瑟夫问题

n 个人围成一圈，编号 1 到 n。从 1 号开始报数，报到 k 的人出列，下一个人重新从 1 开始报数，直到只剩下一个人。求出列的顺序和最后剩下的人。

用循环链表模拟这个过程非常自然：

```c
int josephus(int n, int k)
{
    Node* tail = NULL;
    Node* first = NULL;
    for (int i = 1; i <= n; ++i) {              // 建立循环链表：1 → 2 → … → n → 1
        Node* node = malloc(sizeof(Node));
        if (node == NULL) exit(1);
        node->value = i;
        if (first == NULL) first = node;
        else tail->next = node;
        tail = node;
    }
    tail->next = first;                         // 首尾相连

    Node* prev = tail;                          // prev 是下一个报数的人的前一个
    printf("出列顺序:");
    while (prev->next != prev) {                // 不止一个人
        for (int i = 1; i < k; ++i) {
            prev = prev->next;                  // 报数 1 到 k-1 的人
        }
        Node* out = prev->next;                 // 报数 k 的人
        printf(" %d", out->value);
        prev->next = out->next;
        free(out);
    }
    int last = prev->value;
    free(prev);
    printf("\n");
    return last;
}
```

为什么用 `prev` 而不是直接指向报数的人？因为要删除报到 k 的人，需要它的**前驱**。所以 `prev` 从 `tail`（第一个人的前一个）开始，每次停在报数 k 的人的前一个位置。只剩一个人时，它的 `next` 指向自己，循环结束。

演示中可以修改 n 和 k。每轮报数时，节点下方显示报的数，出列的人变暗并标出顺序：

<div class="dsviz" data-viz="josephus"></div>

```c
printf("n = 7, k = 3，最后剩下 %d\n", josephus(7, 3));
printf("n = 10, k = 2，最后剩下 %d\n", josephus(10, 2));
```

```
出列顺序: 3 6 2 7 5 1
n = 7, k = 3，最后剩下 4
出列顺序: 2 4 6 8 10 3 7 1 9
n = 10, k = 2，最后剩下 5
```

每出列一个人要走 k 步，一共 n - 1 轮，时间复杂度 O(nk)。

## 4. 带哨兵的双向循环链表

把双向链表也首尾相连，并且让头结点同时充当首尾的连接点：头结点的 `next` 是第一个元素，`prev` 是最后一个元素；最后一个元素的 `next` 指回头结点。这时的头结点叫作**哨兵**（sentinel）。

这种结构的好处是：**链表中没有任何 `NULL` 指针**，空链表时哨兵的 `prev` 和 `next` 都指向它自己。于是插入和删除中所有关于「有没有后继」的判断都可以去掉：

```c
// 带哨兵的双向循环链表：不需要判断 NULL
node->prev = p;
node->next = p->next;
p->next->prev = node;
p->next = node;
```

而且访问最后一个元素（`sentinel->prev`）也是 O(1) 的。Linux 内核中的通用链表 `list_head` 和 C++ 标准库的 `std::list` 都是这样实现的。

## 5. 常见错误

1. **双向链表只修改了 `next`，忘了 `prev`**：正向遍历正常，反向遍历出错。
2. **插入时 ③④ 顺序颠倒**：`p->next->prev = node` 修改的是新节点自己。
3. **在最后一个节点之后插入、删除最后一个节点时，没有判断后继是否为 `NULL`**：程序崩溃。
4. **在循环链表上用 `p != NULL` 作为循环条件**：死循环。
5. **删除循环链表的节点时找不到前驱**：所以约瑟夫问题中要让 `prev` 停在出列者的前一个。
6. **释放循环链表时没有先断开环**：重复释放。

## 6. 小结

- 单链表删除已知节点也要 O(n) 查找前驱；双向链表的每个节点有 `prev` 和 `next`，删除已知节点 O(1)，实测快了约 2000 倍
- 双向链表插入要修改 4 个指针：先设置新节点的指针，再修改原有的指针
- 测试双向链表时同时检查正向和反向遍历
- 循环链表首尾相连，适合「轮流」的场景，约瑟夫问题是典型的例子
- 带哨兵的双向循环链表中没有 `NULL`，插入删除不需要特殊判断

## 7. 练习

1. 利用 `insertAfter` 实现 `insertBefore(DList* list, DNode* p, int value)`，在节点 `p` 之前插入。
2. 约瑟夫问题有一个不用模拟的递推公式：设 f(n) 是 n 个人时最后剩下的人的编号（从 0 开始编号），则 f(1) = 0，f(n) = (f(n-1) + k) % n。用它验证 n = 7、k = 3 时剩下的是 4 号（从 1 开始编号）。
3. 在带哨兵的双向循环链表中，如何判断链表为空？如何遍历所有元素？

<details>
<summary>点击查看答案</summary>

1. 在 `p` 之前插入，就是在 `p` 的前驱之后插入：

   ```c
   DNode* insertBefore(DList* list, DNode* p, int value)
   {
       return insertAfter(list, p->prev, value);
   }
   ```

   因为有头结点，即使 `p` 是第一个元素，`p->prev` 也是头结点，不是 `NULL`。这也是双向链表的便利之处：单链表无法 O(1) 地在一个节点之前插入。

2. 按公式逐个计算：f(1) = 0，f(2) = (0 + 3) % 2 = 1，f(3) = (1 + 3) % 3 = 1，f(4) = (1 + 3) % 4 = 0，f(5) = (0 + 3) % 5 = 3，f(6) = (3 + 3) % 6 = 0，f(7) = (0 + 3) % 7 = 3。从 0 开始编号的 3 就是从 1 开始编号的 4 号，和模拟的结果一致。这个公式只需要 O(n) 的时间和 O(1) 的空间，但只能求最后剩下的人，不能得到出列的顺序。

3. 空链表时哨兵的 `next` 指向它自己：`sentinel->next == sentinel`。遍历：

   ```c
   for (DNode* p = sentinel->next; p != sentinel; p = p->next) { ... }
   ```

   终止条件是回到了哨兵，而不是遇到 `NULL`。

</details>
