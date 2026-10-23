---
title: 指针进阶
date: 2026-10-23 16:40:00
updated: 2026-10-23 16:40:00
categories:
  - C/C++
  - C语言基础
tags:
  - C语言
  - 指针
  - 函数指针
order: 9
description: 在指针入门的基础上，讲清楚指针运算、指针与数组的关系、二级指针、const 的三种组合和函数指针，用 qsort 演示回调函数，并实测「return a - b」这种常见比较函数写法在整数溢出时把数组排错的问题。
disableNunjucks: true
---

> **本文目标**：理解指针加减整数和指针相减的含义；理解 `a[i]` 和 `*(a + i)` 的等价关系，分清数组和指针的区别；会用二级指针修改调用者的指针；分清 `const int*` 和 `int* const`；会定义和使用函数指针，会用 `qsort` 排序任意类型的数组。
>
> **前置知识**：《数组》、《指针入门》。
>
> **编译环境**：`gcc -std=c11 -Wall -Wextra`，64 位系统。

## 1. 指针运算

```c
#include <stddef.h>
#include <stdio.h>

int main(void)
{
    int a[5] = {10, 20, 30, 40, 50};
    int* p = a;                              // 数组名转换为首元素的地址，等价于 &a[0]
    double d[3] = {1.5, 2.5, 3.5};
    double* q = d;

    printf("p     = %p, p + 1 = %p（差 %zu 字节）\n", (void*)p, (void*)(p + 1), sizeof(int));
    printf("q     = %p, q + 1 = %p（差 %zu 字节）\n", (void*)q, (void*)(q + 1), sizeof(double));
    printf("*(p + 2) = %d, p[2] = %d, a[2] = %d, 2[a] = %d\n", *(p + 2), p[2], a[2], 2[a]);

    int* end = a + 5;                        // 指向最后一个元素的后面
    printf("遍历:");
    for (int* it = a; it != end; ++it) {
        printf(" %d", *it);
    }
    ptrdiff_t n = end - a;                   // 两个指针相减：相隔几个元素
    printf("\nend - a = %td\n", n);
    return 0;
}
```

运行结果：

```
p     = 0x16f136540, p + 1 = 0x16f136544（差 4 字节）
q     = 0x16f136520, q + 1 = 0x16f136528（差 8 字节）
*(p + 2) = 30, p[2] = 30, a[2] = 30, 2[a] = 30
遍历: 10 20 30 40 50
end - a = 5
```

- **指针加 1，不是地址加 1，而是移动到下一个元素**：`int*` 加 1，地址增加 4；`double*` 加 1，地址增加 8。编译器根据指针的类型自动乘以元素的大小。这是指针必须有类型的另一个原因。
- **`p[i]` 就是 `*(p + i)`**。C 语言的下标运算其实就是指针运算的简写。甚至 `2[a]` 也能编译，因为它等于 `*(2 + a)`。（这只是一个说明原理的冷知识，不要在真实代码中这样写。）
- **两个指针相减**，得到它们之间相隔的**元素个数**（不是字节数），类型是 `ptrdiff_t`，用 `%td` 输出。只有指向同一个数组的两个指针相减才有意义。
- 可以用一对指针 `[begin, end)` 表示一个范围，`end` 指向最后一个元素的**后面**。这正是 C++ STL 中迭代器的原型（《迭代器与算法》）。C 标准允许指针指向数组末尾的下一个位置，但**不能解引用它**。

指针运算越过数组的边界（除了末尾的下一个位置）是未定义行为，和下标越界一样危险。

## 2. 数组和指针

数组名在大多数表达式中会**自动转换为指向首元素的指针**，所以 `int* p = a;` 可以直接写，数组传给函数时也是传递的这个指针。但**数组不是指针**，两者有本质的区别：

```c
#include <stdio.h>

void show(int* p)
{
    printf("  函数中 sizeof(p) = %zu\n", sizeof(p));
}

int main(void)
{
    int a[5] = {1, 2, 3, 4, 5};
    int* p = a;
    printf("sizeof(a) = %zu, sizeof(p) = %zu\n", sizeof(a), sizeof(p));
    printf("a = %p, &a = %p, a + 1 = %p, &a + 1 = %p\n",
           (void*)a, (void*)&a, (void*)(a + 1), (void*)(&a + 1));
    show(a);
    p = p + 1;              // 可以：指针是变量
    printf("*p = %d\n", *p);
    return 0;
}
```

