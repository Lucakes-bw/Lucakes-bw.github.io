---
title: 数据类型与变量
date: 2026-10-23 14:20:00
updated: 2026-10-23 14:20:00
categories:
  - C/C++
  - C语言基础
tags:
  - C语言
  - 数据类型
order: 2
description: 变量要有类型，类型决定了它占多少内存、能表示什么范围的值。本文实测各种类型的大小和范围，复现整数溢出、0.1 + 0.2 不等于 0.3、有符号与无符号比较等陷阱，并演示有符号溢出这种未定义行为在开启优化后如何改变程序的结果。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会定义和初始化变量；了解各种整数类型和浮点类型的大小和范围；理解整数溢出和浮点误差；掌握隐式类型转换和强制类型转换的规则；知道什么是未定义行为，为什么它很危险。
>
> **前置知识**：《C 语言概述与第一个程序》。
>
> **编译环境**：`gcc -std=c11 -Wall -Wextra`，64 位 macOS（Apple clang）。类型的大小在不同的平台上可能不同，文中会说明。

## 1. 变量

**变量**是一块有名字的内存，用来存放数据。使用变量之前必须先**定义**它，指明它的**类型**和**名字**：

```c
int age = 18;              // 定义一个 int 类型的变量 age，初始值为 18
double price = 9.99;
char grade = 'A';
int a = 1, b = 2, c;       // 一次定义多个变量，c 没有初始化
age = 19;                  // 赋值：修改变量的值
```

变量名的规则：由字母、数字和下划线组成，**不能以数字开头**，区分大小写，不能是 `int`、`return` 这样的关键字。好的变量名应该说明它的含义，比如 `studentCount` 或 `student_count`，而不是 `a`、`x1`。

### 1.1 未初始化的变量

```c
#include <stdio.h>
int main(void)
{
    int count;
    int sum;
    printf("count = %d, sum = %d\n", count, sum);
    return 0;
}
```

编译器给出了警告：

```
uninit.c:6:38: warning: variable 'count' is uninitialized when used here [-Wuninitialized]
uninit.c:6:45: warning: variable 'sum' is uninitialized when used here [-Wuninitialized]
```

运行结果：

```
count = -270991144, sum = 1
```

**定义在函数内部的变量不会自动初始化为 0**，它的值是那块内存中原来留下的内容，每次运行、换一台机器都可能不同。用这样的值去计算，结果自然是错的。**养成习惯：定义变量时就给它一个初始值**。

## 2. 整数类型

```c
#include <limits.h>
#include <stdio.h>

int main(void)
{
    printf("char        %zu 字节，范围 %d ~ %d\n", sizeof(char), CHAR_MIN, CHAR_MAX);
    printf("short       %zu 字节，范围 %d ~ %d\n", sizeof(short), SHRT_MIN, SHRT_MAX);
    printf("int         %zu 字节，范围 %d ~ %d\n", sizeof(int), INT_MIN, INT_MAX);
    printf("long        %zu 字节\n", sizeof(long));
    printf("long long   %zu 字节，范围 %lld ~ %lld\n", sizeof(long long), LLONG_MIN, LLONG_MAX);
    printf("unsigned    %zu 字节，范围 0 ~ %u\n", sizeof(unsigned int), UINT_MAX);
    printf("float       %zu 字节\n", sizeof(float));
    printf("double      %zu 字节\n", sizeof(double));
    return 0;
}
```

运行结果：

```
char        1 字节，范围 -128 ~ 127
short       2 字节，范围 -32768 ~ 32767
int         4 字节，范围 -2147483648 ~ 2147483647
long        8 字节
long long   8 字节，范围 -9223372036854775808 ~ 9223372036854775807
unsigned    4 字节，范围 0 ~ 4294967295
float       4 字节
double      8 字节
```

- **`sizeof`** 是一个运算符，返回类型或变量占用的字节数。它的结果类型是 `size_t`，用 `%zu` 输出。
- `<limits.h>` 中定义了 `INT_MAX`、`INT_MIN` 等常量，表示各类型的范围。
- 1 个字节是 8 个二进制位。`int` 有 32 位，能表示 2³² 个不同的值，一半给负数，一半给 0 和正数，所以范围是 -2³¹ 到 2³¹ - 1，**大约 ±21 亿**。
- 每种整数类型都有 **`unsigned`**（无符号）版本，只能表示 0 和正数，但正数的范围扩大了一倍。

**C 标准只规定了各类型的最小范围，没有规定具体的大小**。比如 `long` 在 64 位的 Linux 和 macOS 上是 8 字节，在 64 位的 Windows 上却是 4 字节。需要确定大小时，使用 `<stdint.h>` 中的**固定宽度类型**：

