---
title: 动态内存
date: 2026-10-23 17:20:00
updated: 2026-10-23 17:20:00
categories:
  - C/C++
  - C语言基础
tags:
  - C语言
  - 内存管理
  - malloc
order: 11
description: 数组的大小必须在编译时确定，局部变量在函数返回时就消失了，动态内存解决了这两个问题。本文介绍 malloc、calloc、realloc 和 free，用 realloc 实现一个自动增长的数组，并用 leaks 和 AddressSanitizer 实测内存泄漏、realloc 的错误用法、释放后使用、重复释放等问题。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解栈和堆的区别；会用 `malloc`、`calloc`、`realloc`、`free` 管理内存，并检查分配是否成功；会实现一个可以自动增长的数组；能用工具发现内存泄漏、释放后使用、重复释放等错误，知道如何避免。
>
> **前置知识**：《函数》（栈和栈溢出）、《指针入门》。
>
> **编译环境**：`gcc -std=c11 -Wall -Wextra`，macOS。内存泄漏用 macOS 自带的 `leaks` 工具检查，Linux 上可以用 `valgrind --leak-check=full` 或者 `-fsanitize=address`。

## 1. 为什么需要动态内存

到目前为止，我们使用的内存有两种：

- **局部变量**存放在**栈**上，函数返回时自动释放。栈的空间有限（通常 8 MB 左右），《函数》中递归太深就导致了栈溢出。而且不能把局部变量的地址返回给调用者（《指针入门》中的悬空指针）。
- **全局变量和静态变量**存放在静态存储区，整个程序运行期间都存在，但大小必须在编译时确定。

很多时候，我们**在运行时才知道需要多少内存**（比如用户输入了多少个数字），或者希望一块数据的**生命周期由自己控制**，不随函数返回而消失。这时就需要从**堆**（heap）上**动态地**申请内存。

| | 栈 | 堆 |
|---|---|---|
| 分配方式 | 定义局部变量时自动分配 | 调用 `malloc` 等函数手动申请 |
| 释放方式 | 函数返回时自动释放 | **必须手动调用 `free`** |
| 大小 | 较小，通常几 MB | 很大，受限于系统的可用内存 |
| 大小何时确定 | 编译时 | 运行时 |

## 2. malloc 和 free

```c
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    int n = 5;                                  // 长度可以在运行时决定
    int* scores = malloc(n * sizeof(int));      // 申请 n 个 int 的空间
    if (scores == NULL) {
        fprintf(stderr, "内存不足\n");
        return 1;
    }
    for (int i = 0; i < n; ++i) {
        scores[i] = 60 + i * 10;                // 和数组一样使用
    }
    for (int i = 0; i < n; ++i) printf("%d ", scores[i]);
    printf("\n");
    free(scores);                               // 用完之后释放
    scores = NULL;

    size_t big = 10 * 1000 * 1000;              // 1000 万个 int，40 MB
    int* huge = malloc(big * sizeof(int));
    if (huge != NULL) {
        huge[big - 1] = 42;
        printf("堆上的 40 MB 数组: huge[%zu] = %d\n", big - 1, huge[big - 1]);
        free(huge);
    }
    return 0;
}
```

运行结果：

```
60 70 80 90 100 
堆上的 40 MB 数组: huge[9999999] = 42
```

- **`malloc(字节数)`**（在 `<stdlib.h>` 中）申请一块指定大小的内存，返回指向它的指针（`void*`，可以直接赋给任何类型的指针）。**参数是字节数**，所以申请 n 个 `int` 要写 `n * sizeof(int)`。
- 申请失败时返回 **`NULL`**，**一定要检查**。
- 得到的指针可以像数组一样用下标访问。
- 用完之后调用 **`free(指针)`** 释放。释放后把指针设为 `NULL`，可以防止之后误用它。
- 在栈上定义一个 40 MB 的数组会导致栈溢出，在堆上则没有问题。

> `free(NULL)` 是安全的，什么都不做。所以释放前不需要检查指针是否为 `NULL`。

### 2.1 calloc：分配并清零

**`malloc` 分配的内存中的内容是不确定的**，和未初始化的局部变量一样。**`calloc(个数, 每个的大小)`** 会把内存清零：

