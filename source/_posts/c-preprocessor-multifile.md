---
title: 预处理与多文件编程
date: 2026-10-23 18:00:00
updated: 2026-10-23 18:00:00
categories:
  - C/C++
  - C语言基础
tags:
  - C语言
  - 预处理
  - 头文件
order: 13
description: 程序变大之后，需要拆分成多个文件。本文介绍宏定义和宏的经典陷阱（用 -E 查看展开结果）、条件编译与 assert，然后讲解头文件和源文件如何分工、头文件保护的作用，以及 extern 和 static 如何控制符号的可见性，并复现重复定义和找不到符号两种链接错误。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用 `#define` 定义常量和宏，知道宏的陷阱以及什么时候应该用函数代替；会用条件编译和 `assert`；会把程序拆分成多个 `.c` 和 `.h` 文件；理解头文件保护；会用 `extern` 共享全局变量，用 `static` 隐藏内部的函数和变量。
>
> **前置知识**：《函数》、《结构体、联合与枚举》。编译的四个步骤见《开发环境与编程工具》第 1 节。
>
> **编译环境**：`gcc -std=c11 -Wall -Wextra`。

## 1. 预处理

以 `#` 开头的行是**预处理指令**，在真正编译之前，由**预处理器**做纯文本的处理：

- `#include`：把另一个文件的内容**原样复制**到这里。
- `#define`：定义**宏**，之后出现宏名的地方都被**替换**成定义的内容。
- `#if`、`#ifdef` 等：**条件编译**，根据条件保留或删除一部分代码。

用 `gcc -E` 可以看到预处理之后的结果，这是排查宏相关问题的利器。

## 2. 宏

```c
#include <stdio.h>

#define PI 3.14159
#define SQUARE_BAD(x) x * x
#define SQUARE(x) ((x) * (x))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

static inline int maxInt(int a, int b)       // 用函数代替宏
{
    return a > b ? a : b;
}

int main(void)
{
    int a = 3;
    printf("SQUARE_BAD(a + 1) = %d\n", SQUARE_BAD(a + 1));
    printf("SQUARE(a + 1)     = %d\n", SQUARE(a + 1));
    printf("100 / SQUARE(2)   = %d\n", 100 / SQUARE(2));

    int i = 5, j = 3;
    int m = MAX(i++, j);
    printf("MAX(i++, j) = %d，i 变成了 %d\n", m, i);
    i = 5;
    m = maxInt(i++, j);
    printf("maxInt(i++, j) = %d，i 变成了 %d\n", m, i);
    printf("圆面积 %.2f\n", PI * SQUARE(2.0));
    return 0;
}
```

运行结果：

```
SQUARE_BAD(a + 1) = 7
SQUARE(a + 1)     = 16
100 / SQUARE(2)   = 25
MAX(i++, j) = 6，i 变成了 7
maxInt(i++, j) = 5，i 变成了 6
圆面积 12.57
```

`(a + 1)` 的平方应该是 16，`SQUARE_BAD` 却算出了 7；`MAX(i++, j)` 的结果是 6，而 `i` 增加了两次。用 `gcc -E` 看看宏展开成了什么：

```bash
gcc -std=c11 -E macro.c | grep -E 'printf\("SQUARE|int m = '
```

```
    printf("SQUARE_BAD(a + 1) = %d\n", a + 1 * a + 1);
    printf("SQUARE(a + 1)     = %d\n", ((a + 1) * (a + 1)));
    int m = ((i++) > (j) ? (i++) : (j));
```

真相大白：

1. **宏是纯粹的文本替换，不理解运算符的优先级**。`SQUARE_BAD(a + 1)` 被替换成 `a + 1 * a + 1`，也就是 3 + 3 + 1 = 7。所以**宏的每个参数和整个宏体都要加括号**，像 `SQUARE` 那样。整体的括号也很重要：没有它，`100 / SQUARE(2)` 会变成 `100 / (2) * (2)`。
2. **宏的参数可能被计算多次**。`MAX(i++, j)` 展开后，`i++` 出现了两次，比较时执行一次，作为结果又执行一次。即使括号加得再完美也无法避免这个问题。

