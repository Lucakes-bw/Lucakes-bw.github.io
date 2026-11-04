---
title: 递归与回溯
date: 2026-11-05 10:00:00
updated: 2026-11-05 10:00:00
categories:
  - 数据结构
  - 算法设计
tags:
  - 算法
  - 递归
  - 回溯
order: 1
description: 前面的章节讲的是「数据怎么存」，这一章讲「问题怎么解」。第一种方法是回溯：把所有可能的选择画成一棵树，深度优先地走一遍，走不通就退回来换一条路。本文用同一个模板写出全排列、子集和 N 皇后，实测剪枝的效果：8 皇后不剪枝要访问 1917 万个结点，剪枝以后只要 2057 个。
disableNunjucks: true
viz: [ds-algo]
---

> **本文目标**：会写递归函数，知道终止条件和递归调用各自的作用；理解回溯的模板：做选择、递归、撤销选择；会用回溯解决排列、子集、N 皇后这类「枚举所有方案」的问题；理解剪枝为什么能把指数级的搜索变快很多。
>
> **前置知识**：《函数》（递归调用和调用栈）、《二叉树与遍历》（深度优先遍历）。

## 0. 这一章要做什么

前面几章讲的都是数据结构：线性表、树、图，以及在它们上面的排序和查找。这一章换一个角度：拿到一个问题，**用什么思路去解**。常用的思路只有几种：

| 篇 | 思路 | 什么时候用 |
|---|---|---|
| 1 | 回溯（本文） | 要列出所有方案，或者在所有方案中找满足条件的 |
| 2～4 | 动态规划 | 求最优值，并且大问题的答案可以由小问题的答案推出来 |
| 5 | 贪心 | 每一步都选当前最好的，就能得到全局最好的 |
| 6 | 双指针与滑动窗口 | 数组、字符串上的区间问题 |
| 7 | KMP | 字符串匹配 |

