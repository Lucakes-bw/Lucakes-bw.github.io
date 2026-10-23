---
title: 二叉树与遍历
date: 2026-10-23 21:20:00
updated: 2026-10-23 21:20:00
categories:
  - 数据结构
  - 树
tags:
  - 数据结构
  - 二叉树
  - 递归
order: 1
description: 树是一种分层的数据结构，二叉树是其中最重要的一种。本文介绍树的基本术语和二叉树的性质，用 C 语言实现二叉树的建立、四种遍历、求高度和由遍历序列还原二叉树；三个逐步演示分别展示递归遍历时调用栈的变化、层序遍历中的队列，以及用栈实现的非递归中序遍历。
top_group_list: true
disableNunjucks: true
viz: [ds-tree]
---

> **本文目标**：掌握树和二叉树的基本术语；了解二叉树的性质和存储方式；会实现前序、中序、后序和层序遍历；理解递归遍历时调用栈的变化，会用显式的栈改写成非递归；会由前序和中序序列还原二叉树。
>
> **前置知识**：《栈》、《队列》，C 语言的《函数》（递归）。

## 1. 树

前面的线性表中，每个元素最多只有一个后继。而现实中很多数据是**分层**的：文件系统中一个目录下有多个子目录，公司中一个部门下有多个小组，HTML 中一个标签里嵌套着多个标签。描述这种一对多的关系，需要**树**。

树的基本术语：

| 术语 | 含义 |
|---|---|
| 根 | 最上面的节点，没有父节点 |
| 父节点、孩子 | 相连的两个节点中，上面的是下面的父节点，下面的是上面的孩子 |
| 叶子 | 没有孩子的节点 |
| 子树 | 一个节点和它所有的后代组成的树 |
| 深度（层） | 根在第 1 层，根的孩子在第 2 层，依此类推 |
| 高度 | 树中最大的层数 |
| 度 | 一个节点的孩子个数 |

## 2. 二叉树

**二叉树**的每个节点**最多有两个孩子**，而且分为**左孩子**和**右孩子**，顺序不能交换。

```c
typedef struct TreeNode {
    char value;
    struct TreeNode* left;
    struct TreeNode* right;
} TreeNode;
```

和链表一样，二叉树的节点通过指针相连，只是每个节点有两个「下一个」。没有孩子时，对应的指针是 `NULL`。

### 2.1 性质

- 第 i 层最多有 2^(i-1) 个节点；高度为 h 的二叉树最多有 2^h - 1 个节点。
- 如果每一层都是满的，叫**满二叉树**。如果除了最后一层都是满的，而且最后一层的节点都靠左排列，叫**完全二叉树**。
- n 个节点的二叉树，高度最少是 ⌈log₂(n+1)⌉（完全二叉树），最多是 n（每个节点只有一个孩子，退化成了链表）。**高度决定了很多操作的效率**，这是后面二叉搜索树和平衡树要解决的核心问题。
- 叶子的个数 = 有两个孩子的节点的个数 + 1。

完全二叉树还可以**用数组存储**：把节点按层从左到右编号，存到数组中，下标为 i 的节点（从 0 开始），左孩子是 2i + 1，右孩子是 2i + 2，父节点是 (i - 1) / 2，不需要指针。第 3.4 篇的堆就是这样存储的。

### 2.2 建立二叉树

用一个字符串按**前序**的顺序描述一棵树，`#` 表示空树。例如 `ABD##E##CF###` 表示：根是 A；A 的左子树以 B 为根，B 的左右孩子是 D 和 E（它们的孩子都是空）；A 的右子树以 C 为根，C 的左孩子是 F，右孩子为空。

```c
TreeNode* newNode(char value)
{
    TreeNode* n = malloc(sizeof(TreeNode));
    if (n == NULL) exit(1);
    n->value = value;
    n->left = n->right = NULL;
    return n;
}

// 按前序读取，'#' 表示空树。例如 "ABD##E##CF###"
TreeNode* buildPreorder(const char** s)
{
    char c = **s;
    (*s)++;
    if (c == '#' || c == '\0') return NULL;
    TreeNode* node = newNode(c);
    node->left = buildPreorder(s);
    node->right = buildPreorder(s);
    return node;
}
```

参数是 `const char**`：递归函数要在读取的过程中**移动调用者的指针**，所以要传指针的地址（《指针进阶》中的二级指针）。

## 3. 深度优先遍历

**遍历**就是按某种顺序访问树中的每一个节点恰好一次。二叉树的结构是递归的：一棵二叉树由根、左子树、右子树组成，左右子树又各自是一棵二叉树。所以遍历最自然的写法就是递归。按照**访问根的时机**，分为三种：