```c
int* c = calloc(5, sizeof(int));            // 分配并清零
for (int i = 0; i < 5; ++i) printf(" %d", c[i]);
free(c);
```

```
calloc: 0 0 0 0 0
```

`calloc` 的另一个好处是：它会检查「个数 × 大小」是否溢出。而 `malloc(n * sizeof(int))` 中，如果 `n` 非常大，乘法可能溢出成一个很小的数，结果分配到的内存比预期的少得多。

## 3. realloc：改变大小

**`realloc(指针, 新的字节数)`** 调整一块已分配内存的大小。下面用它实现一个**自动增长的数组**，这正是 C++ `std::vector` 的原理（《STL 容器》）：

```c
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int* data;
    size_t size;
    size_t capacity;
} IntVec;

int push(IntVec* v, int value)
{
    if (v->size == v->capacity) {
        size_t newCap = v->capacity == 0 ? 4 : v->capacity * 2;
        int* p = realloc(v->data, newCap * sizeof(int));   // 先用临时变量接收
        if (p == NULL) {
            return 0;                           // 失败：原来的数据仍然有效
        }
        if (p != v->data) {
            printf("  扩容 %zu → %zu，地址 %p → %p\n", v->capacity, newCap, (void*)v->data, (void*)p);
        } else {
            printf("  扩容 %zu → %zu，原地扩展\n", v->capacity, newCap);
        }
        v->data = p;
        v->capacity = newCap;
    }
    v->data[v->size++] = value;
    return 1;
}

int main(void)
{
    IntVec v = {NULL, 0, 0};
    for (int i = 1; i <= 20; ++i) {
        if (!push(&v, i * i)) {
            fprintf(stderr, "内存不足\n");
            break;
        }
    }
    printf("size = %zu, capacity = %zu, 最后一个 = %d\n", v.size, v.capacity, v.data[v.size - 1]);
    free(v.data);
    return 0;
}
```

运行结果：

```
  扩容 0 → 4，地址 0x0 → 0x102db9160
  扩容 4 → 8，地址 0x102db9160 → 0x102db9170
  扩容 8 → 16，地址 0x102db9170 → 0x102db9190
  扩容 16 → 32，地址 0x102db9190 → 0x102db9240
size = 20, capacity = 32, 最后一个 = 400
```

（`IntVec` 是一个**结构体**，把数据指针、元素个数、容量三个变量组合在一起；`v->size` 访问指针 `v` 所指向结构体的成员。下一篇会详细介绍结构体。）

- `realloc` 可能在原地扩展，也可能**分配一块新的内存，把原来的内容复制过去，再释放旧的内存**。这里每次扩容地址都变了。所以 **realloc 之后，所有指向旧内存的指针都失效了**，这和 `vector` 扩容后迭代器失效是同一回事。
- `realloc(NULL, n)` 等价于 `malloc(n)`，所以第一次 `push` 时 `data` 为 `NULL` 也能正常工作。
- 容量每次翻倍，添加 20 个元素只扩容了 4 次。

### 3.1 陷阱：p = realloc(p, ...)

上面的代码用一个临时变量 `p` 接收 `realloc` 的结果。很多人会直接写成：

```c
int* data = malloc(100 * sizeof(int));
data[0] = 42;
data = realloc(data, SIZE_MAX / 2);        // 申请一块不可能满足的内存
if (data == NULL) {
    printf("realloc 失败，data 变成了 NULL，原来的 400 字节再也找不到了\n");
    return 1;
}
```

`realloc` **失败时返回 `NULL`，但原来的内存不会被释放**。直接赋值给 `data`，原来的地址就被覆盖了，那块内存再也无法访问，也无法释放。`leaks` 的检查结果：

```
realloc 失败，data 变成了 NULL，原来的 400 字节再也找不到了
Process 47572: 1 leak for 448 total leaked bytes.
```

（报告中的 448 是分配器对齐后的大小。）**规则：总是用一个临时变量接收 `realloc` 的返回值，确认成功之后再赋值。**

## 4. 内存错误

手动管理内存，最常见的错误有四种。它们都**不会导致编译错误**，而且常常不会立即导致崩溃。

