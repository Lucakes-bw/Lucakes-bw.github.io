---
title: 数组
date: 2026-10-23 16:00:00
updated: 2026-10-23 16:00:00
categories:
  - C/C++
  - C语言基础
tags:
  - C语言
  - 数组
order: 7
description: 数组是一组相同类型、连续存放的元素。本文介绍数组的定义、初始化和遍历，实测越界写入悄悄改写相邻数组的数据并用 AddressSanitizer 定位，解释数组作为函数参数时退化成指针导致的 sizeof 陷阱，以及二维数组在内存中的布局。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会定义、初始化和遍历数组；理解数组在内存中是连续存放的；认识越界访问的危害，会用 AddressSanitizer 发现它；会把数组传给函数并同时传递长度；会使用二维数组。
>
> **前置知识**：《函数》。
>
> **编译环境**：`gcc -std=c11 -Wall -Wextra`。

## 1. 定义和初始化

需要存放 5 个学生的成绩时，不必定义 5 个变量，可以定义一个**数组**：

```c
#include <stdio.h>

int main(void)
{
    int scores[5] = {90, 85, 78, 92, 66};     // 5 个 int
    int partial[5] = {1, 2};                  // 没有给出的元素自动为 0
    int zeros[100] = {0};                     // 全部初始化为 0
    int autoSize[] = {3, 1, 4, 1, 5, 9};      // 长度由初始值的个数决定

    size_t n = sizeof(scores) / sizeof(scores[0]);
    int sum = 0, max = scores[0];
    for (size_t i = 0; i < n; ++i) {
        sum += scores[i];
        if (scores[i] > max) {
            max = scores[i];
        }
    }
    printf("共 %zu 个分数，总分 %d，平均 %.1f，最高 %d\n", n, sum, (double)sum / n, max);

    printf("partial:");
    for (int i = 0; i < 5; ++i) printf(" %d", partial[i]);
    printf("\nzeros[99] = %d\n", zeros[99]);
    printf("sizeof(autoSize) = %zu，元素个数 %zu\n", sizeof(autoSize), sizeof(autoSize) / sizeof(autoSize[0]));

    printf("元素的地址:");
    for (int i = 0; i < 3; ++i) printf(" %p", (void*)&scores[i]);
    printf("\n");
    return 0;
}
```

运行结果：

```
共 5 个分数，总分 411，平均 82.2，最高 92
partial: 1 2 0 0 0
zeros[99] = 0
sizeof(autoSize) = 24，元素个数 6
元素的地址: 0x16b4f6530 0x16b4f6534 0x16b4f6538
```

- `int scores[5]` 定义了一个包含 5 个 `int` 的数组。**长度必须是常量**（C99 允许用变量作为长度，叫变长数组，但它有很多限制，C11 起变成了可选的功能，不建议使用）。
- **下标从 0 开始**：5 个元素是 `scores[0]` 到 `scores[4]`，**没有 `scores[5]`**。
- **初始化时只给出了一部分值，其余的元素自动为 0**。所以 `int zeros[100] = {0};` 是把数组全部清零的常用写法。但如果完全不初始化（`int a[100];`），函数中的数组元素都是不确定的值。
- **`sizeof(数组) / sizeof(数组[0])`** 计算数组的元素个数：`autoSize` 占 24 字节，每个元素 4 字节，所以有 6 个元素。
- 元素的地址每次增加 4，正好是一个 `int` 的大小：**数组的元素在内存中是连续存放的**。这也是为什么可以用下标快速访问任意一个元素：`scores[i]` 的地址就是「首元素的地址 + i × 4」。

## 2. 越界访问

**C 语言不检查数组下标是否越界**。

### 2.1 越界读取

```c
int arr[3] = {1, 2, 3};
printf("arr[3] = %d（越界读取）\n", arr[3]);
```

下标是常量时，编译器能发现：

```
oob.c:9:47: warning: array index 3 is past the end of the array (that has type 'int[3]') [-Warray-bounds]
```

但程序仍然能编译和运行。分别用 `-O0` 和 `-O2` 编译，读到的值是：

```
-O0: arr[3] = 1（越界读取）
-O2: arr[3] = -270991144（越界读取）
```

读到的是数组后面那块内存中碰巧存放的内容，没有任何意义。