每篇的代码都在 [projects/algo](https://github.com/Lucakes-bw/Lucakes-bw.github.io/tree/main/projects/algo) 中，每个程序都会用暴力枚举或者另一种写法检查自己的结果。文章最后列出了[刷题](/problems/)板块中对应的题目。

## 1. 从递归说起

递归函数就是调用自己的函数。写一个递归函数，只需要想清楚两件事：

1. **什么时候直接返回**（终止条件）：问题小到可以直接回答；
2. **怎样把问题变小**：假设对更小的问题，这个函数已经能给出正确的答案，怎样用它来解决当前的问题。

不要试图在脑子里展开每一层调用，那样很快就会乱掉。相信「对更小的问题，函数是对的」就行了，这和数学归纳法是同一个道理。

## 2. 回溯：把选择画成一棵树

### 2.1 全排列

列出 1、2、3 的所有排列。手工列举时，我们会这样想：第一个位置可以是 1、2、3；如果第一个是 1，第二个可以是 2 或 3；如果第二个是 2，第三个只能是 3……

把这个过程画出来，就是一棵树：

<figure class="diagram">
<svg viewBox="0 0 700 230" role="img" aria-label="1、2、3 的全排列的选择树：根结点什么都没选，第一层选第一个数（1、2、3），第二层选第二个数，第三层选第三个数，6 个叶子就是 6 个排列">
  <defs><marker id="pt-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="pt-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <line x1="350" y1="40" x2="120" y2="68" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="120" y1="100" x2="65" y2="128" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="65" y1="160" x2="65" y2="184" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="120" y1="100" x2="175" y2="128" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="175" y1="160" x2="175" y2="184" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="350" y1="40" x2="350" y2="68" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="350" y1="100" x2="295" y2="128" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="295" y1="160" x2="295" y2="184" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="350" y1="100" x2="405" y2="128" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="405" y1="160" x2="405" y2="184" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="350" y1="40" x2="580" y2="68" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="580" y1="100" x2="525" y2="128" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="525" y1="160" x2="525" y2="184" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="580" y1="100" x2="635" y2="128" style="stroke:var(--border-2);stroke-width:1.5"/><line x1="635" y1="160" x2="635" y2="184" style="stroke:var(--border-2);stroke-width:1.5"/><circle cx="350" cy="24" r="16" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="350.0" y="28.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">·</text><circle cx="120" cy="84" r="16" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="120.0" y="88.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">1</text><circle cx="65" cy="144" r="16" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="65.0" y="148.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><circle cx="65" cy="200" r="16" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="65.0" y="204.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">3</text><circle cx="175" cy="144" r="16" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="175.0" y="148.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">3</text><circle cx="175" cy="200" r="16" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="175.0" y="204.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><circle cx="350" cy="84" r="16" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="350.0" y="88.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><circle cx="295" cy="144" r="16" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="295.0" y="148.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">1</text><circle cx="295" cy="200" r="16" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="295.0" y="204.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">3</text><circle cx="405" cy="144" r="16" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="405.0" y="148.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">3</text><circle cx="405" cy="200" r="16" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="405.0" y="204.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">1</text><circle cx="580" cy="84" r="16" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="580.0" y="88.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">3</text><circle cx="525" cy="144" r="16" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="525.0" y="148.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">1</text><circle cx="525" cy="200" r="16" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="525.0" y="204.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><circle cx="635" cy="144" r="16" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="635.0" y="148.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">2</text><circle cx="635" cy="200" r="16" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="635.0" y="204.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">1</text><text x="65.0" y="228.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">123</text><text x="175.0" y="228.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">132</text><text x="295.0" y="228.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">213</text><text x="405.0" y="228.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">231</text><text x="525.0" y="228.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">312</text><text x="635.0" y="228.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">321</text><text x="14.0" y="24.0" text-anchor="start" style="fill:var(--muted);font-size:11px">第几层 = 正在决定第几个位置</text>
  </g>
</svg>
</figure>

每条从根到叶子的路径就是一个排列。**回溯就是深度优先地遍历这棵树**：沿着一条路往下走，走到叶子就记下一个答案，然后退回上一层，换一个选择继续走。

```c
static int perm[16];
static bool used[16];

static void permute(int n, int depth)
{
    if (depth == n) {                       // 每个位置都选好了：得到一个排列
        /* 输出 perm[0..n-1] */
        return;
    }
    for (int x = 1; x <= n; ++x) {
        if (used[x]) continue;              // 已经用过的数不能再选
        used[x] = true;                     // 做选择
        perm[depth] = x;
        permute(n, depth + 1);
        used[x] = false;                    // 撤销选择
    }
}
```

```
123 132 213 231 312 321
```

`depth` 是当前在第几层（正在决定第几个位置），`perm` 和 `used` 记录这条路径上已经做了哪些选择。最关键的是最后一行**撤销选择**：递归返回以后，要把状态恢复成做选择之前的样子，才能尝试下一个选择。这就是「回溯」这个名字的来历。

几乎所有的回溯都是这个模板：

```
回溯(当前状态):
    if 已经是一个完整的方案:
        记录它；return
    for 每一个可以做的选择:
        做选择
        回溯(新的状态)
        撤销选择
```

n 个数的排列有 n! 个，回溯要把它们一个个列出来，时间至少是 O(n·n!)：

```
n=1：1 个排列
n=5：120 个排列
n=8：40320 个排列
n=10：3628800 个排列
```

增长得非常快。回溯适合「方案本身就是要的结果」或者「n 很小」的情况。

### 2.2 子集

列出 {1, 2, 3} 的所有子集。和排列的区别是：选出来的数没有顺序，{1, 2} 和 {2, 1} 是同一个子集。避免重复的方法是**只往后选**：选了 `a[i]` 以后，下一个只能从 `a[i+1]` 开始选。

```c
static void subsets(const int* a, int n, int start, int k)
{
    /* 输出 chosen[0..k-1]：每个结点都是一个子集（包括空集） */
    for (int i = start; i < n; ++i) {       // 只从 start 往后选，避免 {1,2} 和 {2,1} 重复
        chosen[k] = a[i];
        subsets(a, n, i + 1, k + 1);
    }
}
```

```
{} {1} {1,2} {1,2,3} {1,3} {2} {2,3} {3}
```

和全排列不同，这里**树上的每个结点**都是一个答案，而不只是叶子。另外 `chosen[k] = a[i]` 没有显式的撤销：下一次循环会直接覆盖 `chosen[k]`，而 `k` 本身是参数，返回时自动恢复了。

## 3. N 皇后：剪枝

在 n×n 的棋盘上放 n 个皇后，任意两个都不能互相攻击（不在同一行、同一列、同一条斜线上）。有多少种放法？

每行恰好一个皇后，所以问题变成：给每一行选一列。这又是一棵树：第 r 层决定第 r 行的皇后放在哪一列。

### 3.1 不剪枝：先放完，再检查

最直接的写法：每一行任选一列，n 行都放好以后，再检查有没有互相攻击：

```c
static long long queensNoPrune(int n, int row)
{
    if (row == n) return validAll(n) ? 1 : 0;   // 放完了才检查
    long long total = 0;
    for (int c = 0; c < n; ++c) {
        queenCol[row] = c;
        total += queensNoPrune(n, row + 1);
    }
    return total;
}
```

这棵树每个结点有 n 个孩子，一共有 nⁿ 个叶子。8 皇后有 8⁸ ≈ 1677 万种放法要检查。

### 3.2 剪枝：一冲突就不往下走

很多放法在前几行就已经冲突了，比如第 0 行和第 1 行的皇后放在了同一列，后面 6 行怎么放都没用。所以每放一个皇后，就检查它和前面的有没有冲突，冲突就跳过，**整棵子树都不用走了**：

```c
static bool safe(int row, int c)            // 第 row 行放在第 c 列，和前面的行冲突吗
{
    for (int r = 0; r < row; ++r) {
        if (queenCol[r] == c || abs(queenCol[r] - c) == row - r) return false;
    }
    return true;
}

static long long queensPrune(int n, int row)
{
    if (row == n) return 1;                 // 每一步都检查过了，走到底就是一个解
    long long total = 0;
    for (int c = 0; c < n; ++c) {
        if (!safe(row, c)) continue;        // 剪枝
        queenCol[row] = c;
        total += queensPrune(n, row + 1);
    }
    return total;
}
```

两个皇后在同一条斜线上，等价于行的差等于列的差的绝对值。下面的演示是 4 皇后的回溯过程：红色的格子是冲突的位置，被剪掉了；一行中没有任何安全的位置时，退回上一行，把那一行的皇后往右移。

<div class="dsviz" data-viz="algo-queens"></div>

### 3.3 再快一点：用数组记录占用情况

`safe` 每次要遍历前面所有的行，O(n)。可以用三个布尔数组记录哪些列、哪些斜线已经被占用了：同一条主对角线上 `row - col` 相同，同一条副对角线上 `row + col` 相同：

```c
static bool colUsed[32], diag1[64], diag2[64];   // 主对角线 r-c+n，副对角线 r+c

static long long queensFast(int n, int row)
{
    if (row == n) return 1;
    long long total = 0;
    for (int c = 0; c < n; ++c) {
        if (colUsed[c] || diag1[row - c + n] || diag2[row + c]) continue;
        colUsed[c] = diag1[row - c + n] = diag2[row + c] = true;    // 做选择
        total += queensFast(n, row + 1);
        colUsed[c] = diag1[row - c + n] = diag2[row + c] = false;   // 撤销选择
    }
    return total;
}
```

`row - c` 可能是负数，加上 n 变成合法的下标。

### 3.4 实测

三种写法访问的结点数和时间（Apple M6，`-O2`）：

| n | 解的个数 | 不剪枝的结点 | 时间 | 剪枝的结点 | 时间 | 布尔数组 |
|---|---|---|---|---|---|---|
| 4 | 2 | 341 | | 17 | | 17 |
| 6 | 4 | 55,987 | | 153 | | 153 |
| 8 | 92 | 19,173,961 | 0.034 s | 2,057 | 0.000 s | 2,057 |
| 10 | 724 | – | | 35,539 | 0.002 s | 0.001 s |
| 12 | 14,200 | – | | 856,189 | 0.046 s | 0.024 s |
| 14 | 365,596 | – | | 27,358,553 | 1.576 s | 0.802 s |

（不剪枝的写法只测到 8：9 皇后要 4 亿多个结点。）

- **剪枝把 8 皇后的结点数从 1917 万降到了 2057 个**，少了将近一万倍。这就是剪枝的威力：在树的上层剪掉一个分支，下面的整棵子树都省掉了；
- 布尔数组和逐个检查访问的结点数完全一样（剪掉的分支相同），只是每个结点的检查从 O(n) 变成了 O(1)，快了一倍；
- 即使剪了枝，结点数仍然随 n 指数增长，14 皇后要 2700 万个结点。回溯能解决的规模始终有限。

这里有一个普遍的规律：**检查越早，剪掉的越多**。能在做选择时就判断不行的，不要等到最后再判断。

## 4. 回溯的几个要点

1. **状态要完全恢复**。做选择时改了什么，撤销时就要改回什么。忘了恢复 `used[x]`，后面的排列就会少；
2. **避免重复**：如果方案没有顺序（子集、组合），规定一个顺序（只往后选）；如果输入中有相同的元素，排序后跳过相同的选择；
3. **尽早剪枝**：凡是能判断「这样走下去不可能得到答案」的，立即返回；
4. **复杂度通常是指数级的**，适合 n 在几十以内的问题。要的是最优值而不是所有方案时，往往有更好的方法，比如下一篇的动态规划。

## 5. 常见错误

1. **忘了撤销选择**。结果比正确答案少，或者有重复。
2. **把引用或指针加进结果，而不是复制一份**。比如把同一个数组的指针反复加进结果列表，最后所有的结果都变成了同一个（最后一次的内容）。
3. **终止条件写错**。`depth == n` 写成 `depth == n - 1`，最后一个位置没有选。
4. **剪枝的条件太强**，把正确的分支也剪掉了。剪枝以后，用小规模的输入和不剪枝的写法对比结果（本文的程序就是这样检查的）。
5. **递归太深导致栈溢出**。回溯的深度通常等于方案的长度，一般不会太深；但如果每层在栈上放了大数组，就可能溢出。

## 6. 练习

刷题板块中的回溯题：

- [全排列](/problems/permutations/)、[子集](/problems/subsets/)：本文的模板
- [组合总和](/problems/combination-sum/)：同一个数可以选多次，怎样避免重复的组合
- [电话号码的字母组合](/problems/letter-combinations-of-a-phone-number/)、[括号生成](/problems/generate-parentheses/)
- [单词搜索](/problems/word-search/)：在二维网格上回溯，走过的格子要标记、回来时恢复
- [N 皇后](/problems/n-queens/)、[分割回文串](/problems/palindrome-partitioning/)

思考题：

1. 如果输入的数中有重复的，比如 {1, 1, 2}，全排列的写法会输出重复的排列。怎样修改？
2. N 皇后的结点数中，有多少是叶子（解）？8 皇后访问了 2057 个结点，得到 92 个解，其余的结点在做什么？
3. 用位运算代替三个布尔数组：用三个整数的二进制位表示被占用的列和斜线。这能让 14 皇后快多少？

<details>
<summary>点击查看答案</summary>

1. 先把数组排序，让相同的数挨在一起。在同一层中，如果当前的数和前一个数相同，并且前一个数没有被使用（说明它在这一层已经被试过了），就跳过：`if (i > 0 && a[i] == a[i-1] && !used[i-1]) continue;`。这样每一层中相同的值只会被选一次。

2. 92 个结点是解（第 8 层）。其余 1965 个结点是中间状态：放了 1 到 7 个皇后、互不冲突的局面。其中很多走到某一行时发现没有任何安全的位置，就退回去了，这些是「死胡同」。剪枝的效率取决于死胡同出现得有多早。

3. 用 `cols`、`d1`、`d2` 三个整数，可用的位置是 `avail = ~(cols | d1 | d2) & ((1 << n) - 1)`，每次取最低位 `p = avail & -avail`，递归时传入 `cols | p`、`(d1 | p) << 1`、`(d2 | p) >> 1`（斜线的影响下一行左移、右移一位）。不用遍历所有的列，只遍历可用的列，并且没有数组访问。在同一台电脑上实测，14 皇后用了 0.112 秒，是布尔数组写法（0.802 秒）的 7 倍多。

</details>

下一篇：[动态规划（一）：从记忆化搜索到递推](/posts/algo-dp-basics/)。