### 4.1 内存泄漏

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* makeGreeting(const char* name)
{
    size_t len = strlen("Hello, ") + strlen(name) + 1;
    char* s = malloc(len);
    if (s != NULL) {
        snprintf(s, len, "Hello, %s", name);
    }
    return s;                                  // 调用者负责 free
}

int main(void)
{
    for (int i = 0; i < 3; ++i) {
        char* g = makeGreeting("Tom");
        printf("%s\n", g);
        // 忘了 free(g);
    }
    return 0;
}
```

`makeGreeting` 在堆上创建字符串并返回，这是合法的（不像返回局部变量的地址），但**调用者必须负责释放它**。忘了 `free`，用 `leaks` 检查：

```bash
gcc -std=c11 -g leak.c -o leak
leaks --atExit -- ./leak
```

```
Process 47500: 3 leaks for 96 total leaked bytes.
STACK OF 3 INSTANCES OF 'ROOT LEAK: <malloc in makeGreeting>':
1   leak                                  0x1040cc48c makeGreeting + 44  leak.c:8
```

报告指出：有 3 处泄漏，都是在 `makeGreeting` 第 8 行的 `malloc` 分配的。

**内存泄漏**是指申请的内存在不再使用后没有被释放。程序退出时操作系统会回收所有内存，所以对于很快就结束的小程序，泄漏不会造成明显的后果。但对于长时间运行的程序（服务器、桌面应用），每处理一个请求泄漏一点，内存占用就会越来越大，最终耗尽内存。

**规则：每一个 `malloc` 都要有一个对应的 `free`。** 函数返回一块动态分配的内存时，要在注释或文档中写清楚由调用者负责释放。

### 4.2 释放后使用

```c
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    int* p = malloc(4 * sizeof(int));
    p[0] = 100;
    free(p);
    int* q = malloc(4 * sizeof(int));          // 新的分配可能复用同一块内存
    q[0] = 999;
    printf("p[0] = %d（p 已经被释放了）\n", p[0]);
    printf("p == q ? %s\n", p == q ? "是" : "否");
    free(q);
    return 0;
}
```

```
p[0] = 999（p 已经被释放了）
p == q ? 是
```

`p` 被释放之后，那块内存被新的 `malloc` 分配给了 `q`。通过 `p` 读到的是 `q` 写入的 999。如果通过 `p` 写入数据，就会悄悄地破坏 `q` 的数据。**释放后使用**（use after free）是最危险的内存错误之一，也是很多安全漏洞的根源。AddressSanitizer 能发现它：

```
ERROR: AddressSanitizer: heap-use-after-free on address 0x602000000950
READ of size 4 at 0x602000000950 thread T0
    #0 0x00010258c954 in main uaf.c:11
freed by thread T0 here:
    ...
previously allocated by thread T0 here:
    ...
```

报告还会给出这块内存是在哪里分配、在哪里释放的，非常便于排查。

### 4.3 重复释放

```c
int* p = malloc(16);
free(p);
free(p);                                   // 第二次释放
```

在 macOS 上，程序被直接终止（退出码 133）。AddressSanitizer 的报告：

```
ERROR: AddressSanitizer: attempting double-free on 0x602000000950 in thread T0:
    #0 0x00010358d368 in free+0x7c
    #1 0x000102e505c0 in main dfree.c:6
