---
title: 单链表
date: 2026-10-17 10:00:00
updated: 2026-10-23 19:40:00
categories:
  - 数据结构
  - 线性表
tags:
  - 数据结构
  - 链表
order: 3
description: 链表用指针把分散在内存各处的节点串起来，插入和删除只需要修改指针。本文用 C 语言实现带头结点的单链表，并用逐步演示展示插入、删除、反转三个操作中每一个指针的变化，最后介绍快慢指针找中间节点和判断链表是否有环。
top_group_list: true
disableNunjucks: true
viz: [ds-linked-list]
---

> **本文目标**：理解链表的存储方式和它与顺序表的区别；理解头结点的作用；会实现单链表的插入、删除、查找、遍历和释放；掌握反转链表；会用快慢指针找中间节点、判断环。
>
> **前置知识**：《顺序表》，C 语言的《指针入门》、《动态内存》、《结构体、联合与枚举》。

## 1. 链表的结构

顺序表的元素必须连续存放，所以在中间插入要移动大量元素，扩容还要整体复制。**链表**换了一种思路：每个元素单独申请一块内存，叫作**节点**，节点中除了数据，还保存着**下一个节点的地址**：

```c
typedef struct Node {
    int value;
    struct Node* next;
} Node;

typedef struct {
    Node* head;                                 // 头结点：不存数据，head->next 才是第一个元素
    size_t size;
} List;
```

节点在内存中的位置是分散的，靠 `next` 指针串成一条链，最后一个节点的 `next` 是 `NULL`。下面的演示中，每个节点右侧的小格子就是 `next` 指针域，画有斜线表示它是 `NULL`。

### 1.1 头结点

`List` 中的 `head` 指向一个**不存放数据的节点**，叫作**头结点**，真正的第一个元素是 `head->next`。它的作用是**让所有位置的插入和删除都变得一样**：

- 没有头结点时，在第一个位置插入或删除第一个元素，要修改的是「指向第一个节点的那个指针变量」本身，和在其他位置的处理不同，需要特殊判断（或者用二级指针）。
- 有了头结点，**每个元素都有前驱**，第一个元素的前驱就是头结点，所有位置都用同一套代码处理。

```c
int listInit(List* list)
{
    list->head = malloc(sizeof(Node));
    if (list->head == NULL) return 0;
    list->head->next = NULL;
    list->size = 0;
    return 1;
}
```

## 2. 插入

在第 pos 个位置（从 0 开始）插入，关键是找到**第 pos 个位置的前一个节点** `prev`，然后修改两个指针：

```c
int insertAt(List* list, size_t pos, int value)
{
    if (pos > list->size) return 0;             // 位置超出范围
    Node* prev = list->head;
    for (size_t i = 0; i < pos; ++i) {
        prev = prev->next;                      // 走到第 pos 个位置的前一个节点
    }
    Node* node = malloc(sizeof(Node));
    if (node == NULL) return 0;
    node->value = value;
    node->next = prev->next;                    // ① 新节点指向原来的后继
    prev->next = node;                          // ② 前驱指向新节点
    list->size++;
    return 1;
}
```

点击「插入」，观察 `prev` 怎样一步步走到插入位置，以及 ①、② 两步分别改变了哪个指针。可以修改 pos 和 value 多试几次，比如 pos = 0（插在最前面）和 pos = 4（插在最后面）：

<div class="dsviz" data-viz="ll-insert"></div>

**① 和 ② 的顺序不能颠倒**。如果先执行 `prev->next = node`，`prev` 原来的后继的地址就丢失了，`node->next = prev->next` 得到的是 `node` 自己，链表的后半部分再也找不到了（而且造成了内存泄漏）。

插入本身只修改两个指针，是 O(1) 的，**不需要移动任何元素**。但找到 `prev` 需要从头走 pos 步，所以按位置插入整体是 O(n)。如果手上已经有了前驱节点的指针（比如在遍历的过程中插入），插入就是 O(1)，这是链表相比顺序表的真正优势。

## 3. 删除

删除一个节点，同样需要它的**前驱**，让前驱直接跳过它：

```c
int removeValue(List* list, int value)
{
    Node* prev = list->head;
    while (prev->next != NULL && prev->next->value != value) {
        prev = prev->next;
    }
    if (prev->next == NULL) return 0;           // 没有找到
    Node* target = prev->next;
    prev->next = target->next;                  // 让前驱跳过 target
    free(target);
    list->size--;
    return 1;
}
```

注意循环中比较的是 `prev->next->value`，而不是 `prev->value`：我们要让 `prev` 停在目标的**前一个**节点上。`while` 条件中先检查 `prev->next != NULL`，利用短路求值保证不会访问空指针。

<div class="dsviz" data-viz="ll-remove"></div>

顺序同样重要：**先修改 `prev->next`，再 `free(target)`**。如果先释放，再读取 `target->next`，就是释放后使用（《动态内存》）。

