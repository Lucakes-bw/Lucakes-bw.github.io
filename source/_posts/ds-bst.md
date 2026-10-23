---
title: 二叉搜索树
date: 2026-10-23 21:40:00
updated: 2026-10-23 21:40:00
categories:
  - 数据结构
  - 树
tags:
  - 数据结构
  - 二叉树
  - 查找
order: 2
description: 二叉搜索树把二分查找的思想放进了树里，查找、插入、删除都只需要沿着一条路径走。本文实现查找、插入和删除的三种情况，配有逐步演示；并实测插入有序数据时树退化成链表：10 万个节点高度变成 100000，建树和查找都慢了 500 多倍。
top_group_list: true
disableNunjucks: true
viz: [ds-bst]
---

> **本文目标**：理解二叉搜索树的性质；会实现查找、插入和删除，特别是删除有两个孩子的节点；理解操作的复杂度取决于树的高度；认识插入顺序导致的退化问题。
>
> **前置知识**：《二叉树与遍历》、《复杂度分析》（二分查找）。

## 1. 二叉搜索树的性质

**二叉搜索树**（BST，Binary Search Tree）是满足下面性质的二叉树：

- 对任意一个节点，**左子树中所有节点的值都比它小**；
- **右子树中所有节点的值都比它大**；
- 左右子树也都是二叉搜索树。

由此可以得到一个重要的结论：**二叉搜索树的中序遍历是一个递增的序列**。

```c
typedef struct Node {
    int key;
    struct Node* left;
    struct Node* right;
} Node;
```

《复杂度分析》中的二分查找要求数据存放在有序的数组中，每次和中间的元素比较，排除一半。但有序数组插入和删除都要移动元素，是 O(n) 的。二叉搜索树把「中间的元素」变成了根节点，「左半边」和「右半边」变成了左右子树，**既能像二分查找那样快速查找，又能像链表那样只修改指针就完成插入和删除**。

## 2. 查找

从根开始，和当前节点比较：相等就找到了；比它小就去左子树，比它大就去右子树；走到 `NULL` 说明不存在。

```c
Node* search(Node* root, int key)
{
    Node* p = root;
    while (p != NULL && p->key != key) {
        p = key < p->key ? p->left : p->right;  // 小的往左，大的往右
    }
    return p;
}
```

## 3. 插入

插入就是先查找：一路向下走，直到走到 `NULL`，那个空位置就是新节点应该在的地方。**新节点总是作为叶子插入**，已有的节点都不需要移动。

```c
int insert(Node** root, int key)
{
    Node** link = root;                         // 指向「将要挂上新节点的那个指针」
    while (*link != NULL) {
        if (key == (*link)->key) return 0;      // 已经存在
        link = key < (*link)->key ? &(*link)->left : &(*link)->right;
    }
    Node* node = malloc(sizeof(Node));
    if (node == NULL) return 0;
    node->key = key;
    node->left = node->right = NULL;
    *link = node;
    return 1;
}
```

这里用了一个技巧：`link` 是一个**二级指针**，它不指向节点，而是指向「某个节点的 `left` 或 `right` 成员」（或者指向调用者的 `root` 变量）。走到 `*link == NULL` 时，直接 `*link = node`，就把新节点挂上去了，**不需要区分它是根、左孩子还是右孩子**。这和链表中头结点的作用是一样的。

在演示中试试查找存在和不存在的值，然后插入几个新值：

<div class="dsviz" data-viz="bst-insert"></div>

## 4. 删除

删除比插入复杂，因为被删除的节点可能有孩子，要保证删除之后仍然是一棵二叉搜索树。分三种情况：

1. **叶子**：直接删除，父节点中指向它的指针改为 `NULL`。
2. **只有一个孩子**：让这个孩子顶替它的位置。
3. **有两个孩子**：不能直接删掉它，否则两棵子树没有地方挂。做法是找到它的**后继**（右子树中最小的节点，也就是比它大的数中最小的一个），**用后继的值覆盖它，然后删除后继**。后继在右子树中一路向左走到头，所以它**一定没有左孩子**，删除后继就变成了情况 1 或 2。