```
sizeof(a) = 20, sizeof(p) = 8
a = 0x16d566550, &a = 0x16d566550, a + 1 = 0x16d566554, &a + 1 = 0x16d566564
  函数中 sizeof(p) = 8
*p = 2
```

| | 数组 `int a[5]` | 指针 `int* p` |
|---|---|---|
| 是什么 | 5 个连续的 `int`，共 20 字节 | 一个保存地址的变量，8 字节 |
| `sizeof` | 20 | 8 |
| 能否改变 | `a = p;` 编译错误：`array type 'int[5]' is not assignable` | `p = p + 1;` 可以 |
| `&` 的结果 | `&a` 是「指向整个数组的指针」，`&a + 1` 跳过整个数组（20 字节） | `&p` 是指针变量自己的地址 |

注意 `a` 和 `&a` 的值相同，但类型不同：`a + 1` 前进 4 字节（一个 `int`），`&a + 1` 前进 20 字节（一整个数组）。

**数组名转换为指针**的规则有三个例外：作为 `sizeof` 的操作数、作为 `&` 的操作数、以及用字符串字面量初始化字符数组时。这就是为什么 `sizeof(a)` 能得到整个数组的大小，而传到函数中就不行了。

## 3. 二级指针

指针也是变量，所以也可以有**指向指针的指针**，叫作二级指针：

```c
int x = 5;
int* p = &x;
int** pp = &p;
printf("x = %d, *p = %d, **pp = %d\n", x, *p, **pp);
```

```
x = 5, *p = 5, **pp = 5
```

二级指针最常见的用途是：**让函数修改调用者的一个指针变量**。道理和《指针入门》中的 `swap` 一样：要修改一个 `int`，传 `int*`；要修改一个 `int*`，就传 `int**`。

```c
#include <stdio.h>

int findMax(int arr[], int n, int** result)      // 通过二级指针返回一个指针
{
    if (n <= 0) {
        *result = NULL;
        return 0;
    }
    int* best = &arr[0];
    for (int i = 1; i < n; ++i) {
        if (arr[i] > *best) {
            best = &arr[i];
        }
    }
    *result = best;                              // 修改调用者的指针变量
    return 1;
}

int main(void)
{
    int a[] = {3, 9, 4, 1};
    int* maxPtr = NULL;
    if (findMax(a, 4, &maxPtr)) {
        printf("最大值 %d，下标 %td\n", *maxPtr, maxPtr - a);
        *maxPtr = 0;                             // 通过返回的指针修改数组
        printf("a = %d %d %d %d\n", a[0], a[1], a[2], a[3]);
    }
    return 0;
}
```

```
最大值 9，下标 1
a = 3 0 4 1
```

`findMax` 不仅找到了最大值，还给出了它的**位置**：调用者既可以读它，也可以通过它修改数组中的那个元素。指针相减 `maxPtr - a` 得到了下标。

二级指针的另一个常见用途是**指针数组**，比如 `main` 函数的完整形式 `int main(int argc, char* argv[])`，`argv` 是一个字符串（`char*`）的数组，在函数参数中它就是 `char**`。这会在下一篇字符串中用到。

## 4. const 和指针

`const` 放在不同的位置，含义不同：

```c
int x = 1, y = 2;
const int* p1 = &x;       // 指向 const int：不能通过 p1 修改 x
int* const p2 = &x;       // const 指针：p2 本身不能改指向
const int* const p3 = &x; // 两者都不能改

*p1 = 10;
p1 = &y;
*p2 = 10;
p2 = &y;
*p3 = 10;
p3 = &y;
```

```
constptr.c:8:9: error: read-only variable is not assignable
constptr.c:11:8: error: cannot assign to variable 'p2' with const-qualified type 'int *const'
constptr.c:12:9: error: read-only variable is not assignable
constptr.c:13:8: error: cannot assign to variable 'p3' with const-qualified type 'const int *const'
```

第 9 行 `p1 = &y` 和第 10 行 `*p2 = 10` 没有报错，其余四行都是错误。

| 写法 | 能否修改指向的数据 `*p = ...` | 能否改变指向 `p = ...` | 用途 |
|---|---|---|---|
| `const int* p` | 不能 | 能 | **最常用**：只读的指针参数 |
| `int* const p` | 能 | 不能 | 较少用 |
| `const int* const p` | 不能 | 不能 | 较少用 |