用 `static inline` 函数代替宏，就不会有这些问题：参数只计算一次，有类型检查，也能用调试器单步进入。`inline` 建议编译器把函数体直接展开到调用处，和宏一样没有函数调用的开销。

**规则：定义常量可以用 `#define` 或 `const`；能用函数实现的，就不要用宏。** 宏仍然有它的用武之地，比如下一节的条件编译和调试日志，以及需要 `__FILE__`、`__LINE__` 的场合。

## 3. 条件编译和 assert

```c
#include <assert.h>
#include <stdio.h>

#ifdef DEBUG
#define LOG(msg) fprintf(stderr, "[%s:%d %s] %s\n", __FILE__, __LINE__, __func__, msg)
#else
#define LOG(msg) ((void)0)
#endif

double average(const int* a, int n)
{
    assert(n > 0);                               // 调试时检查前提条件
    LOG("开始计算");
    int sum = 0;
    for (int i = 0; i < n; ++i) sum += a[i];
    return (double)sum / n;
}

int main(void)
{
    int a[] = {1, 2, 3};
#if defined(__APPLE__)
    printf("在 macOS 上编译\n");
#elif defined(__linux__)
    printf("在 Linux 上编译\n");
#elif defined(_WIN32)
    printf("在 Windows 上编译\n");
#endif
    printf("平均值 %.1f\n", average(a, 3));
    printf("平均值 %.1f\n", average(a, 0));
    return 0;
}
```

用三种方式编译运行：

```bash
gcc -std=c11 debug.c -o debug               # 默认
gcc -std=c11 -DDEBUG debug.c -o debug       # 定义 DEBUG 宏
gcc -std=c11 -DNDEBUG debug.c -o debug      # 定义 NDEBUG 宏
```

默认：

```
在 macOS 上编译
平均值 2.0
Assertion failed: (n > 0), function average, file debug.c, line 12.
```

`-DDEBUG`：

```
在 macOS 上编译
[debug.c:13 average] 开始计算
平均值 2.0
Assertion failed: (n > 0), function average, file debug.c, line 12.
```

`-DNDEBUG`：

```
在 macOS 上编译
平均值 2.0
平均值 nan
```

- **`#ifdef 宏名`** 判断宏是否被定义过；**`#if defined(...)`** 可以组合多个条件。编译选项 **`-D名字`** 相当于在代码开头写了 `#define 名字`。
- 加上 `-DDEBUG` 后，`LOG` 输出了调试信息；不加时，`LOG` 被替换成什么都不做的 `((void)0)`，**发布版本中完全没有这些代码**。
- 编译器预定义了很多有用的宏：`__FILE__`（文件名）、`__LINE__`（行号）、`__func__`（函数名，严格来说它是一个变量）；`__APPLE__`、`__linux__`、`_WIN32` 表示目标平台，用来编写跨平台的代码。
- **`assert(条件)`**（在 `<assert.h>` 中）：条件为假时，输出出错的文件、行号和条件，然后**终止程序**（退出码 134）。它用来检查「如果程序没有 bug，就一定成立」的条件。
- 定义了 **`NDEBUG`** 之后，所有的 `assert` 都被删除了。此时 `average(a, 0)` 计算了 0 / 0，得到 `nan`。所以 **`assert` 只能用来发现 bug，不能用来处理正常的错误**（比如用户的输入错误、文件打不开），那些情况要用 `if` 判断并处理。

## 4. 多文件编程

把所有代码都写在一个文件中，几百行之后就很难维护了。通常的做法是**按功能拆分**，每个模块由两个文件组成：

- **头文件 `.h`**：模块的**接口**，放类型定义、函数声明、宏定义。别的文件通过 `#include` 使用它。
- **源文件 `.c`**：模块的**实现**，放函数的定义。

下面把一个计算点的距离、中点的模块拆分出来：

```c
// point.h
#ifndef POINT_H
#define POINT_H

typedef struct {
    double x, y;
} Point;

double distance(Point a, Point b);
Point midpoint(Point a, Point b);

extern int pointCalls;                           // 声明：定义在 point.c 中

#endif
```

