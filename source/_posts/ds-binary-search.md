---
title: 二分查找
date: 2026-10-23 22:56:00
updated: 2026-10-23 22:56:00
categories:
  - 数据结构
  - 排序与查找
tags:
  - 数据结构
  - 查找
  - 二分
order: 4
description: 二分查找的思想人人都懂，但要写对却出奇地难。本文讲清楚区间的定义和循环不变式，实现标准二分、lowerBound、upperBound，配有逐步演示；用 UBSan 复现 (lo + hi) / 2 的整数溢出，复现 lo = mid 导致的死循环，并介绍「在答案上二分」的用法。
top_group_list: true
disableNunjucks: true
viz: [ds-bsearch]
---

> **本文目标**：理解二分查找的前提和复杂度；能根据区间的定义写出不会出错的二分查找；会用 lowerBound 和 upperBound 查找边界；避免整数溢出和死循环；会在答案上二分。
>
> **前置知识**：《复杂度分析》（第 3.2 节已经演示过二分查找的基本过程）。

## 1. 为什么二分查找容易写错

二分查找的思想非常简单：在一个**有序**的数组中查找，每次和中间的元素比较，排除一半。n 个元素最多比较 log₂n + 1 次，100 万个元素只要 20 次。

但写对它并不容易。Jon Bentley 在《编程珠玑》中提到，他让专业程序员写二分查找，大约 90% 的人写出的代码有 bug。Java 标准库中的二分查找也曾有一个整数溢出的 bug，存在了将近 10 年才被发现。

错误主要出在几个细节上：

- `hi` 的初值是 `n` 还是 `n - 1`？
- 循环条件是 `lo < hi` 还是 `lo <= hi`？
- 更新时是 `hi = mid` 还是 `hi = mid - 1`？
- `mid` 怎么算才不会溢出？

解决的办法是：**先确定区间的含义，然后让每一行代码都符合这个含义**。

## 2. 标准二分查找

用**闭区间** `[lo, hi]` 表示「要找的元素如果存在，一定在这个范围内」：

```c
int binarySearch(const int* a, int n, int key)  // 找到返回下标，否则返回 -1
{
    int lo = 0, hi = n - 1;                     // 在闭区间 [lo, hi] 中查找
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] == key) return mid;
        if (a[mid] < key) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}
```

每一行都由「闭区间」这个定义决定：

- 一开始整个数组都有可能，`[0, n - 1]`；
- `lo == hi` 时区间里还有一个元素，所以条件是 `lo <= hi`；
- `a[mid] < key`：mid 本身和它左边的都不可能，新的区间是 `[mid + 1, hi]`；
- `a[mid] > key`：新的区间是 `[lo, mid - 1]`。

```
binarySearch(5) = 4, binarySearch(4) = -1, binarySearch(3) = 3
```

（数组是 `{1, 3, 3, 3, 5, 8, 8, 13}`。）注意查找 3 返回的是下标 3，而数组中 3 出现在下标 1、2、3。有重复元素时，标准二分查找返回的是**其中任意一个**，至于是哪一个取决于比较的过程。很多时候我们需要的是**第一个**或**最后一个**，这就需要下面的边界查找。

## 3. 查找边界：lowerBound 和 upperBound

- **lowerBound(key)**：第一个**大于等于** key 的位置；
- **upperBound(key)**：第一个**大于** key 的位置。

这次用**左闭右开**区间 `[lo, hi)` 表示「答案一定在这个范围内」，答案可能是 n（所有元素都比 key 小），所以 `hi` 的初值是 `n`：

```c
int lowerBound(const int* a, int n, int key)    // 第一个 >= key 的位置，没有则为 n
{
    int lo = 0, hi = n;                         // 在左闭右开区间 [lo, hi) 中查找
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] < key) lo = mid + 1;         // mid 及左边都太小
        else hi = mid;                          // mid 可能就是答案，保留
    }
    return lo;
}

int upperBound(const int* a, int n, int key)    // 第一个 > key 的位置
{
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] <= key) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}
```

和标准二分的区别：