```c
int removeKey(Node** root, int key)
{
    Node** link = root;
    while (*link != NULL && (*link)->key != key) {
        link = key < (*link)->key ? &(*link)->left : &(*link)->right;
    }
    Node* target = *link;
    if (target == NULL) return 0;               // 没有找到
    if (target->left != NULL && target->right != NULL) {
        Node** succLink = &target->right;       // 情况 3：两个孩子，找右子树中最小的节点（后继）
        while ((*succLink)->left != NULL) {
            succLink = &(*succLink)->left;
        }
        Node* succ = *succLink;
        target->key = succ->key;                // 用后继的值替换，然后删除后继
        *succLink = succ->right;                // 后继没有左孩子，属于情况 1 或 2
        free(succ);
    } else {
        *link = target->left != NULL ? target->left : target->right;   // 情况 1、2：用唯一的孩子（或 NULL）顶替
        free(target);
    }
    return 1;
}
```

情况 1 和 2 可以合并成一行：如果两个孩子都是 `NULL`，用 `NULL` 顶替，就是情况 1。

依次删除 20（叶子）、30（一个孩子）、50（两个孩子，根）：

<div class="dsviz" data-viz="bst-remove"></div>

测试一下（`inorder` 是中序遍历）：

```c
Node* root = NULL;
int keys[] = {50, 30, 70, 20, 40, 60, 80, 35, 45};
for (int i = 0; i < 9; ++i) insert(&root, keys[i]);
printf("中序: "); inorder(root); printf("\n");
printf("search(45) %s，search(55) %s\n", search(root, 45) ? "找到" : "没有", search(root, 55) ? "找到" : "没有");
removeKey(&root, 20); printf("删除 20（叶子）:       "); inorder(root); printf("\n");
removeKey(&root, 40); printf("删除 40（两个孩子）:   "); inorder(root); printf("\n");
removeKey(&root, 30); printf("删除 30（一个孩子）:   "); inorder(root); printf("\n");
removeKey(&root, 50); printf("删除 50（根）:         "); inorder(root); printf("\n");
printf("removeKey(99) 返回 %d\n", removeKey(&root, 99));
```

```
中序: 20 30 35 40 45 50 60 70 80 
search(45) 找到，search(55) 没有
删除 20（叶子）:       30 35 40 45 50 60 70 80 
删除 40（两个孩子）:   30 35 45 50 60 70 80 
删除 30（一个孩子）:   35 45 50 60 70 80 
删除 50（根）:         35 45 60 70 80 
removeKey(99) 返回 0
```

每次删除后，中序遍历仍然是递增的，说明树仍然满足二叉搜索树的性质。

## 5. 复杂度与退化

查找、插入、删除都只沿着从根到某个节点的**一条路径**走，所以复杂度都是 **O(h)**，h 是树的高度。问题是：h 是多少？

- 如果树比较「平衡」，h 约为 log n，操作是 O(log n)；
- 如果每个节点都只有一个孩子，树就**退化成了链表**，h = n，操作变成了 O(n)。

而树的形状完全由**插入的顺序**决定。在演示中分别按有序和打乱的顺序插入 1 到 9：

<div class="dsviz" data-viz="bst-degenerate"></div>

实测：分别按随机顺序和从小到大的顺序插入 n 个数，测量树的高度，以及查找全部 n 个数的时间（`-O2`）：

```
n = 10000   随机插入：高度   33，建树     1.6 ms，查找 n 次     0.4 ms
           有序插入：高度  10000，建树    81.2 ms，查找 n 次    41.3 ms
n = 100000  随机插入：高度   40，建树     6.7 ms，查找 n 次     5.7 ms
           有序插入：高度 100000，建树  3672.0 ms，查找 n 次  3629.7 ms
```

