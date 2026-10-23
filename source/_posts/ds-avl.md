---
title: AVL 树
date: 2026-10-23 22:00:00
updated: 2026-10-23 22:00:00
categories:
  - 数据结构
  - 树
tags:
  - 数据结构
  - 平衡树
order: 3
description: 普通二叉搜索树在有序插入时会退化成链表。AVL 树在每次插入后检查平衡因子，失衡时通过旋转恢复平衡，保证高度始终是 O(log n)。本文实现四种旋转和插入，配有可以观察旋转过程的逐步演示；实测有序插入 100 万个数，高度只有 20。
top_group_list: true
disableNunjucks: true
viz: [ds-avl]
---

> **本文目标**：理解平衡因子；掌握左旋和右旋，理解旋转为什么不破坏二叉搜索树的性质；能区分 LL、RR、LR、RL 四种失衡情况；会实现 AVL 树的插入；了解平衡树的代价和其他选择。
>
> **前置知识**：《二叉搜索树》。

## 1. 平衡因子

上一篇实测，有序插入 10 万个数，普通二叉搜索树的高度是 100000，查找慢了 500 多倍。解决的办法是：**在插入的过程中不断调整树的形状，让它始终保持「平衡」**。

AVL 树（以两位发明者 Adelson-Velsky 和 Landis 的名字命名，1962 年）对平衡的定义是：**每个节点的左右子树高度之差不超过 1**。

左子树的高度减去右子树的高度，叫作这个节点的**平衡因子**（balance factor）。AVL 树中每个节点的平衡因子只能是 -1、0、+1。

```c
typedef struct AVLNode {
    int key;
    int height;                                 // 以这个节点为根的子树的高度，叶子为 1
    struct AVLNode* left;
    struct AVLNode* right;
} AVLNode;

static int height(const AVLNode* n) { return n == NULL ? 0 : n->height; }
static int maxInt(int a, int b) { return a > b ? a : b; }
static void update(AVLNode* n) { n->height = maxInt(height(n->left), height(n->right)) + 1; }
static int balance(const AVLNode* n) { return height(n->left) - height(n->right); }   // 平衡因子
```

每个节点保存自己的高度，这样计算平衡因子是 O(1) 的，不需要每次遍历子树。

可以证明，满足这个条件的树，高度不超过 1.44 log₂(n + 2)，所以所有操作都是 O(log n)。

## 2. 旋转

插入一个节点后，从它到根的路径上，某些节点的平衡因子可能变成 +2 或 -2。修复的方法是**旋转**：在不破坏二叉搜索树性质的前提下，改变几个节点之间的父子关系，让高的一侧降低一层。

### 2.1 右旋

<figure class="diagram">
<svg viewBox="0 0 680 210" role="img" aria-label="右旋示意图：节点 y 的左孩子 x 升上来成为根，y 成为 x 的右孩子，x 原来的右子树 T2 改挂到 y 的左边">
  <defs>
    <marker id="rot-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--accent)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:14px">
    <g style="stroke:var(--fg-2);stroke-width:1.6">
      <line x1="140" y1="40" x2="90" y2="95"/><line x1="140" y1="40" x2="200" y2="95"/>
      <line x1="90" y1="95" x2="50" y2="155"/><line x1="90" y1="95" x2="130" y2="155"/>
      <line x1="540" y1="40" x2="480" y2="95"/><line x1="540" y1="40" x2="590" y2="95"/>
      <line x1="590" y1="95" x2="550" y2="155"/><line x1="590" y1="95" x2="630" y2="155"/>
    </g>
    <g style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5">
      <circle cx="140" cy="40" r="18"/><circle cx="90" cy="95" r="18"/>
      <circle cx="540" cy="40" r="18"/><circle cx="590" cy="95" r="18"/>
    </g>
    <g style="fill:var(--panel);stroke:var(--border-2);stroke-dasharray:4 3">
      <polygon points="50,148 30,190 70,190"/><polygon points="130,148 110,190 150,190"/><polygon points="200,88 180,130 220,130"/>
      <polygon points="480,88 460,130 500,130"/><polygon points="550,148 530,190 570,190"/><polygon points="630,148 610,190 650,190"/>
    </g>
    <g style="fill:var(--fg)" text-anchor="middle">
      <text x="140" y="45">y</text><text x="90" y="100">x</text>
      <text x="540" y="45">x</text><text x="590" y="100">y</text>
      <text x="50" y="183">T1</text><text x="130" y="183" style="fill:var(--warn)">T2</text><text x="200" y="123">T3</text>
      <text x="480" y="123">T1</text><text x="550" y="183" style="fill:var(--warn)">T2</text><text x="630" y="183">T3</text>
    </g>
    <line x1="270" y1="100" x2="400" y2="100" style="stroke:var(--accent);stroke-width:2" marker-end="url(#rot-arrow)"/>
    <text x="335" y="88" text-anchor="middle" style="fill:var(--accent)">对 y 右旋</text>
    <text x="335" y="125" text-anchor="middle" style="fill:var(--muted);font-size:12px">T1 &lt; x &lt; T2 &lt; y &lt; T3</text>
  </g>