- 区间是 `[lo, hi)`，`lo == hi` 时为空，所以条件是 `lo < hi`；
- `a[mid] >= key` 时，**mid 可能就是答案**，不能排除它，所以是 `hi = mid`，而不是 `mid - 1`；
- 循环结束时 `lo == hi`，就是答案。

两个函数只差一个等号。在演示中分别查找 3、4、0、100，观察区间（亮着的部分）怎样缩小：

<div class="dsviz" data-viz="lower-bound"></div>

```
lowerBound(3) = 1, upperBound(3) = 4，3 出现了 3 次
lowerBound(4) = 4, lowerBound(100) = 8, lowerBound(0) = 0
```

- **`[lowerBound(key), upperBound(key))` 就是所有等于 key 的元素**，两者相减就是 key 出现的次数；
- 查找不存在的 4，lowerBound 返回 4，正好是 4 应该**插入**的位置，插入后数组仍然有序；
- C++ 的 `std::lower_bound`、`std::upper_bound`（《迭代器与算法》）就是这两个函数。

## 4. 两个陷阱

### 4.1 整数溢出

计算中点最直接的写法是 `mid = (lo + hi) / 2`。但如果 `lo` 和 `hi` 都很大，`lo + hi` 会超出 `int` 的范围。在数组中查找时，只有数组非常大（超过 10 亿个元素）才会遇到；但在下一节的「在答案上二分」中，范围常常就是 0 到 `INT_MAX`，很容易遇到：

```c
// 在答案上二分：最大的 x，使得 x * x <= target
int isqrtBad(int target)
{
    int lo = 0, hi = target;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;            // 错误：lo + hi 可能溢出
        if ((long long)mid * mid <= target) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}
```

计算 `isqrtBad(INT_MAX)`，用 `-fsanitize=undefined` 检查：

```
bs.c:43:28: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'
isqrtBad(INT_MAX) = 0
```

`0 + INT_MAX + 1` 溢出了，结果是错误的 0（正确答案是 46340）。有意思的是，同样的代码分别用 `-O0` 和 `-O2` 编译：

```
-O0: isqrtBad(INT_MAX) = 0
-O2: isqrtBad(INT_MAX) = 46340
```

开启优化后，结果「碰巧」是对的。这正是《数据类型与变量》中讲的**未定义行为**：结果取决于编译器怎么优化，这种 bug 可能在测试时一切正常，换一个编译选项就出错。

正确的写法是 `mid = lo + (hi - lo) / 2`：`hi - lo` 不会溢出，`(hi - lo) / 2` 加上 `lo` 也不会超过 `hi`。但要小心：向上取整时，`lo + (hi - lo + 1) / 2` 在 `hi - lo` 等于 `INT_MAX` 时，`+ 1` 同样会溢出。我第一次写这个例子时就犯了这个错误，是 UBSan 发现的。最稳妥的办法是用更大的类型计算：

```c
int isqrt(int target)
{
    int lo = 0, hi = target;
    while (lo < hi) {
        int mid = lo + (int)(((long long)hi - lo + 1) / 2);   // 向上取整；用 long long 计算，避免 hi - lo + 1 溢出
        if ((long long)mid * mid <= target) lo = mid;   // mid 满足条件，答案在 [mid, hi]
        else hi = mid - 1;                      // mid 太大，答案在 [lo, mid - 1]
    }
    return lo;
}
```

```
isqrt(10) = 3, isqrt(16) = 4, isqrt(INT_MAX) = 46340
```

### 4.2 死循环

为什么上面的 `isqrt` 要**向上取整**？如果用向下取整的 `mid = lo + (hi - lo) / 2`：

```c
int target = 10, lo = 0, hi = target, rounds = 0;
while (lo < hi && rounds < 20) {
    int mid = lo + (hi - lo) / 2;           // 向下取整
    if (mid * mid <= target) lo = mid;      // lo = mid：当 hi = lo + 1 时 mid == lo，区间不再缩小
    else hi = mid - 1;
    ++rounds;
    printf("lo = %d, hi = %d\n", lo, hi);
}
```

```
lo = 3, hi = 4
lo = 3, hi = 4
lo = 3, hi = 4
循环了 20 次
```

