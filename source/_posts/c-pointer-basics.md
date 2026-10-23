---
title: 指针入门
date: 2026-10-16 10:00:00
updated: 2026-10-23 16:20:00
categories:
  - C/C++
  - C语言基础
tags:
  - C语言
  - 指针
order: 8
description: 指针是 C 语言的核心，也是初学者最害怕的部分。本文从「指针就是地址」讲起，介绍取地址和解引用，用指针实现真正的 swap 和多个返回值，并逐一复现空指针、野指针、返回局部变量地址三种危险的指针，以及 const 指针参数的用法。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解地址和指针的概念；会用 `&` 取地址、用 `*` 解引用；会通过指针参数修改调用者的变量、返回多个结果；认识空指针、野指针、悬空指针，知道如何避免；会用 `const` 修饰只读的指针参数。
>
> **前置知识**：《函数》、《数组》。
>
> **编译环境**：`gcc -std=c11 -Wall -Wextra`，64 位系统。

## 1. 地址和指针

内存可以看作一排连续的格子，每个格子是 1 个字节，每个格子都有一个编号，叫作**地址**。定义一个 `int` 变量，系统就为它分配 4 个连续的字节，第一个字节的地址就是这个变量的地址。

**指针**就是一个**保存地址的变量**。

```c
#include <stdio.h>

int main(void)
{
    int age = 18;
    int* p = &age;                  // p 是一个指针，保存 age 的地址

    printf("age 的值:      %d\n", age);
    printf("age 的地址:    %p\n", (void*)&age);
    printf("p 的值:        %p\n", (void*)p);
    printf("*p:            %d\n", *p);       // 通过指针读取 age
    printf("p 自己的地址:  %p\n", (void*)&p);

    *p = 20;                        // 通过指针修改 age
    printf("*p = 20 之后 age = %d\n", age);

    double d = 3.14;
    char c = 'A';
    double* pd = &d;
    char* pc = &c;
    printf("sizeof(p) = %zu, sizeof(pd) = %zu, sizeof(pc) = %zu\n", sizeof(p), sizeof(pd), sizeof(pc));
    printf("*pd = %.2f, *pc = %c\n", *pd, *pc);
    return 0;
}
```

运行结果：

```
age 的值:      18
age 的地址:    0x16b6a2558
p 的值:        0x16b6a2558
*p:            18
p 自己的地址:  0x16b6a2550
*p = 20 之后 age = 20
sizeof(p) = 8, sizeof(pd) = 8, sizeof(pc) = 8
*pd = 3.14, *pc = A
```

<figure class="diagram">
<svg viewBox="0 0 640 150" role="img" aria-label="指针示意图：指针变量 p 中保存着变量 age 的地址，通过 *p 可以访问 age">
  <defs>
    <marker id="ptr-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/>
    </marker>
  </defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="40" y="30" style="fill:var(--muted)">地址 0x16b6a2550</text>
    <rect x="40" y="40" width="200" height="56" rx="6" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="55" y="62" style="fill:var(--accent)">int* p</text>
    <text x="55" y="86" style="fill:var(--fg)">0x16b6a2558</text>
    <text x="400" y="30" style="fill:var(--muted)">地址 0x16b6a2558</text>
    <rect x="400" y="40" width="200" height="56" rx="6" style="fill:var(--panel-2);stroke:var(--warn)"/>
    <text x="415" y="62" style="fill:var(--warn)">int age</text>
    <text x="415" y="86" style="fill:var(--fg)">18</text>
    <line x1="240" y1="68" x2="398" y2="68" style="stroke:var(--fg-2)" marker-end="url(#ptr-arrow)"/>
    <text x="270" y="60" style="fill:var(--muted)">指向</text>
    <text x="40" y="128" style="fill:var(--muted)">p 的值 = &amp;age</text>
    <text x="400" y="128" style="fill:var(--muted)">*p 就是 age</text>
  </g>
</svg>
</figure>

- **`int* p`** 定义一个指针变量 `p`，它可以保存一个 `int` 变量的地址。`*` 表示「指针」，`int` 表示它**指向**的数据的类型。
- **`&age`**：取地址运算符，得到 `age` 的地址。
- **`*p`**：**解引用**运算符，访问 `p` 所指向的那个变量。`*p` 就是 `age`，读取 `*p` 就是读取 `age`，给 `*p` 赋值就是给 `age` 赋值。
- 指针本身也是一个变量，它也有自己的地址（`&p`）。
- 在 64 位系统上，**所有指针都占 8 个字节**，不管它指向什么类型。地址就是一个 64 位的数。

`*` 在不同的位置含义不同，这是初学者最容易混淆的地方：

| 出现的位置 | 含义 | 例子 |
|---|---|---|
| 定义变量时 | 「这是一个指针」 | `int* p;` |
| 表达式中，作为一元运算符 | 解引用，「p 指向的东西」 | `*p = 20;` |
| 表达式中，作为二元运算符 | 乘法 | `a * b` |