</svg>
</figure>

y 的左子树太高时，对 y **右旋**：

- y 的左孩子 x **升上来**，成为这棵子树新的根；
- y **降下去**，成为 x 的右孩子；
- x 原来的右子树 T2 **改挂**到 y 的左边。

为什么 T2 可以挂在 y 的左边？因为 T2 原来在 x 的右边、y 的左边，所以 **x < T2 < y**，挂在 y 的左边正好满足二叉搜索树的性质。旋转前后，中序遍历的顺序都是 T1 x T2 y T3，**完全没有变**。

```c
static AVLNode* rotateRight(AVLNode* y)         // 右旋：左孩子 x 升上来
{
    AVLNode* x = y->left;
    y->left = x->right;                         // x 的右子树改挂到 y 的左边
    x->right = y;
    update(y);                                  // 先更新下面的 y，再更新上面的 x
    update(x);
    return x;                                   // x 成为这棵子树新的根
}

static AVLNode* rotateLeft(AVLNode* x)          // 左旋：右孩子 y 升上来
{
    AVLNode* y = x->right;
    x->right = y->left;
    y->left = x;
    update(x);
    update(y);
    return y;
}
```

左旋和右旋完全对称。旋转只修改了几个指针，是 O(1) 的。注意更新高度的顺序：旋转后 y 在 x 的下面，必须**先更新 y，再更新 x**，因为 x 的高度依赖于 y 的高度。

### 2.2 四种失衡

设失衡的节点是 n，根据新节点插在了 n 的哪个位置，分为四种情况：

| 类型 | 新节点的位置 | n 的平衡因子 | 孩子的平衡因子 | 调整 |
|---|---|---|---|---|
| LL | n 的左孩子的左子树 | +2 | ≥ 0 | 对 n 右旋 |
| RR | n 的右孩子的右子树 | -2 | ≤ 0 | 对 n 左旋 |
| LR | n 的左孩子的右子树 | +2 | < 0 | 先对左孩子左旋（变成 LL），再对 n 右旋 |
| RL | n 的右孩子的左子树 | -2 | > 0 | 先对右孩子右旋（变成 RR），再对 n 左旋 |

LR 和 RL 为什么要旋转两次？以 LR 为例：插入 30、10、20，20 在 30 的左孩子 10 的右边，形状像一个「之」字。如果直接对 30 右旋，10 升上来，20 跟着改挂到 30 的左边，结果仍然是一个「之」字，只是方向反了。所以要先对 10 左旋，把「之」字拉直成一条向左的斜线（LL），再对 30 右旋。

```c
static AVLNode* rebalance(AVLNode* n)
{
    update(n);
    int b = balance(n);
    if (b > 1) {                                // 左边太高
        if (balance(n->left) < 0) {
            n->left = rotateLeft(n->left);      // LR：先把左孩子左旋，变成 LL
        }
        return rotateRight(n);                  // LL：右旋
    }
    if (b < -1) {                               // 右边太高
        if (balance(n->right) > 0) {
            n->right = rotateRight(n->right);   // RL：先把右孩子右旋，变成 RR
        }
        return rotateLeft(n);                   // RR：左旋
    }
    return n;
}
```

## 3. 插入

插入的过程和普通二叉搜索树一样，只是在**递归返回的路上**，对路径上的每个节点调用 `rebalance`：

```c
AVLNode* insert(AVLNode* root, int key)
{
    if (root == NULL) {
        AVLNode* n = malloc(sizeof(AVLNode));
        if (n == NULL) exit(1);
        n->key = key;
        n->height = 1;
        n->left = n->right = NULL;
        return n;
    }
    if (key < root->key) root->left = insert(root->left, key);
    else if (key > root->key) root->right = insert(root->right, key);
    else return root;                           // 已经存在
    return rebalance(root);                     // 返回的路上，逐层检查并调整
}
```

`insert` 返回的是这棵子树（可能经过旋转之后的）新的根，调用者用它更新自己的孩子指针：`root->left = insert(root->left, key)`。这就把旋转的结果接回了树中。

在演示中点击「有序插入 1..7」，观察每次失衡时的旋转，节点会移动到旋转后的位置；也可以清空后逐个插入，试试 30、10、20：

<div class="dsviz" data-viz="avl-insert"></div>

```c
AVLNode* root = NULL;
for (int k = 1; k <= 7; ++k) root = insert(root, k);
printf("有序插入 1..7，前序: "); preorder(root); printf("，高度 %d\n", height(root));
```

```
有序插入 1..7，前序: 4 2 1 3 6 5 7 ，高度 3
插入 30 10 20，前序: 20 10 30 
```

普通二叉搜索树中，1 到 7 有序插入会退化成高度为 7 的链表；AVL 树得到的是高度为 3 的满二叉树。