```c
void preorder(const TreeNode* root)
{
    if (root == NULL) return;
    printf("%c ", root->value);                 // 根
    preorder(root->left);                       // 左
    preorder(root->right);                      // 右
}

void inorder(const TreeNode* root)
{
    if (root == NULL) return;
    inorder(root->left);
    printf("%c ", root->value);
    inorder(root->right);
}

void postorder(const TreeNode* root)
{
    if (root == NULL) return;
    postorder(root->left);
    postorder(root->right);
    printf("%c ", root->value);
}
```

- **前序**（根左右）：先访问根，再遍历左子树，最后遍历右子树。
- **中序**（左根右）：先遍历左子树，再访问根，最后遍历右子树。
- **后序**（左右根）：先遍历左右子树，最后访问根。

三个函数只是 `printf` 的位置不同。在演示中分别运行三种遍历，注意右侧的**调用栈**：每调用一次函数压入一层，遇到 `NULL` 立即返回。正在处理的节点是绿色边框，在调用栈中等待返回的祖先节点是黄色，已经访问过的节点被填充：

<div class="dsviz" data-viz="tree-recursive"></div>

对 `ABD##E##CF###` 这棵树：

```
前序: A B D E C F 
中序: D B E A F C 
后序: D E B F C A 
```

三种遍历走过的路径其实完全一样（演示中三者的步数都是 38），区别只在于**什么时候输出**。每个节点被访问一次，时间复杂度 O(n)；递归的深度等于树的高度 h，空间复杂度 O(h)。

不同的遍历顺序有不同的用途：

- **前序**：复制一棵树、按前序序列化（就像 `ABD##E##CF###`）；
- **中序**：对二叉搜索树，中序遍历得到的是**有序**序列（下一篇）；
- **后序**：先处理孩子再处理父节点，比如释放整棵树、计算目录占用的总空间、求高度。

```c
int height(const TreeNode* root)
{
    if (root == NULL) return 0;
    int l = height(root->left), r = height(root->right);
    return (l > r ? l : r) + 1;
}

int countLeaves(const TreeNode* root)
{
    if (root == NULL) return 0;
    if (root->left == NULL && root->right == NULL) return 1;
    return countLeaves(root->left) + countLeaves(root->right);
}

void freeTree(TreeNode* root)
{
    if (root == NULL) return;
    freeTree(root->left);                       // 必须先释放孩子
    freeTree(root->right);
    free(root);
}
```

这些都是后序的思路：先求出左右子树的结果，再合并成自己的结果。`freeTree` 必须是后序的，如果先 `free(root)`，再访问 `root->left` 就是释放后使用。

## 4. 层序遍历

**层序遍历**一层一层地访问：先访问第 1 层（根），再从左到右访问第 2 层，然后第 3 层……它需要一个**队列**：访问一个节点时，把它的孩子放到队尾，排在同一层的其他节点后面。

```c
void levelOrder(TreeNode* root)
{
    if (root == NULL) return;
    TreeNode* queue[100];
    int head = 0, tail = 0;
    queue[tail++] = root;
    while (head < tail) {
        TreeNode* node = queue[head++];         // 出队
        printf("%c ", node->value);
        if (node->left != NULL) queue[tail++] = node->left;     // 孩子入队
        if (node->right != NULL) queue[tail++] = node->right;
    }
}
```

（这里为了简洁用了一个固定大小的数组作为队列，实际中应该用《队列》中的循环队列。）

<div class="dsviz" data-viz="tree-level"></div>

```
层序: A B C D E F 
```

层序遍历是**广度优先搜索**（BFS）在树上的应用，前三种是**深度优先搜索**（DFS）。第 4 章的图也会用到这两种搜索方式。

## 5. 非递归遍历

递归遍历的空间是 O(h)，如果树很高（比如退化成链表的 10 万个节点），递归太深会导致栈溢出（《函数》第 3.2 节）。这时可以**用一个显式的栈来模拟调用栈**。以中序遍历为例：

```c
void inorderIterative(TreeNode* root)
{
    TreeNode* stack[100];
    int top = 0;
    TreeNode* cur = root;
    while (cur != NULL || top > 0) {
        while (cur != NULL) {                   // 一路向左，沿途的节点入栈
            stack[top++] = cur;
            cur = cur->left;
        }
        cur = stack[--top];                     // 左边走到头了，访问栈顶
        printf("%c ", cur->value);
        cur = cur->right;                       // 转向右子树
    }
}
```

思路是：中序要先处理完左子树才能访问根，所以一路向左，把沿途的节点都记在栈中；走到头之后，弹出的栈顶节点的左子树已经处理完了，访问它，然后转向它的右子树，重复同样的过程。和递归版本的演示对比一下，这个栈中的内容正是递归版本调用栈中「等待返回」的那些节点：

<div class="dsviz" data-viz="tree-inorder-iter"></div>