### 2.2 越界写入

越界写入更危险，它会**改写其他变量的数据**：

```c
#include <stdio.h>

int main(void)
{
    int limit[4] = {100, 100, 100, 100};
    int data[4] = {1, 2, 3, 4};

    for (int i = 0; i <= 4; ++i) {         // <= 写错了，多写了一个
        data[i] = 0;
    }
    printf("data:  %d %d %d %d\n", data[0], data[1], data[2], data[3]);
    printf("limit: %d %d %d %d\n", limit[0], limit[1], limit[2], limit[3]);
    printf("&data[4] = %p, &limit[0] = %p\n", (void*)&data[4], (void*)&limit[0]);
    return 0;
}
```

用 `-Wall -Wextra` 编译**没有任何警告**（下标是变量，编译器无法确定它的范围），运行结果：

```
data:  0 0 0 0
limit: 0 100 100 100
&data[4] = 0x16d95e550, &limit[0] = 0x16d95e550
```

我们从来没有修改过 `limit`，但 `limit[0]` 从 100 变成了 0。最后一行揭示了原因：在这台机器上，`data[4]` 的地址**恰好就是** `limit[0]` 的地址，两个数组在栈上紧挨着。循环条件多写了一个 `=`，就悄悄地改写了另一个数组。

想象一下，如果 `limit` 是一个用于安全检查的限额，程序就在不知不觉中被绕过了检查。而且这种错误**不一定**表现出来：变量的布局随编译器、优化级别而变化，换一个环境，被改写的可能是别的数据，或者什么都没发生。这类 bug 极难排查。

**AddressSanitizer** 能立即发现它：

```bash
gcc -std=c11 -g -fsanitize=address oob2.c -o oob2 && ./oob2
```

```
ERROR: AddressSanitizer: stack-buffer-overflow on address 0x00016dd22490
WRITE of size 4 at 0x00016dd22490 thread T0
    #0 0x0001020dc9dc in main oob2.c:9
```

第 9 行 `data[i] = 0;` 发生了栈缓冲区溢出。对上一节的越界读取，ASan 的报告更加详细，直接指出了被越界访问的是哪个变量：

```
ERROR: AddressSanitizer: stack-buffer-overflow on address 0x00016f6e645c
READ of size 4 at 0x00016f6e645c thread T0
    #0 0x0001007189d8 in main oob.c:9
Address 0x00016f6e645c is located in stack of thread T0 at offset 60 in frame
    [48, 60) 'arr' (line 6) <== Memory access at offset 60 overflows this variable
```

**规则：遍历数组统一写成 `for (i = 0; i < n; ++i)`；写练习时打开 `-fsanitize=address`。**

## 3. 数组作为函数参数

```c
#include <stdio.h>

void printSize(int arr[])
{
    printf("函数内 sizeof(arr) = %zu\n", sizeof(arr));
}

double average(const int arr[], size_t n)   // 同时传入长度
{
    int sum = 0;
    for (size_t i = 0; i < n; ++i) {
        sum += arr[i];
    }
    return n > 0 ? (double)sum / n : 0.0;
}

void doubleAll(int arr[], size_t n)          // 函数可以修改数组的元素
{
    for (size_t i = 0; i < n; ++i) {
        arr[i] *= 2;
    }
}

int main(void)
{
    int scores[5] = {90, 85, 78, 92, 66};
    printf("main 中 sizeof(scores) = %zu\n", sizeof(scores));
    printSize(scores);
    printf("平均分 %.1f\n", average(scores, 5));
    doubleAll(scores, 5);
    printf("doubleAll 之后 scores[0] = %d\n", scores[0]);
    return 0;
}
```

编译时的警告：

```
param.c:5:51: warning: sizeof on array function parameter will return size of 'int *' instead of 'int[]' [-Wsizeof-array-argument]
```

运行结果：

```
main 中 sizeof(scores) = 20
函数内 sizeof(arr) = 8
平均分 82.2
doubleAll 之后 scores[0] = 180
```

两个重要的现象：