## 4. 遍历、查找和释放

```c
void print(const List* list)
{
    printf("[%zu]", list->size);
    for (Node* p = list->head->next; p != NULL; p = p->next) {
        printf(" %d", p->value);
    }
    printf("\n");
}

void listFree(List* list)
{
    Node* p = list->head;
    while (p != NULL) {
        Node* next = p->next;                   // 先保存后继，再释放当前节点
        free(p);
        p = next;
    }
    list->head = NULL;
    list->size = 0;
}
```

`for (Node* p = first; p != NULL; p = p->next)` 是遍历链表的标准写法。链表**不能随机访问**：要访问第 i 个元素，只能从头开始走 i 步，所以按下标访问和按值查找都是 O(n)。

## 5. 反转链表

反转链表是最经典的链表题目。思路是：从前往后遍历，把每个节点的 `next` 改为指向它的前一个节点。

```c
void reverse(List* list)
{
    Node* prev = NULL;
    Node* cur = list->head->next;
    while (cur != NULL) {
        Node* next = cur->next;                 // 先记住后继
        cur->next = prev;                       // 反转指针
        prev = cur;                             // prev 和 cur 各前进一步
        cur = next;
    }
    list->head->next = prev;
}
```

三个指针 `prev`、`cur`、`next` 的配合是关键。在演示中，反转后的指针画在节点的下方：

<div class="dsviz" data-viz="ll-reverse"></div>

每一轮循环做四件事：用 `next` 记住后继（因为下一步就要改掉 `cur->next`）；把 `cur->next` 指向 `prev`；`prev` 和 `cur` 各前进一步。每个节点只处理一次，时间 O(n)，只用了三个指针，额外空间 O(1)。

## 6. 快慢指针

### 6.1 找中间节点

不知道链表长度的情况下，怎样只遍历一次就找到中间的节点？用两个指针：**慢指针每次走一步，快指针每次走两步**。快指针走到末尾时，慢指针恰好走了一半。

```c
Node* middle(const List* list)
{
    Node* slow = list->head->next;
    Node* fast = list->head->next;
    while (fast != NULL && fast->next != NULL) {
        slow = slow->next;                      // 慢指针走一步
        fast = fast->next->next;                // 快指针走两步
    }
    return slow;
}
```

<div class="dsviz" data-viz="ll-middle"></div>

元素个数是偶数时，这个写法返回的是中间两个中的**后一个**，比如 6 个元素时返回第 4 个。

### 6.2 判断链表是否有环

如果某个节点的 `next` 错误地指向了前面的节点，链表就形成了一个**环**，遍历它会陷入死循环。判断方法同样是快慢指针：**如果有环，快指针进入环后会一圈圈地追赶慢指针，每走一轮两者的距离缩小 1，最终一定会相遇**；如果没有环，快指针会先到达 `NULL`。

```c
int hasCycle(const List* list)
{
    Node* slow = list->head->next;
    Node* fast = list->head->next;
    while (fast != NULL && fast->next != NULL) {
        slow = slow->next;
        fast = fast->next->next;
        if (slow == fast) return 1;             // 快指针追上了慢指针：有环
    }
    return 0;
}
```

演示中最后一个节点指回了第 3 个节点：

<div class="dsviz" data-viz="ll-cycle"></div>

这个方法只需要 O(1) 的额外空间，叫作 Floyd 判圈算法。

## 7. 完整测试

把上面的函数放在一起测试：

```c
int main(void)
{
    List list;
    if (!listInit(&list)) return 1;
    for (int i = 0; i < 5; ++i) insertAt(&list, list.size, (i + 1) * 10);
    print(&list);
    insertAt(&list, 0, 5);
    insertAt(&list, 3, 25);
    printf("插入 5 和 25 之后: "); print(&list);
    printf("insertAt(100, ...) 返回 %d\n", insertAt(&list, 100, 1));
    removeValue(&list, 30);
    printf("删除 30 之后:     "); print(&list);
    printf("removeValue(99) 返回 %d\n", removeValue(&list, 99));
    Node* mid = middle(&list);
    printf("中间节点: %d\n", mid->value);
    reverse(&list);
    printf("反转之后:         "); print(&list);
    printf("有环吗？%s\n", hasCycle(&list) ? "有" : "没有");

    Node* last = list.head->next;               // 人为制造一个环：最后一个节点指回第二个
    while (last->next != NULL) last = last->next;
    last->next = list.head->next->next;
    printf("制造环之后有环吗？%s\n", hasCycle(&list) ? "有" : "没有");
    last->next = NULL;                          // 拆掉环，才能正确释放

    listFree(&list);
    return 0;
}
```

```
[5] 10 20 30 40 50
插入 5 和 25 之后: [7] 5 10 20 25 30 40 50
insertAt(100, ...) 返回 0
删除 30 之后:     [6] 5 10 20 25 40 50
removeValue(99) 返回 0
中间节点: 25
反转之后:         [6] 50 40 25 20 10 5
有环吗？没有
制造环之后有环吗？有
```