> `int* p` 和 `int *p` 是一样的，空格的位置不影响含义。但要注意 `int* p, q;` 定义的是一个指针 `p` 和一个**普通的 int** `q`，`*` 只作用于紧跟着它的变量名。所以**每行只定义一个指针**。

### 1.1 指针的类型

为什么指针要有类型？`int*` 和 `double*` 都是 8 字节的地址，有什么区别？区别在于**解引用时读取多少字节、怎样解释它们**：`*pd` 从地址开始读 8 个字节，按 `double` 解释；`*pc` 只读 1 个字节，按 `char` 解释。把一个 `int` 的地址赋给 `double*`，编译器会给出警告，因为用错误的类型去解读内存，得到的是垃圾值。

## 2. 指针作为函数参数

《函数》中的 `swap` 无法交换实参，因为参数是按值传递的。现在可以把**地址**传进去：

```c
#include <stdio.h>

void swap(int* a, int* b)           // 参数是两个地址
{
    int t = *a;
    *a = *b;
    *b = t;
}

int main(void)
{
    int x = 1, y = 2;
    swap(&x, &y);                   // 传入 x 和 y 的地址
    printf("swap 之后: x = %d, y = %d\n", x, y);
    return 0;
}
```

```
swap 之后: x = 2, y = 1
```

参数仍然是按值传递的，但这次被复制的是**地址**。`a` 中保存着 `x` 的地址，`*a` 就是 `main` 中的 `x`。通过地址，函数可以修改调用者的变量。

这也解释了为什么 `scanf("%d", &age)` 要写 `&`：`scanf` 需要把读到的数字**存到** `age` 中，所以要知道 `age` 的地址。

### 2.1 返回多个结果

函数只能有一个返回值。需要返回多个结果时，可以通过指针参数「带出来」：

```c
#include <stdio.h>

int divide(int a, int b, int* quotient, int* remainder)
{
    if (b == 0) {
        return 0;                   // 失败
    }
    *quotient = a / b;
    *remainder = a % b;
    return 1;                       // 成功
}

int main(void)
{
    int q, r;
    if (divide(17, 5, &q, &r)) {
        printf("17 / 5 = %d 余 %d\n", q, r);
    }
    if (!divide(1, 0, &q, &r)) {
        printf("除数不能为 0\n");
    }
    return 0;
}
```

```
17 / 5 = 3 余 2
除数不能为 0
```

这是 C 语言中非常常见的模式：**返回值表示成功或失败，真正的结果通过指针参数输出**。标准库中的 `scanf` 就是这样设计的。

### 2.2 const 指针参数

如果函数只需要读取指针指向的数据，不需要修改，就把参数声明为 **`const int*`**：

```c
void printScore(const int* score)   // 只读：承诺不修改
{
    printf("分数: %d\n", *score);
    *score = 100;                   // 试图修改
}
```

```
constp.c:6:12: error: read-only variable is not assignable
```

`const int* score` 的意思是「通过 `score` 不能修改它指向的 `int`」。这既能防止函数意外地修改数据，也让调用者从函数声明中就能看出这个参数是只读的。《数组》中的 `const int arr[]` 也是同样的道理。`const` 和指针还有其他的组合方式，下一篇再详细介绍。

## 3. 危险的指针

指针能直接访问内存，用错了后果很严重。下面三种情况最为常见。

### 3.1 空指针

**`NULL`** 是一个特殊的指针值（通常就是地址 0），表示「不指向任何东西」。函数经常用返回 `NULL` 表示「没有找到」或者「失败」：

```c
#include <stdio.h>

int* findFirstNegative(int arr[], int n)
{
    for (int i = 0; i < n; ++i) {
        if (arr[i] < 0) {
            return &arr[i];
        }
    }
    return NULL;                    // 没有找到
}

int main(void)
{
    int a[] = {3, 1, 4};
    int* p = findFirstNegative(a, 3);
    if (p != NULL) {
        printf("找到了 %d\n", *p);
    } else {
        printf("没有负数\n");
    }
    printf("不检查就解引用:\n");
    fflush(stdout);
    printf("%d\n", *p);
    return 0;
}
```

```
没有负数
不检查就解引用:
```

程序在最后一行崩溃了，退出码 139（段错误）。**解引用空指针**是未定义行为，在现代操作系统上通常会立即崩溃，因为地址 0 附近的内存是不允许访问的。用 AddressSanitizer 运行，它会指出崩溃的行号，并提示这是一个空地址：

```
ERROR: AddressSanitizer: SEGV on unknown address 0x000000000000
Hint: address points to the zero page.
    #0 0x000102a94b9c in main null.c:24
```

**规则：使用一个可能为 `NULL` 的指针之前，先检查它。** 《运算符与表达式》中提到的短路求值在这里很有用：`if (p != NULL && *p > 0)`。

### 3.2 野指针

```c
int* p;                         // 没有初始化：野指针
*p = 42;
```

```
wild.c:5:6: warning: variable 'p' is uninitialized when used here [-Wuninitialized]
```