1. **在函数中，`sizeof(arr)` 是 8，而不是 20**。数组作为参数传递时，传过去的不是整个数组，而是**首元素的地址**，形参 `int arr[]` 实际上是一个**指针**（`int*`），在 64 位系统上占 8 个字节。这叫**数组退化为指针**。所以在函数中**无法得到数组的长度**，必须由调用者通过另一个参数传进来，就像 `average(scores, 5)` 那样。
2. **函数可以修改调用者的数组**。这和上一篇的「按值传递」并不矛盾：被复制的是地址，函数通过这个地址访问的是**同一个数组**。`doubleAll` 修改的就是 `main` 中的 `scores`。

如果函数不应该修改数组，就把参数声明为 **`const int arr[]`**，像 `average` 那样。这样函数中意外地修改数组时，编译器会报错，调用者也能从声明中看出「这个函数不会改我的数组」。

指针和数组的关系会在第 1.9 篇详细讲。

## 4. 数组的限制

```c
int a[3] = {1, 2, 3};
int b[3];
b = a;                   // 想把 a 复制给 b
```

```
assign.c:5:7: error: array type 'int[3]' is not assignable
```

**数组不能整体赋值**，也不能用 `==` 比较：

```c
int a[3] = {1, 2, 3};
int b[3] = {1, 2, 3};
printf("a == b: %d\n", a == b);
```

```
warning: array comparison always evaluates to false [-Wtautological-compare]
a == b: 0
```

内容完全相同，比较的结果却是假，因为 `a == b` 比较的是两个数组的**地址**。复制和比较数组，要逐个元素地用循环处理，或者使用 `<string.h>` 中的 `memcpy` 和 `memcmp`：

```c
int c[3];
memcpy(c, a, sizeof(a));                         // 复制 sizeof(a) 个字节
printf("memcmp: %d\n", memcmp(a, b, sizeof(a)));  // 0 表示内容相同
```

```
memcmp: 0
c = 1 2 3
```

## 5. 二维数组

```c
#include <stdio.h>

void printMatrix(int rows, int m[][3])         // 第二维的大小必须写出来
{
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < 3; ++c) {
            printf("%4d", m[r][c]);
        }
        printf("\n");
    }
}

int main(void)
{
    int m[2][3] = {
        {1, 2, 3},
        {4, 5, 6},
    };
    printMatrix(2, m);
    printf("sizeof(m) = %zu，sizeof(m[0]) = %zu\n", sizeof(m), sizeof(m[0]));
    printf("按地址顺序:");
    int* p = &m[0][0];
    for (int i = 0; i < 6; ++i) {
        printf(" %d", p[i]);
    }
    printf("\n&m[0][2] = %p, &m[1][0] = %p\n", (void*)&m[0][2], (void*)&m[1][0]);
    return 0;
}
```

运行结果：

```
   1   2   3
   4   5   6
sizeof(m) = 24，sizeof(m[0]) = 12
按地址顺序: 1 2 3 4 5 6
&m[0][2] = 0x16bcea558, &m[1][0] = 0x16bcea55c
```

- `int m[2][3]` 是一个 2 行 3 列的数组，`m[r][c]` 访问第 r 行第 c 列。
- 二维数组在内存中仍然是**一整块连续的空间**，按行依次存放：先是第 0 行的 3 个元素，紧接着是第 1 行。`m[0][2]` 和 `m[1][0]` 的地址只差 4 个字节。
- `m[0]` 本身是一个包含 3 个 `int` 的一维数组，占 12 字节。
- 把二维数组传给函数时，**第二维的大小必须写出来**（`int m[][3]`），因为编译器要靠它计算 `m[r][c]` 的地址：首地址 + (r × 3 + c) × 4。

遍历二维数组时，**让内层循环遍历列**（最后一维），这样访问的内存是连续的，速度最快。用一个 4096 × 4096 的 `int` 数组（64 MB）测试，`-O2` 编译：

```c
for (int r = 0; r < N; ++r)               // 按行遍历：内层循环遍历列
    for (int c = 0; c < N; ++c)
        sum += m[r][c];

for (int c = 0; c < N; ++c)               // 按列遍历：内层循环遍历行
    for (int r = 0; r < N; ++r)
        sum += m[r][c];
```

```
按行: 6.5 ms，按列: 69.3 ms
```