用 `-fsanitize=address,undefined` 编译运行没有报错，`leaks` 报告 `0 leaks for 0 total leaked bytes.`。注意释放之前要先拆掉环，否则 `listFree` 会在环中反复释放同一批节点。

## 8. 链表和顺序表的比较

| | 顺序表 | 单链表 |
|---|---|---|
| 按下标访问 | O(1) | O(n) |
| 已知位置后的插入、删除 | O(n)，要移动元素 | O(1)，只改指针 |
| 按位置插入、删除（含查找） | O(n) | O(n) |
| 在末尾添加 | 均摊 O(1) | O(n)，除非另外保存尾指针 |
| 额外空间 | 少量未使用的容量 | 每个节点一个指针（64 位系统上 8 字节） |
| 缓存友好 | 是 | 否，节点分散在内存各处 |

链表的优势在于「已经找到位置之后」的插入和删除，以及不需要连续的大块内存、永远不需要扩容。但它的遍历慢（《STL 容器》中实测慢了 10 倍以上），每个节点还要额外的指针空间。实际中，链表更多地用作其他数据结构的组成部分：哈希表的冲突链、操作系统的进程队列、LRU 缓存等。

## 9. 常见错误

1. **插入时先修改 `prev->next`**：后继丢失。
2. **删除时先 `free` 再读 `next`**：释放后使用。
3. **遍历时不检查 `NULL`**：访问空指针，程序崩溃。
4. **找前驱时让 `prev` 停在了目标节点上**：单链表无法回到前一个节点，无法完成删除。
5. **释放链表时直接 `free(p); p = p->next;`**：释放后使用。
6. **没有头结点时忘了处理第一个位置的特殊情况**。
7. **有环的链表上调用遍历或释放函数**：死循环。

## 10. 小结

- 链表的节点分散存放，通过 `next` 指针相连，最后一个节点的 `next` 为 `NULL`
- 头结点让每个元素都有前驱，所有位置的插入和删除都可以统一处理
- 插入：先 `node->next = prev->next`，再 `prev->next = node`；删除：先 `prev->next = target->next`，再 `free(target)`
- 修改指针是 O(1)，但找到位置是 O(n)；链表不能随机访问
- 反转链表用 `prev`、`cur`、`next` 三个指针，O(n) 时间、O(1) 空间
- 快慢指针：快指针每次两步、慢指针每次一步，可以找中间节点、判断环

## 11. 练习

1. 只遍历一次，找到链表的**倒数第 k 个**节点。提示：让快指针先走 k 步。
2. 把两个**有序**的链表合并成一个有序链表，不能申请新的节点，只能修改指针。
3. 在不带头结点的链表上实现「删除第一个元素」，函数的参数应该是什么类型？为什么？

<details>
<summary>点击查看答案</summary>

1. 快指针先走 k 步，然后两个指针一起走，快指针到达 `NULL` 时，慢指针就在倒数第 k 个：

   ```c
   Node* kthFromEnd(Node* first, size_t k)            // 倒数第 k 个（k 从 1 开始）
   {
       Node* fast = first;
       for (size_t i = 0; i < k; ++i) {
           if (fast == NULL) return NULL;             // 链表不足 k 个
           fast = fast->next;
       }
       Node* slow = first;
       while (fast != NULL) {
           slow = slow->next;
           fast = fast->next;
       }
       return slow;
   }
   ```

   对 `1 4 7 9`，倒数第 2 个是 7，倒数第 5 个返回 `NULL`。

2. 用一个临时的头结点 `dummy` 和一个尾指针 `tail`，每次把两个链表中较小的那个节点接到 `tail` 后面：

   ```c
   Node* mergeSorted(Node* a, Node* b)                // 合并两个有序链表（不带头结点）
   {
       Node dummy = {0, NULL};                        // 临时的头结点
       Node* tail = &dummy;
       while (a != NULL && b != NULL) {
           if (a->value <= b->value) { tail->next = a; a = a->next; }
           else { tail->next = b; b = b->next; }
           tail = tail->next;
       }
       tail->next = (a != NULL) ? a : b;              // 剩下的直接接上
       return dummy.next;
   }
   ```

   `1 4 7 9` 和 `2 3 8` 合并为 `1 2 3 4 7 8 9`。这里的 `dummy` 就是头结点思想的应用：它让「第一个节点」不需要特殊处理。`dummy` 是局部变量，不需要释放。

3. 参数要是 `Node**`（指向「指向第一个节点的指针」的指针）。删除第一个元素需要修改调用者的那个指针变量，让它指向第二个节点；如果只传 `Node*`，修改的只是函数中的副本（《指针进阶》中的二级指针）。有头结点时就没有这个问题，因为要修改的是头结点的 `next` 成员。

</details>