记忆方法：**从右往左读**。`const int* p`：p 是一个指针，指向 `const int`；`int* const p`：p 是一个 `const` 的指针，指向 `int`。（`int const* p` 和 `const int* p` 完全一样。）

## 5. 函数指针

函数在内存中也有地址，可以用一个指针保存它，再通过这个指针调用函数：

```c
#include <stdio.h>

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
int mul(int a, int b) { return a * b; }

int apply(int (*op)(int, int), int a, int b)     // 参数是一个函数指针
{
    return op(a, b);
}

int main(void)
{
    int (*f)(int, int) = add;                    // f 指向 add
    printf("f(3, 4) = %d\n", f(3, 4));
    f = mul;
    printf("f(3, 4) = %d\n", f(3, 4));

    printf("apply(sub, 10, 3) = %d\n", apply(sub, 10, 3));

    int (*ops[3])(int, int) = {add, sub, mul};   // 函数指针数组
    const char* names = "+-*";
    for (int i = 0; i < 3; ++i) {
        printf("6 %c 2 = %d\n", names[i], ops[i](6, 2));
    }
    return 0;
}
```

运行结果：

```
f(3, 4) = 7
f(3, 4) = 12
apply(sub, 10, 3) = 7
6 + 2 = 8
6 - 2 = 4
6 * 2 = 12
```

- **`int (*f)(int, int)`** 定义了一个函数指针 `f`，它可以指向「接受两个 `int`、返回 `int`」的函数。**括号不能省略**：`int *f(int, int)` 是声明一个返回 `int*` 的函数。
- 函数名本身就会转换为函数的地址，所以 `f = add;` 不需要写 `&`；调用时 `f(3, 4)` 也不需要写 `(*f)(3, 4)`（两种写法都可以）。
- 函数指针的类型写起来很长，通常用 `typedef` 起一个别名：`typedef int (*BinaryOp)(int, int);`，之后就可以写 `BinaryOp f = add;`。

函数指针的核心用途是**回调**：把「做什么」作为参数传给另一个函数。`apply` 不知道也不关心具体是加法还是减法，由调用者决定。

### 5.1 qsort：通用的排序函数

标准库 `<stdlib.h>` 中的 `qsort` 可以排序**任意类型**的数组，它是回调函数最典型的例子：

```c
void qsort(void* base, size_t count, size_t size, int (*compare)(const void*, const void*));
```

- `base`、`count`、`size`：数组的地址、元素个数、每个元素的大小。`qsort` 不知道元素的类型，所以用 **`void*`**（可以指向任何类型的「通用指针」）接收地址。
- `compare`：**由我们提供的比较函数**。`qsort` 每次需要比较两个元素时，就把它们的地址传给这个函数。比较函数返回负数表示第一个应该排在前面，返回正数表示第二个应该排在前面，返回 0 表示相等。

```c
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    const char* name;
    int score;
} Student;

int compareInt(const void* a, const void* b)
{
    int x = *(const int*)a;                      // void* 转换为真正的类型
    int y = *(const int*)b;
    return (x > y) - (x < y);                    // 返回负数、0 或正数
}

int byScoreDesc(const void* a, const void* b)
{
    const Student* s1 = a;
    const Student* s2 = b;
    return (s2->score > s1->score) - (s2->score < s1->score);
}

int main(void)
{
    int nums[] = {42, -7, 19, 0, 88, 3};
    size_t n = sizeof(nums) / sizeof(nums[0]);
    qsort(nums, n, sizeof(nums[0]), compareInt);
    for (size_t i = 0; i < n; ++i) printf("%d ", nums[i]);
    printf("\n");

    Student s[] = {{"Alice", 85}, {"Bob", 92}, {"Carol", 78}};
    qsort(s, 3, sizeof(Student), byScoreDesc);
    for (int i = 0; i < 3; ++i) printf("%s %d\n", s[i].name, s[i].score);
    return 0;
}
```

```
-7 0 3 19 42 88 
Bob 92
Alice 85
Carol 78
```

（`Student` 是一个结构体，`s1->score` 访问它的成员，第 1.12 篇会详细介绍。这里只需要知道：同一个 `qsort`，换一个比较函数，就能按照完全不同的规则排序完全不同的数据。）