```c
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
int main(void)
{
    int32_t a = 100000;                     // 一定是 32 位
    int64_t b = (int64_t)a * a;             // 一定是 64 位
    uint8_t byte = 255;                     // 无符号 8 位，0 ~ 255
    byte = byte + 1;
    printf("a * a = %" PRId64 "，uint8_t 255 + 1 = %u\n", b, byte);
}
```

```
a * a = 10000000000，uint8_t 255 + 1 = 0
```

`int8_t`、`int16_t`、`int32_t`、`int64_t` 以及对应的 `uint` 版本，在网络协议、文件格式、嵌入式编程中经常用到。（输出 `int64_t` 要用 `<inttypes.h>` 中的 `PRId64` 宏，也可以先转换为 `long long` 再用 `%lld`。）

## 3. 整数溢出

如果计算结果超出了类型的范围，会发生什么？

```c
#include <limits.h>
#include <stdio.h>

int main(void)
{
    unsigned int u = 0;
    u = u - 1;                                  // 无符号：回绕到最大值
    printf("unsigned 0 - 1 = %u\n", u);

    int big = INT_MAX;
    printf("INT_MAX     = %d\n", big);
    big = big + 1;                              // 有符号溢出：未定义行为
    printf("INT_MAX + 1 = %d\n", big);

    int population = 2000000000;
    int doubled = population * 2;
    long long ok = (long long)population * 2;
    printf("20 亿 * 2：int 结果 %d，long long 结果 %lld\n", doubled, ok);
    return 0;
}
```

运行结果（**编译时没有任何警告**）：

```
unsigned 0 - 1 = 4294967295
INT_MAX     = 2147483647
INT_MAX + 1 = -2147483648
20 亿 * 2：int 结果 -294967296，long long 结果 4000000000
```

- **无符号整数溢出**会「回绕」：0 - 1 变成最大值，最大值 + 1 变成 0，就像里程表转了一圈。这是 C 标准**明确规定**的行为。
- **有符号整数溢出**是**未定义行为**（undefined behavior，UB）。上面的输出中 `INT_MAX + 1` 变成了最小的负数，但这只是在这台机器上碰巧如此，C 标准**没有保证任何结果**。
- 20 亿乘以 2 得到了一个负数。修复的方法是在计算之前**先转换为更大的类型**：`(long long)population * 2`。注意，写成 `(long long)(population * 2)` 是没有用的，因为溢出在转换之前就已经发生了。

### 3.1 未定义行为有多危险

「未定义行为」不是「结果是个随机数」，而是「**编译器可以假设它永远不会发生**」，并据此优化代码。看这个例子：

```c
#include <limits.h>
#include <stdio.h>

int willGrow(int x)
{
    return x + 1 > x;              // x + 1 一定比 x 大吗？
}

int main(void)
{
    volatile int big = INT_MAX;    // volatile：阻止编译器在编译时算出结果
    printf("willGrow(INT_MAX) = %d\n", willGrow(big));
    return 0;
}
```

分别用不优化（`-O0`）和优化（`-O2`）编译：

```
-O0: willGrow(INT_MAX) = 0
-O2: willGrow(INT_MAX) = 1
```

**同一份代码，只是改变了优化级别，结果就不同了**。不优化时，`INT_MAX + 1` 溢出成了负数，比较的结果是 0（假）。优化时，编译器推理：「有符号整数不会溢出，所以 `x + 1 > x` 永远为真」，直接把函数优化成了 `return 1;`。这两种结果都「符合」C 标准，因为标准对未定义行为没有任何要求。

**UndefinedBehaviorSanitizer** 可以在运行时发现有符号溢出：

```bash
gcc -std=c11 -g -fsanitize=undefined overflow.c -o overflow && ./overflow
```

```
overflow.c:12:15: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'
overflow.c:16:30: runtime error: signed integer overflow: 2000000000 * 2 cannot be represented in type 'int'
```

两处溢出都被准确地找了出来。**规则：选择足够大的类型，让计算不会溢出；不确定时用 `-fsanitize=undefined` 检查**。

## 4. 浮点类型

C 语言有两种常用的浮点类型：**`float`**（4 字节，大约 7 位有效数字）和 **`double`**（8 字节，大约 15～16 位有效数字）。**默认使用 `double`**，只有在内存非常紧张时（比如大量数据、嵌入式设备）才考虑 `float`。