```

**重复释放**会破坏分配器内部的数据结构，后果不可预料。`free` 之后立即把指针设为 `NULL`，再次 `free(NULL)` 就是安全的。

### 4.4 释放不是 malloc 得到的内存

```c
int x = 5;
int* p = &x;
free(p);                                   // 释放不是 malloc 得到的内存
```

程序崩溃（退出码 134）。**只能 `free` 由 `malloc`、`calloc`、`realloc` 返回的指针**，而且必须是返回的那个原始指针，不能是它加上某个偏移之后的值。

## 5. 管理内存的原则

1. **谁申请，谁释放**。在设计函数时就要想清楚每块内存的「所有者」是谁。
2. **申请后立即检查是否为 `NULL`**。
3. **释放后立即把指针设为 `NULL`**。
4. **成对地写代码**：写下 `malloc` 的同时，就想好对应的 `free` 写在哪里。函数中有多处 `return` 时，每条路径都要释放；第 1.14 篇会介绍用 `goto` 集中释放资源的写法。
5. **开发时打开检查工具**：`-fsanitize=address`，以及 `leaks` 或 `valgrind`。

手动管理内存非常容易出错，这正是 C++ 引入构造函数、析构函数和智能指针（RAII）的原因：让内存在离开作用域时**自动**释放（《动态内存：new 与 delete》、《智能指针：unique_ptr、shared_ptr 与 weak_ptr》）。

## 6. 常见错误

1. **忘了乘以 `sizeof`**：`malloc(n)` 只分配 n 个字节，而不是 n 个 `int`。
2. **不检查 `malloc` 的返回值**：分配失败时解引用 `NULL`。
3. **使用 `malloc` 分配的内存之前没有初始化**：内容是不确定的。需要清零时用 `calloc`。
4. **`p = realloc(p, ...)`**：失败时原来的内存泄漏。
5. **realloc 之后继续使用旧的指针**：内存可能已经移动。
6. **内存泄漏**：`malloc` 之后没有 `free`。
7. **释放后使用**：`free` 之后又访问那块内存。
8. **重复释放**、**释放非堆内存**：程序崩溃。

## 7. 小结

- 栈上的内存自动管理，但空间小、大小固定；堆上的内存用 `malloc` 等申请，空间大、大小可以在运行时决定，但必须手动 `free`
- `malloc(字节数)` 分配未初始化的内存，`calloc(个数, 大小)` 分配并清零，`realloc` 调整大小，三者失败时都返回 `NULL`
- `realloc` 可能移动内存，旧指针失效；用临时变量接收返回值，否则失败时会泄漏
- 内存泄漏用 `leaks` / `valgrind` 检查；释放后使用、重复释放、越界用 AddressSanitizer 检查
- 谁申请谁释放；申请后检查 `NULL`；释放后置 `NULL`

## 8. 练习

1. 写一个函数 `int* readNumbers(size_t* count)`，从标准输入读取整数，直到输入结束，返回一个动态分配的数组，并通过 `count` 返回读到的个数。数组要能自动增长。调用者负责释放。
2. 写一个函数 `char* duplicate(const char* s)`，返回 `s` 的一个动态分配的副本（也就是 POSIX 中的 `strdup` 函数）。
3. 下面的代码有两处错误，AddressSanitizer 会首先报告哪一处？

   ```c
   int* p = malloc(10);
   for (int i = 0; i < 10; ++i) {
       p[i] = i;
   }
   free(p);
   p[0] = 1;
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```c
   int* readNumbers(size_t* count)
   {
       size_t size = 0, capacity = 4;
       int* data = malloc(capacity * sizeof(int));
       if (data == NULL) return NULL;
       int x;
       while (scanf("%d", &x) == 1) {
           if (size == capacity) {
               int* p = realloc(data, capacity * 2 * sizeof(int));
               if (p == NULL) {
                   free(data);              // 失败时释放已有的内存，避免泄漏
                   return NULL;
               }
               data = p;
               capacity *= 2;
           }
           data[size++] = x;
       }
       *count = size;
       return data;
   }
   ```

   用 `seq 1 10 | ./ex` 输入 1 到 10，输出 `读到 10 个数，和为 55`。

2. 参考代码：

   ```c
   char* duplicate(const char* s)
   {
       size_t len = strlen(s) + 1;              // 要包括 '\0'
       char* copy = malloc(len);
       if (copy != NULL) {
           memcpy(copy, s, len);
       }
       return copy;
   }
   ```

   副本是独立的，可以修改，用完之后要 `free`。

3. 第一处错误是 `malloc(10)` 只分配了 10 个**字节**，只够放 2 个半 `int`，循环写到 `p[2]` 时就越界了。ASan 首先报告的就是它：

   ```
   ERROR: AddressSanitizer: heap-buffer-overflow on address 0x602000000958
   WRITE of size 4 at 0x602000000958 thread T0
       #0 0x000102e10638 in main ex3.c:6
   0x60200000095a is located 0 bytes after 10-byte region [0x602000000950,0x60200000095a)
   ```

   修复后，第二处错误是 `free(p)` 之后又写入 `p[0]`，属于释放后使用。正确的写法是 `malloc(10 * sizeof(int))`，并删掉最后一行（或者在 `free` 之前完成写入）。编译器对这两处错误都没有给出任何警告。

</details>