`(x > y) - (x < y)` 是一个巧妙的写法：`x > y` 时结果是 1 - 0 = 1，`x < y` 时是 0 - 1 = -1，相等时是 0。

### 5.2 陷阱：用减法写比较函数

很多教程中的比较函数是这样写的：

```c
int compareBad(const void* a, const void* b)
{
    return *(const int*)a - *(const int*)b;      // 常见写法，但可能溢出
}
```

它看起来很简洁，大多数时候也能正常工作。但对包含很大或很小的数的数组：

```c
int a[] = {INT_MAX, -1, INT_MIN, 0, 1};
```

```
a - b 写法:  2147483647 -2147483648 -1 0 1 
比较写法:    -2147483648 -1 0 1 2147483647 
```

用减法的版本排错了：`INT_MAX` 被排在了最前面。原因是 `INT_MAX - (-1)` 溢出了，结果变成一个负数，于是 `qsort` 认为 `INT_MAX` 比 -1 小。用 `-fsanitize=undefined` 可以发现：

```
qsort_bad.c:7:27: runtime error: signed integer overflow: 2147483647 - -1 cannot be represented in type 'int'
```

**规则：比较函数用比较运算，不要用减法。**

## 6. 常见错误

1. **以为指针加 1 是地址加 1**：实际是前进一个元素。
2. **指针运算越过数组边界**：和下标越界一样是未定义行为。
3. **以为数组就是指针**：`sizeof` 不同，数组名不能赋值。
4. **想在函数中修改调用者的指针，却只传了一级指针**：修改的是副本。要传二级指针。
5. **分不清 `const int*` 和 `int* const`**：从右往左读。
6. **函数指针声明漏了括号**：`int *f(int, int)` 是函数声明。
7. **`qsort` 比较函数中把 `void*` 转换成错误的类型**：比如数组是 `double`，却转换为 `int*`。
8. **用减法写比较函数**：整数溢出导致排序错误。

## 7. 小结

- 指针加减整数以元素为单位移动；两个指针相减得到相隔的元素个数；`p[i]` 等价于 `*(p + i)`
- 数组名在大多数情况下转换为首元素的指针，但数组不是指针：`sizeof` 不同，数组不能赋值
- 要在函数中修改调用者的指针变量，传二级指针
- `const int* p` 不能改数据，`int* const p` 不能改指向，从右往左读
- 函数指针保存函数的地址，用于回调；`qsort` 通过比较函数排序任意类型的数组
- 比较函数用 `(x > y) - (x < y)`，不要用 `x - y`，后者会溢出

## 8. 练习

1. 只用指针（不使用下标 `[]`），写一个函数 `int sumRange(const int* begin, const int* end)`，计算 `[begin, end)` 范围内所有元素的和。
2. 写一个比较函数，用 `qsort` 把 `{5, 2, 9, 1, 7}` 按从大到小的顺序排序。
3. 对数组 `int a[] = {1, 2, 3, 4};`，下面的代码输出什么？

   ```c
   int* p = a + 1;
   p += 2;
   printf("%d %td %d\n", *p, p - a, *(a + 1) + 5);
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```c
   int sumRange(const int* begin, const int* end)
   {
       int sum = 0;
       for (const int* p = begin; p != end; ++p) {
           sum += *p;
       }
       return sum;
   }
   ```

   对 `{1, 2, 3, 4}`，`sumRange(a, a + 4)` 是 10，`sumRange(a + 2, a + 4)`（后两个元素）是 7。用一对指针表示范围，可以很方便地处理数组的任意一段。

2. 把比较的方向反过来即可：

   ```c
   int descending(const void* a, const void* b)
   {
       int x = *(const int*)a, y = *(const int*)b;
       return (x < y) - (x > y);
   }

   qsort(v, 5, sizeof(int), descending);
   ```

   结果是 `9 7 5 2 1`。

3. 输出 `4 3 7`。`p` 从 `a + 1` 前进 2 个元素，指向 `a[3]`，所以 `*p` 是 4，`p - a` 是 3；`*(a + 1)` 是 `a[1]`，即 2，加 5 得 7。注意 `*(a + 1) + 5` 和 `*(a + 1 + 5)` 是完全不同的，后者会越界。

</details>