```c
#include <stdio.h>

int main(void)
{
    double a = 0.1 + 0.2;
    printf("0.1 + 0.2 = %f\n", a);
    printf("0.1 + 0.2 = %.17f\n", a);
    if (a == 0.3) {
        printf("等于 0.3\n");
    } else {
        printf("不等于 0.3\n");
    }

    float f = 16777216.0f;
    printf("float: 16777216 + 1 = %.1f\n", f + 1.0f);
    float price = 0.1f;
    double precise = 0.1;
    printf("float 0.1 = %.20f\n", price);
    printf("double 0.1 = %.20f\n", precise);
    return 0;
}
```

运行结果：

```
0.1 + 0.2 = 0.300000
0.1 + 0.2 = 0.30000000000000004
不等于 0.3
float: 16777216 + 1 = 16777216.0
float 0.1 = 0.10000000149011611938
double 0.1 = 0.10000000000000000555
```

- 浮点数用二进制表示小数，而 0.1 在二进制中是一个**无限循环小数**（就像 1/3 在十进制中是 0.333...），只能存储一个近似值。`%f` 默认只显示 6 位小数，看起来是 0.300000，显示 17 位就露出了误差。
- 所以 **不要用 `==` 比较两个浮点数**。应该判断它们的差是否足够小：`fabs(a - 0.3) < 1e-9`（`fabs` 在 `<math.h>` 中）。
- `float` 的精度很有限：16777216（2²⁴）加 1 之后还是 16777216，因为 `float` 已经无法表示 16777217 了。
- **涉及金额的计算不要用浮点数**。可以用整数表示「分」，比如 9.99 元存为 999。
- 写 `float` 类型的常量时加后缀 `f`，比如 `0.1f`。不加后缀的 `0.1` 是 `double`。

## 5. 字符类型

`char` 用来存放一个字符，字符常量用**单引号**括起来。但 `char` 本质上是一个 1 字节的**整数**，存放的是字符的编码（ASCII 码）：

```c
char c = 'A';
printf("'A' 的值是 %d，'A' + 1 = %c\n", c, c + 1);
```

```
'A' 的值是 65，'A' + 1 = B
```

用 `%d` 输出得到它的编码 65，用 `%c` 输出得到字符。`'A' + 1` 是 66，对应的字符是 `B`。利用这一点，可以做很多字符处理，比如 `c - '0'` 把字符 `'7'` 转换为整数 7，`c - 'a' + 'A'` 把小写字母转换为大写。

注意 `'A'`（单引号，一个字符）和 `"A"`（双引号，一个字符串）是不同的，字符串会在第 1.10 篇介绍。中文字符在 UTF-8 编码中占 3 个字节，一个 `char` 放不下。

## 6. 类型转换

不同类型的值一起运算时，会发生**类型转换**。

```c
#include <stdio.h>

int main(void)
{
    int a = 7, b = 2;
    printf("7 / 2 = %d\n", a / b);                      // 整数除法
    printf("7 / 2.0 = %f\n", a / 2.0);                  // 有一个是浮点数
    printf("(double)7 / 2 = %f\n", (double)a / b);      // 强制类型转换

    int truncated = 3.99;                               // 小数部分被直接丢弃
    printf("int x = 3.99 → %d\n", truncated);
    int negative = -3.99;
    printf("int y = -3.99 → %d\n", negative);

    char c = 'A';
    printf("'A' 的值是 %d，'A' + 1 = %c\n", c, c + 1);

    int n = -1;
    unsigned int m = 1;
    if (n < m) {
        printf("-1 < 1\n");
    } else {
        printf("-1 >= 1 ？！\n");
    }
    return 0;
}
```

编译时的警告：

```
convert.c:10:21: warning: implicit conversion from 'double' to 'int' changes value from 3.99 to 3 [-Wliteral-conversion]
convert.c:12:20: warning: implicit conversion from 'double' to 'int' changes value from -3.99 to -3 [-Wliteral-conversion]
convert.c:20:11: warning: comparison of integers of different signs: 'int' and 'unsigned int' [-Wsign-compare]
```

运行结果：

```
7 / 2 = 3
7 / 2.0 = 3.500000
(double)7 / 2 = 3.500000
int x = 3.99 → 3
int y = -3.99 → -3
'A' 的值是 65，'A' + 1 = B
-1 >= 1 ？！
```

规则：

1. **两个整数相除，结果还是整数**，小数部分被丢弃。`7 / 2` 是 3，不是 3.5。只要有一个操作数是浮点数，就按浮点数计算。
2. **强制类型转换** `(类型)值` 可以显式地转换类型。`(double)a / b` 先把 `a` 转换为 `double`，再做除法。
3. **浮点数转换为整数时，小数部分被直接截断**（向 0 取整），不是四舍五入。3.99 变成 3，-3.99 变成 -3。需要四舍五入时用 `<math.h>` 中的 `round`。
4. **有符号数和无符号数一起运算时，有符号数会被转换为无符号数**。-1 转换为 `unsigned int` 是 4294967295，所以 `-1 < 1u` 是假！这是 C 语言中最隐蔽的陷阱之一，`-Wall -Wextra` 会给出 `-Wsign-compare` 警告。**尽量避免混用有符号和无符号类型**。