```c
// point.c
#include <math.h>
#include "point.h"

int pointCalls = 0;                              // 定义

static double square(double v)                   // static：只在本文件中可见
{
    return v * v;
}

double distance(Point a, Point b)
{
    ++pointCalls;
    return sqrt(square(a.x - b.x) + square(a.y - b.y));
}

Point midpoint(Point a, Point b)
{
    ++pointCalls;
    Point m = {(a.x + b.x) / 2, (a.y + b.y) / 2};
    return m;
}
```

```c
// shape.h
#ifndef SHAPE_H
#define SHAPE_H

#include "point.h"

typedef struct {
    Point center;
    double radius;
} Circle;

#endif
```

```c
// main.c
#include <stdio.h>
#include "point.h"
#include "shape.h"                               // 它也包含了 point.h

int main(void)
{
    Point a = {0, 0}, b = {3, 4};
    Circle c = {midpoint(a, b), distance(a, b) / 2};
    printf("距离 %.1f，圆心 (%.1f, %.1f)，半径 %.1f\n", distance(a, b), c.center.x, c.center.y, c.radius);
    printf("point 函数被调用了 %d 次\n", pointCalls);
    return 0;
}
```

编译时把所有的 `.c` 文件都写上（在 Linux 上使用 `sqrt` 还要在最后加 `-lm`）：

```bash
gcc -std=c11 -Wall -Wextra main.c point.c -o app
./app
```

```
距离 5.0，圆心 (1.5, 2.0)，半径 2.5
point 函数被调用了 3 次
```

- `#include "point.h"` 用**双引号**，表示先在当前目录中查找；`#include <stdio.h>` 用**尖括号**，表示在系统目录中查找。
- 每个 `.c` 文件**单独编译**，然后由链接器组合在一起（《开发环境与编程工具》第 1.5 节）。`main.c` 编译时只看到了 `distance` 的声明，链接时才找到 `point.c` 中的定义。
- `point.c` 自己也要 `#include "point.h"`：这样编译器能检查声明和定义是否一致，`Point` 类型也是在头文件中定义的。

### 4.1 头文件保护

`main.c` 包含了 `point.h`，又包含了 `shape.h`，而 `shape.h` 也包含了 `point.h`。所以 `point.h` 的内容被复制了**两次**。如果去掉 `point.h` 中的 `#ifndef POINT_H` / `#define POINT_H` / `#endif`：

```
./point_noguard.h:4:3: error: typedef redefinition with different types ('struct Point' vs 'struct Point')
```

`Point` 被定义了两次。**头文件保护**（include guard）的原理是：第一次包含时，`POINT_H` 还没有定义，于是定义它并保留文件的内容；第二次包含时，`POINT_H` 已经定义了，`#ifndef` 的条件不成立，整个文件的内容被跳过。

宏的名字通常用文件名的大写形式。更简单的写法是在文件的第一行写 **`#pragma once`**，它不是 C 标准的一部分，但所有主流编译器都支持。**每个头文件都要有头文件保护**。

### 4.2 头文件中不要放定义

如果在头文件中**定义**一个全局变量：

```c
// config.h
#pragma once
int counter = 0;
```

`a.c` 和 `b.c` 都包含了这个头文件：

```
duplicate symbol '_counter' in:
    /private/var/folders/.../b-97cc94.o
    /private/var/folders/.../a-897e09.o
ld: 1 duplicate symbols
```

**链接错误**：`counter` 被定义了两次。头文件保护只能防止**同一个 `.c` 文件**重复包含，`a.c` 和 `b.c` 是分别编译的，各自都包含了一份定义。函数定义放在头文件中也是同样的问题。

正确的做法是像 `point.h` 中的 `pointCalls` 那样：**头文件中用 `extern` 声明，在某一个 `.c` 文件中定义**。

| 放在头文件中 | 放在源文件中 |
|---|---|
| 类型定义（`struct`、`typedef`、`enum`） | 函数定义 |
| 函数声明 | 全局变量的定义 |
| `extern` 变量声明 | `static` 的函数和变量 |
| 宏定义、`static inline` 函数 | |