运行时程序崩溃了，退出码 138（总线错误）。`p` 没有初始化，它的值是一个随机的地址，向这个地址写入数据，可能导致崩溃，也可能**悄悄改写某个无关的变量**，后者更加可怕。

**规则：定义指针时就初始化。还不知道它指向哪里时，初始化为 `NULL`**：`int* p = NULL;`。这样即使误用，也会稳定地崩溃在第一次解引用的地方，而不是悄悄地破坏数据。

### 3.3 返回局部变量的地址

```c
#include <stdio.h>

int* makeNumber(void)
{
    int n = 42;
    return &n;                      // 返回局部变量的地址
}

void other(void)
{
    int junk[4] = {7, 7, 7, 7};     // 占用同一块栈空间
    (void)junk;
}

int main(void)
{
    int* p = makeNumber();
    printf("第一次读取: %d\n", *p);
    other();
    printf("调用 other 之后: %d\n", *p);
    return 0;
}
```

```
dangling.c:6:13: warning: address of stack memory associated with local variable 'n' returned [-Wreturn-stack-address]
第一次读取: 42
调用 other 之后: 1
```

第一次读取「碰巧」得到了 42，调用另一个函数之后，值就变了。《函数》中讲过，局部变量在函数返回时就被销毁了，它占用的栈空间会被之后的函数调用重复使用。`p` 指向的是一块**已经不属于 `n` 的内存**，这样的指针叫作**悬空指针**（dangling pointer）。

这个错误特别危险，因为它**有时候看起来能正常工作**。AddressSanitizer 需要打开 `detect_stack_use_after_return` 选项才能发现它：

```bash
ASAN_OPTIONS=detect_stack_use_after_return=1 ./dangling
```

```
ERROR: AddressSanitizer: stack-use-after-return on address 0x00010729d020
READ of size 4 at 0x00010729d020 thread T0
    #0 0x000104fe0a84 in main dangling.c:18
```

**规则：不要返回局部变量的地址。** 需要在函数中创建数据并交给调用者时，可以让调用者传入一个指针用来存放结果（像第 2.1 节那样），或者使用第 1.11 篇介绍的动态内存。

## 4. 常见错误

1. **混淆 `*` 在定义和表达式中的含义**：`int* p = &a;` 是定义并初始化 `p`，不是给 `*p` 赋值。
2. **`int* p, q;` 以为定义了两个指针**：`q` 是 `int`。
3. **调用需要地址的函数时忘了 `&`**：`swap(x, y)`、`scanf("%d", x)`。
4. **解引用 `NULL`**：使用前检查。
5. **使用未初始化的指针**：定义时初始化为 `NULL` 或者一个有效的地址。
6. **返回局部变量的地址**：函数返回后那块内存就失效了。
7. **只读的参数没有加 `const`**：失去了编译器的保护。

## 5. 小结

- 地址是内存中每个字节的编号；指针是保存地址的变量，64 位系统上占 8 字节
- `&x` 取 `x` 的地址，`*p` 访问 `p` 指向的变量；指针的类型决定了解引用时如何解释内存
- 把地址传给函数，函数就能修改调用者的变量；常用指针参数输出多个结果
- 只读的指针参数用 `const int*`
- 解引用空指针会崩溃；未初始化的野指针和返回局部变量地址产生的悬空指针会读写错误的内存，而且不一定立即暴露
- 定义指针时就初始化，使用可能为 `NULL` 的指针前先检查

## 6. 练习

1. 写一个函数 `int minMax(const int arr[], size_t n, int* min, int* max)`，通过指针参数同时返回数组的最小值和最大值；数组为空时返回 0，否则返回 1。
2. 下面的程序输出什么？

   ```c
   int x = 10, y = 20;
   int* p = &x;
   int* q = &y;
   *p = *q;
   p = q;
   *p = 30;
   printf("%d %d\n", x, y);
   ```

3. 为什么 `scanf("%d", &n)` 要写 `&`，而《输入与输出》中 `scanf("%7s", name)` 读取字符数组时不用写 `&`？

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```c
   int minMax(const int arr[], size_t n, int* min, int* max)
   {
       if (n == 0) {
           return 0;
       }
       *min = *max = arr[0];
       for (size_t i = 1; i < n; ++i) {
           if (arr[i] < *min) *min = arr[i];
           if (arr[i] > *max) *max = arr[i];
       }
       return 1;
   }
   ```

   对 `{5, 2, 9, 1, 7}` 输出 `最小 1，最大 9`。

2. 输出 `20 30`。逐行分析：`*p = *q` 把 `y` 的值 20 赋给 `x`（`p` 指向 `x`）；`p = q` 让 `p` 改为指向 `y`，**没有修改任何 `int`**；`*p = 30` 修改的是 `y`。要分清「修改指针本身」（`p = q`）和「修改指针指向的数据」（`*p = ...`）。

3. `scanf` 需要的是存放数据的**地址**。`n` 是一个 `int`，要用 `&n` 取得它的地址；而《数组》中讲过，数组名在作为函数参数时会自动转换为首元素的地址，`name` 本身就已经是地址了。下一篇会详细讲数组和指针的关系。

</details>