```
中序（非递归）: D B E A F C 
```

显式的栈分配在堆上（或者用更大的数组），不受系统栈大小的限制。

## 6. 由遍历序列还原二叉树

只给出一种遍历序列，无法唯一确定一棵二叉树。但**前序和中序**（或者**后序和中序**）可以：

- 前序的第一个元素是**根**；
- 在中序中找到根，它**左边的是左子树，右边的是右子树**，于是知道了左子树有 k 个节点；
- 前序中根后面的 k 个元素是左子树的前序，再后面的是右子树的前序；
- 对左右子树递归。

```c
TreeNode* rebuild(const char* pre, const char* in, int n)
{
    if (n == 0) return NULL;
    TreeNode* root = newNode(pre[0]);           // 前序的第一个是根
    int k = (int)(strchr(in, pre[0]) - in);     // 根在中序中的位置：左边 k 个属于左子树
    root->left = rebuild(pre + 1, in, k);
    root->right = rebuild(pre + 1 + k, in + k + 1, n - 1 - k);
    return root;
}
```

```
由前序 ABDECF 和中序 DBEAFC 还原，后序: D E B F C A 
```

还原出的树的后序遍历和原来的树完全相同。（这里假设节点的值各不相同，否则无法在中序中唯一地找到根。）

整个程序用 `-fsanitize=address,undefined` 检查过，`leaks` 报告没有泄漏。

## 7. 常见错误

1. **递归函数没有处理 `NULL`**：访问空指针，程序崩溃。
2. **混淆三种遍历的顺序**：记住「前、中、后」指的是**根**的位置。
3. **先释放根，再释放孩子**：释放后使用。
4. **对很高的树使用递归**：栈溢出。
5. **层序遍历用栈而不是队列**：得到的不是按层的顺序。
6. **由前序和后序还原二叉树**：无法唯一确定（比如只有一个孩子时，无法知道它是左孩子还是右孩子）。
7. **节点的值有重复时，用值来定位中序中的根**。

## 8. 小结

- 二叉树的每个节点最多有左右两个孩子；n 个节点的高度在 log n 到 n 之间
- 前序（根左右）、中序（左根右）、后序（左右根）只是访问根的时机不同，都是 O(n) 时间、O(h) 空间
- 后序适合「先算孩子再算自己」的问题，比如求高度、释放树
- 层序遍历用队列，是树上的广度优先搜索
- 递归遍历可以用显式的栈改写，避免栈溢出
- 前序 + 中序（或后序 + 中序）可以唯一还原一棵二叉树

## 9. 练习

1. 写一个函数 `void mirror(TreeNode* root)`，把二叉树变成它的镜像（交换每个节点的左右孩子）。对 `ABD##E##CF###` 镜像后，前序和中序遍历分别是什么？
2. 已知后序序列 `DEBFCA` 和中序序列 `DBEAFC`，画出这棵树，写出它的前序序列。
3. 二叉树的**宽度**是节点最多的那一层的节点数。修改层序遍历，求二叉树的宽度。

<details>
<summary>点击查看答案</summary>

1. 对每个节点交换左右孩子，然后递归处理两个子树：

   ```c
   void mirror(TreeNode* root)
   {
       if (root == NULL) return;
       TreeNode* t = root->left;                   // 交换左右孩子
       root->left = root->right;
       root->right = t;
       mirror(root->left);
       mirror(root->right);
   }
   ```

   镜像后前序是 `A C F B E D`，中序是 `C F A E B D`，正好是原来中序序列的反转。

2. 后序的最后一个 A 是根。在中序中，A 左边的 `DBE` 是左子树，右边的 `FC` 是右子树。对左子树：后序 `DEB` 的最后一个 B 是根，D 是左孩子，E 是右孩子；对右子树：后序 `FC` 的根是 C，中序 `FC` 中 F 在 C 的左边，所以 F 是 C 的左孩子。这就是本文例子中的那棵树，前序是 `ABDECF`。

3. 关键是每次处理**一整层**：进入循环时，队列中的节点正好是同一层的全部节点，先记下个数，再把这些节点逐个出队、把它们的孩子入队：

   ```c
   int maxWidth(TreeNode* root)
   {
       if (root == NULL) return 0;
       TreeNode* queue[100];
       int head = 0, tail = 0, best = 0;
       queue[tail++] = root;
       while (head < tail) {
           int levelSize = tail - head;            // 当前队列中的节点正好是一整层
           if (levelSize > best) best = levelSize;
           for (int i = 0; i < levelSize; ++i) {
               TreeNode* node = queue[head++];
               if (node->left) queue[tail++] = node->left;
               if (node->right) queue[tail++] = node->right;
           }
       }
       return best;
   }
   ```

   对本文的树，第 3 层有 D、E、F 三个节点，宽度是 3。

</details>