同样是读取 1600 多万个元素，按列遍历慢了 10 倍。按列遍历时，相邻两次访问相隔 4096 × 4 = 16 KB，CPU 每次从内存读入缓存的一整块数据只用到了一个元素。原因和《STL 容器》中 `vector` 与 `list` 的遍历速度差异一样，都是 CPU 缓存。（这个数组定义成了 `static`，放在静态存储区而不是栈上，否则 64 MB 会直接导致栈溢出。）

## 6. 常见错误

1. **下标从 1 开始，或者访问 `arr[n]`**：有效的下标是 0 到 n-1。
2. **越界写入**：悄悄改写其他变量，实测改掉了相邻数组的数据，而且没有警告。打开 `-fsanitize=address`。
3. **在函数中用 `sizeof` 求数组参数的长度**：得到的是指针的大小。要把长度作为参数传入。
4. **使用未初始化的数组**：局部数组的元素是不确定的值。用 `= {0}` 初始化。
5. **用 `=` 复制数组、用 `==` 比较数组**：前者编译错误，后者比较的是地址。
6. **二维数组参数省略第二维的大小**：编译错误。
7. **在函数中定义很大的局部数组**：栈溢出，见《函数》第 3.2 节。

## 7. 小结

- 数组是一组相同类型、连续存放的元素，下标从 0 到 n-1
- 部分初始化时其余元素为 0；`sizeof(a) / sizeof(a[0])` 求元素个数（只在定义数组的地方有效）
- C 语言不检查越界；越界写入会改写相邻的数据，用 AddressSanitizer 检查
- 数组传给函数时退化为指向首元素的指针：函数中 `sizeof` 得到的是指针大小，长度要另外传入；函数可以修改数组，不想被修改就加 `const`
- 数组不能整体赋值或比较，使用循环或 `memcpy` / `memcmp`
- 二维数组按行连续存放，传参时要写出第二维的大小

## 8. 练习

1. 写一个函数 `int indexOf(const int arr[], size_t n, int target)`，返回 `target` 在数组中第一次出现的下标，找不到时返回 -1。
2. 实现冒泡排序 `void bubbleSort(int arr[], size_t n)`：反复比较相邻的两个元素，如果顺序不对就交换，直到整个数组有序。用 `{5, 2, 9, 1, 7}` 测试。
3. 有人想写一个计算数组长度的函数：

   ```c
   size_t length(int arr[]) { return sizeof(arr) / sizeof(arr[0]); }
   ```

   对一个有 5 个元素的 `int` 数组，它返回多少？为什么？

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```c
   int indexOf(const int arr[], size_t n, int target)
   {
       for (size_t i = 0; i < n; ++i) {
           if (arr[i] == target) {
               return (int)i;
           }
       }
       return -1;
   }
   ```

   对 `{5, 2, 9, 1, 7}`，`indexOf(a, n, 9)` 返回 2，`indexOf(a, n, 4)` 返回 -1。

2. 参考代码：

   ```c
   void bubbleSort(int arr[], size_t n)
   {
       for (size_t pass = 0; pass + 1 < n; ++pass) {
           int swapped = 0;
           for (size_t i = 0; i + 1 < n - pass; ++i) {
               if (arr[i] > arr[i + 1]) {
                   int t = arr[i];
                   arr[i] = arr[i + 1];
                   arr[i + 1] = t;
                   swapped = 1;
               }
           }
           if (!swapped) {
               break;                // 这一轮没有交换，说明已经有序
           }
       }
   }
   ```

   输出 `1 2 5 7 9`。每一轮把最大的元素「冒泡」到最后，所以第 `pass` 轮只需要比较前 `n - pass` 个元素。注意条件写成 `pass + 1 < n` 而不是 `pass < n - 1`：`n` 是无符号的 `size_t`，当 `n` 为 0 时，`n - 1` 会回绕成一个巨大的数。用 `-fsanitize=address,undefined` 测试过，空数组也能正确处理。排序算法在数据结构专栏中还会详细讲。

3. 返回 2（在 64 位系统上）。`arr` 是一个指针，`sizeof(arr)` 是 8；`arr[0]` 是 `int`，`sizeof(arr[0])` 是 4。8 / 4 = 2。编译器会给出 `-Wsizeof-array-argument` 警告。这样的函数是写不出来的，长度只能由调用者传入。

</details>