## 7. 常量

不应该被修改的值，用 **`const`** 定义：

```c
const double PI = 3.14159265358979;
const int MAX_STUDENTS = 50;
PI = 3.0;                   // 编译错误：不能给 const 变量赋值
```

用有名字的常量代替代码中直接出现的数字（被称为「魔法数字」），代码更容易读懂，修改时也只需要改一处。另一种定义常量的方式是 `#define PI 3.14159`，它是预处理器的文本替换，会在第 1.13 篇介绍。

常量的写法：

| 写法 | 类型 / 含义 |
|---|---|
| `42` | `int` |
| `42L`、`42LL` | `long`、`long long` |
| `42U` | `unsigned int` |
| `0x2A`、`052` | 十六进制、八进制的 42 |
| `3.14` | `double` |
| `3.14f` | `float` |
| `1e-9` | 科学计数法，1 × 10⁻⁹ |
| `'A'`、`'\n'` | 字符 |

## 8. 常见错误

1. **使用未初始化的变量**：值是不确定的。定义时就初始化。
2. **整数溢出**：选择足够大的类型；乘法之前先转换为大类型，而不是在结果上转换。
3. **用 `==` 比较浮点数**：用差的绝对值和一个很小的数比较。
4. **用浮点数计算金额**：用整数表示最小单位。
5. **整数除法丢失小数**：`1 / 2` 是 0。需要小数时，让其中一个操作数成为浮点数。
6. **以为浮点数转换为整数会四舍五入**：实际是截断。
7. **混用有符号和无符号类型**：`-1 < 1u` 为假。
8. **假设 `long` 一定是 8 字节**：在 Windows 上是 4 字节。需要确定大小时用 `int64_t`。

## 9. 小结

- 变量要先定义后使用；函数内的变量不会自动初始化，值是不确定的
- 整数类型有 `char`、`short`、`int`、`long`、`long long` 及其 `unsigned` 版本；`int` 是 4 字节，约 ±21 亿；需要固定大小时用 `int32_t`、`int64_t`
- 无符号溢出会回绕；**有符号溢出是未定义行为**，开启优化后实测同一个函数返回了不同的结果；用 `-fsanitize=undefined` 检查
- `double` 比 `float` 精确，默认使用 `double`；浮点数有误差，`0.1 + 0.2 != 0.3`，不能用 `==` 比较
- `char` 本质上是存放字符编码的小整数
- 整数相除结果是整数；浮点转整数是截断；有符号和无符号比较时，负数会变成很大的正数
- 不该修改的值用 `const` 定义

## 10. 练习

1. 计算 100 年有多少秒（每年按 365 天计算），分别用 `int` 和 `long long` 保存结果并输出，解释为什么结果不同。
2. 不运行程序，判断 `printf("%f %f\n", 5 / 2 * 2.0, 5 / 2.0 * 2);` 的输出。
3. 写一个程序，判断 `0.1 + 0.2` 和 `0.3` 是否「近似相等」（差的绝对值小于 10⁻⁹）。

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```c
   int years = 100;
   int seconds = years * 365 * 24 * 3600;
   long long seconds2 = (long long)years * 365 * 24 * 3600;
   printf("int: %d，long long: %lld\n", seconds, seconds2);
   ```

   输出：

   ```
   int: -1141367296，long long: 3153600000
   ```

   正确的结果是 31.5 亿，超过了 `int` 的最大值（约 21 亿），发生了溢出。注意编译器没有给出任何警告。`long long` 版本中，第一步 `(long long)years` 就已经转换成了 `long long`，后面的每一次乘法都按 `long long` 计算。

2. 输出 `4.000000 5.000000`。第一个表达式从左往右计算，`5 / 2` 是整数除法，结果是 2，再乘以 2.0 得到 4.0；第二个表达式中 `5 / 2.0` 是 2.5，乘以 2 得到 5.0。

3. 参考代码：

   ```c
   #include <math.h>
   #include <stdio.h>
   int main(void)
   {
       double a = 0.1 + 0.2;
       if (fabs(a - 0.3) < 1e-9) {
           printf("近似相等\n");
       }
       return 0;
   }
   ```

   在 Linux 上使用 `<math.h>` 中的函数时，可能需要在编译命令的最后加上 `-lm`，链接数学库。

</details>