### 4.3 static：隐藏实现细节

`point.c` 中的 `square` 函数加了 `static`，它只在 `point.c` 内部使用。如果另一个文件试图调用它：

```c
// use_static.c
double square(double v);
int main(void) { return (int)square(2.0); }
```

```
Undefined symbols for architecture arm64:
  "_square", referenced from:
      _main in use_static-cf20b8.o
ld: symbol(s) not found for architecture arm64
```

**`static` 用在函数和全局变量上，表示「只在当前文件中可见」**（叫作内部链接）。这有两个好处：

1. **隐藏实现细节**：模块的使用者只能调用头文件中公开的函数，内部的辅助函数可以随意修改。
2. **避免名字冲突**：两个文件中各有一个 `static` 的 `square`，互不影响；如果都不是 `static`，链接时就会报重复定义。

**规则：不需要被其他文件使用的函数和全局变量，都加上 `static`。**

注意 `static` 的三种用法含义不同：用在函数内的局部变量上，表示生命周期延长到整个程序（《函数》）；用在函数和全局变量上，表示只在本文件中可见。

## 5. 常见错误

1. **宏的参数和宏体没有加括号**：`SQUARE_BAD(a + 1)` 得到 7。
2. **把有副作用的表达式传给宏**：`MAX(i++, j)` 中 `i` 增加了两次。
3. **用 `assert` 处理正常的错误**：定义了 `NDEBUG` 后检查就消失了。
4. **头文件没有头文件保护**：重复包含导致重复定义。
5. **在头文件中定义变量或函数**：多个 `.c` 包含后链接时重复定义。
6. **编译时漏掉了某个 `.c` 文件**：链接时找不到符号。
7. **内部使用的函数没有加 `static`**：可能与其他文件中的同名函数冲突。
8. **`#include` 一个 `.c` 文件**：应该包含头文件，`.c` 文件交给编译器单独编译。

## 6. 小结

- 预处理在编译之前做文本处理：`#include` 复制文件，`#define` 替换文本，`#if` 选择代码；用 `gcc -E` 查看结果
- 宏的参数和整体都要加括号；宏的参数可能被计算多次；能用函数（`static inline`）就不用宏
- `-D名字` 定义宏，配合 `#ifdef` 实现调试开关和跨平台代码；`assert` 检查程序的 bug，定义 `NDEBUG` 后被删除
- 头文件放接口（类型、声明），源文件放实现（定义）；每个头文件都要有头文件保护
- 全局变量在头文件中用 `extern` 声明，在一个源文件中定义
- `static` 的函数和全局变量只在当前文件中可见

## 7. 练习

1. 写一个宏 `ARRAY_SIZE(a)`，计算数组的元素个数。它能用在函数的数组参数上吗？
2. 下面的代码输出什么？

   ```c
   #define DOUBLE(x) x + x
   printf("%d\n", 3 * DOUBLE(2));
   ```

3. 第 4.2 节的 `config.h` 应该怎样修改，才能让 `a.c` 和 `b.c` 共享同一个 `counter`？

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```c
   #define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
   ```

   对 `int nums[7]` 得到 7，对 `double ds[3]` 得到 3。它**不能**用在函数的数组参数上，因为参数实际上是一个指针，会得到错误的结果（《数组》第 3 节）。这里必须用宏而不能用函数，因为一旦把数组传给函数，它就退化成指针了。

2. 输出 8，而不是 12。宏展开后是 `3 * 2 + 2`。正确的定义是 `#define DOUBLE(x) ((x) + (x))`。

3. 头文件中只放声明，在其中一个 `.c` 文件中定义：

   ```c
   // config.h
   #pragma once
   extern int counter;

   // a.c
   #include "config.h"
   int counter = 0;
   void inc(void) { ++counter; }
   ```

   `b.c` 不需要修改。编译链接成功，`b.c` 中调用 `inc()` 之后输出 1，说明两个文件访问的是同一个变量。

</details>