随机插入 10 万个数，高度只有 40（可以证明，随机顺序插入时树高的期望是 O(log n)）；有序插入的高度是 100000，建树和查找都慢了 **500 多倍**。

现实中的数据经常是有序或基本有序的（按时间顺序产生的 ID、已经排好序的文件），所以普通的二叉搜索树在实际中并不可靠。下一篇的 AVL 树通过**旋转**在每次插入后自动调整，保证高度始终是 O(log n)。

> 测试代码中的 `height` 和释放函数都改用了非递归的写法。对一棵高度为 10 万的树使用递归，会直接导致栈溢出。这也再次说明了退化的危害：不只是慢，连递归遍历都做不到了。

## 6. 常见错误

1. **插入时不处理重复的值**：要么拒绝，要么在节点中计数，不能随意放到左边或右边。
2. **删除有两个孩子的节点时，直接用一个孩子顶替**：另一棵子树丢失了。
3. **找后继时从 `target` 本身开始向左走**：找到的是左子树中的节点，应该先走到右孩子再一路向左。
4. **删除时忘了更新父节点的指针**：用二级指针 `link` 可以避免分别处理「是根」「是左孩子」「是右孩子」三种情况。
5. **判断二叉搜索树时只比较节点和它的两个孩子**：要保证整个左子树都比它小，见练习 2。
6. **假设二叉搜索树的操作总是 O(log n)**：有序插入时退化为 O(n)。

## 7. 小结

- 二叉搜索树中，左子树的值都比根小，右子树的值都比根大；中序遍历是递增序列
- 查找、插入沿着一条路径向下走；新节点总是作为叶子插入
- 删除分三种情况：叶子直接删；一个孩子用孩子顶替；两个孩子用后继的值覆盖，再删除后继
- 用二级指针指向「要修改的那个指针」，可以统一处理根节点和普通节点
- 所有操作都是 O(h)；有序插入时树退化成链表，实测 10 万个节点慢了 500 多倍

## 8. 练习

1. 写一个函数，返回二叉搜索树中第 k 小的值。
2. 写一个函数，判断一棵二叉树是否是二叉搜索树。为什么只检查「左孩子 < 根 < 右孩子」是不够的？
3. 删除有两个孩子的节点时，除了用后继，还可以用什么节点来替换？

<details>
<summary>点击查看答案</summary>

1. 二叉搜索树的中序遍历是递增的，所以中序遍历中的第 k 个就是第 k 小的值。用一个计数器，访问到第 k 个时返回：

   ```c
   const Node* kthSmallest(const Node* root, int* k)   // 中序遍历中的第 k 个
   {
       if (root == NULL) return NULL;
       const Node* r = kthSmallest(root->left, k);
       if (r != NULL) return r;
       if (--*k == 0) return root;
       return kthSmallest(root->right, k);
   }
   ```

   对本文的树，第 4 小的是 40。

2. 只比较节点和它的孩子不够：比如根是 50，左孩子是 30，30 的右孩子是 55。每个节点和孩子的关系都对，但 55 在 50 的左子树中，违反了规则。正确的做法是给每个节点一个**允许的范围**，向左走时上界变成当前节点的值，向右走时下界变成当前节点的值：

   ```c
   int isBST(const Node* root, long lo, long hi)     // 所有节点都要在 (lo, hi) 范围内
   {
       if (root == NULL) return 1;
       if (root->key <= lo || root->key >= hi) return 0;
       return isBST(root->left, lo, root->key) && isBST(root->right, root->key, hi);
   }
   ```

   初始调用 `isBST(root, LONG_MIN, LONG_MAX)`。对本文的树返回 1；把 35（在 50 的左子树中）改成 55 之后返回 0。另一种方法是做一次中序遍历，检查是否严格递增。

3. 也可以用**前驱**：左子树中最大的节点（在左子树中一路向右走到头）。它一定没有右孩子，删除它同样变成情况 1 或 2。

</details>