可以证明，**插入时最多只需要一次调整**（单旋转或双旋转）就能让整棵树恢复平衡：调整后那棵子树的高度恢复到了插入之前，它上面的节点就都不会失衡了。所以插入的总代价是 O(log n)。

## 4. 实测

和上一篇一样，用 AVL 树有序地插入 n 个数（普通二叉搜索树的最坏情况），然后查找每个数（`-O2`）：

```
有序插入 n = 100000   高度 17（log2 n ≈ 16.6），建树   18.2 ms，查找 n 次    3.9 ms
有序插入 n = 1000000  高度 20（log2 n ≈ 19.9），建树   80.2 ms，查找 n 次   25.2 ms
```

| n = 10 万，有序插入 | 高度 | 建树 | 查找 n 次 |
|---|---|---|---|
| 普通二叉搜索树（上一篇） | 100000 | 3672 ms | 3630 ms |
| AVL 树 | 17 | 18.2 ms | 3.9 ms |

高度从 10 万降到了 17，几乎正好是 log₂n；建树快了 200 倍，查找快了 900 多倍。100 万个数时高度也只有 20。程序用 `-fsanitize=address,undefined` 和 `leaks` 检查过。

## 5. 删除和其他平衡树

AVL 树的删除也是先按普通二叉搜索树的方法删除，再沿着路径向上 `rebalance`。和插入不同的是，**删除后可能需要沿路径一直旋转到根**，因为旋转可能让子树变矮，从而让上面的节点失衡。删除时判断失衡类型的条件也稍有不同（孩子的平衡因子可能为 0）。

AVL 树的平衡条件非常严格，所以树很矮，查找很快；代价是插入和删除时旋转比较频繁。实际中还有其他平衡树：

- **红黑树**：平衡条件宽松一些（最长路径不超过最短路径的 2 倍），插入删除时旋转更少。C++ 的 `std::map`、`std::set` 和 Java 的 `TreeMap` 都是红黑树。
- **B 树、B+ 树**：每个节点有很多个孩子，树非常矮，适合存放在磁盘上，是数据库索引和文件系统的基础。
- **跳表**：不是树，而是多层的链表，用随机化达到平均 O(log n)，Redis 的有序集合就是用它实现的。

## 6. 常见错误

1. **旋转后更新高度的顺序反了**：先更新了上面的节点，用的是下面节点的旧高度。
2. **旋转后没有把新的根接回父节点**：`insert` 要返回新的根，调用者要赋值。
3. **LR、RL 型只旋转一次**：形状从一个「之」字变成反方向的「之」字，仍然失衡。
4. **判断失衡类型时比较的是新插入的值**：应该看孩子的平衡因子，删除时没有「新插入的值」可以比较。
5. **忘记维护 `height` 字段**：每次修改孩子之后都要更新。

## 7. 小结

- AVL 树中每个节点左右子树的高度差不超过 1，高度是 O(log n)
- 旋转只修改几个指针，不改变中序遍历的顺序，O(1)
- 四种失衡：LL 右旋，RR 左旋，LR 先左旋左孩子再右旋，RL 先右旋右孩子再左旋
- 插入后沿路径向上检查平衡因子，插入最多需要一次调整
- 实测有序插入 10 万个数：高度 17，查找比普通二叉搜索树快了 900 多倍

## 8. 练习

1. 依次向空的 AVL 树中插入 10、20、30、40、50、25，画出每一步之后的树，写出最后的前序遍历。
2. 解释为什么右旋之后，中序遍历的顺序不变。
3. 一棵高度为 h 的 AVL 树最少有多少个节点？写出递推式。

<details>
<summary>点击查看答案</summary>

1. 用本文的代码逐步插入，每一步之后的前序遍历：

   ```
   插入 10 后前序: 10 
   插入 20 后前序: 10 20 
   插入 30 后前序: 20 10 30 
   插入 40 后前序: 20 10 30 40 
   插入 50 后前序: 20 10 40 30 50 
   插入 25 后前序: 30 20 10 25 40 50
   ```

   插入 30 时 10 失衡（RR），左旋，20 成为根；插入 50 时 30 失衡（RR），左旋，40 升上来；插入 25 时根 20 失衡，新节点在右孩子 40 的左子树中（RL），先对 40 右旋，再对 20 左旋，30 成为新的根。

2. 旋转前，中序遍历是 T1 x T2 y T3（x 是 y 的左孩子，T2 是 x 的右子树）。旋转后，x 是根，左边是 T1，右边是 y；y 的左边是 T2，右边是 T3。中序遍历是 T1 x (T2 y T3)，和原来完全一样。

3. 设 N(h) 是高度为 h 的 AVL 树的最少节点数。根的两棵子树高度最多差 1，节点最少时一棵高度为 h - 1，另一棵为 h - 2：N(h) = N(h-1) + N(h-2) + 1，N(1) = 1，N(2) = 2。依次是 1、2、4、7、12、20、33……和斐波那契数列的增长速度相同（指数增长），所以 n 个节点的 AVL 树高度是 O(log n)。

</details>