（加了 20 次的限制，否则就是死循环。）当 `lo = 3`、`hi = 4` 时，`mid = 3`，`3 * 3 <= 10` 成立，执行 `lo = mid`，lo 还是 3，区间没有缩小。

规则是：**如果更新时有 `lo = mid`，中点就要向上取整；如果有 `hi = mid`，就向下取整**。这样 mid 总是和被赋值的那一端不同，区间每次至少缩小 1。

## 5. 在答案上二分

二分查找不只能在数组中查找。只要问题满足**单调性**：「如果 x 满足条件，那么所有比 x 小（或大）的数都满足」，就可以在答案的范围内二分。

上面的 `isqrt` 就是一个例子：x * x <= target 对 x 是单调的，所以可以在 [0, target] 中二分，找满足条件的最大的 x，只需要约 31 次比较。

更实际的例子：

- 有一批货物和若干辆卡车，每辆卡车的载重是 C，最少要多大的 C 才能在 D 天内运完？C 越大越容易运完，满足单调性。在可能的载重范围内二分，每次用 O(n) 检查给定的 C 是否可行，总共 O(n log C)。
- 在一个只升不降的日志中，找到第一条出错的记录。

这种「**把求最优值的问题转化为判定问题**」的思路非常强大：判断「C 是否可行」往往比直接求「最小的 C」简单得多。

所有的程序都用 `-fsanitize=address,undefined` 检查过（除了故意演示溢出的 `isqrtBad`）。

## 6. 常见错误

1. **在无序的数组上二分查找**：结果没有意义。
2. **区间的定义和代码不一致**：比如用 `hi = n - 1`，却写 `while (lo < hi)`，会漏掉最后一个元素。
3. **`hi = mid` 写成 `hi = mid - 1`**：在边界查找中漏掉了答案。
4. **`mid = (lo + hi) / 2`**：溢出，结果可能「碰巧」正确，更难发现。
5. **`lo = mid` 时中点向下取整**：死循环。
6. **用标准二分查找求第一个或最后一个**：有重复元素时，返回的是任意一个。

## 7. 小结

- 二分查找要求数据有序，O(log n)；写对的关键是先确定区间的含义（闭区间或左闭右开），再让每一行代码符合它
- lowerBound 找第一个 >= key 的位置，upperBound 找第一个 > key 的位置；两者之差是 key 出现的次数，lowerBound 也是插入位置
- 中点用 `lo + (hi - lo) / 2`；实测 `(lo + hi) / 2` 溢出后，`-O0` 得到 0，`-O2` 碰巧得到正确的 46340
- 有 `lo = mid` 时中点要向上取整，否则会死循环
- 满足单调性的问题都可以在答案上二分

## 8. 练习

1. 用 lowerBound 实现：判断一个有序数组中是否存在 key。
2. 一个先递增后递减的数组（比如 `1 3 8 12 9 4 2`），用二分查找找出最大值的位置。
3. 有 n 堆香蕉，第 i 堆有 p[i] 根。每小时最多吃 k 根（一堆不足 k 根就吃完这堆，这一小时不再吃别的堆），要在 h 小时内吃完，k 最小是多少？说明二分的范围和判定方法。

<details>
<summary>点击查看答案</summary>

1. `int i = lowerBound(a, n, key); return i < n && a[i] == key;`。lowerBound 返回第一个 >= key 的位置，还要检查它是否在数组范围内、是否正好等于 key。

2. 比较 `a[mid]` 和 `a[mid + 1]`：如果 `a[mid] < a[mid + 1]`，说明 mid 在递增的部分，最大值在右边，`lo = mid + 1`；否则最大值在 mid 或它的左边，`hi = mid`。用 `[lo, hi]` 且 `lo < hi` 作为循环条件（这样 `mid + 1` 不会越界），结束时 `lo` 就是答案。对上面的数组，答案是下标 3（值 12）。

3. k 的范围是 [1, max(p)]：k = max(p) 时每堆一小时就能吃完，再大也没有用。判定：给定 k，需要的小时数是所有 ⌈p[i] / k⌉ 的和，小于等于 h 就可行。k 越大越容易满足，有单调性，所以二分找满足条件的最小 k。时间复杂度 O(n log max(p))。

</details>
