---
title: 函数
date: 2026-10-23 15:40:00
updated: 2026-10-23 15:40:00
categories:
  - C/C++
  - C语言基础
tags:
  - C语言
  - 函数
  - 递归
order: 6
description: 函数把程序拆分成一个个独立的小块。本文介绍函数的声明、定义和调用，解释为什么按值传递的 swap 交换不了实参，实测递归调用在栈上的布局、递归斐波那契的指数级调用次数和栈溢出，并讲解局部变量、全局变量和 static 局部变量的区别。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会定义、声明和调用函数；理解按值传递；理解函数调用栈，会写递归函数并知道它的代价；分清局部变量、全局变量、静态局部变量的作用域和生命周期。
>
> **前置知识**：《流程控制》。
>
> **编译环境**：`gcc -std=c11 -Wall -Wextra`。

## 1. 定义、声明和调用

把一段完成特定任务的代码包装起来，起一个名字，这就是**函数**。前面一直在用的 `printf` 和 `main` 都是函数。

```c
#include <stdio.h>

double average(int a, int b, int c);      // 函数声明（原型）

int main(void)
{
    double avg = average(90, 85, 78);     // 调用
    printf("平均分: %.2f\n", avg);
    return 0;
}

double average(int a, int b, int c)       // 函数定义
{
    return (a + b + c) / 3.0;
}
```

```
平均分: 84.33
```

- **定义**：`返回类型 函数名(参数列表) { 函数体 }`。`return` 结束函数，并把值返回给调用者。不需要返回值时，返回类型写 `void`；不需要参数时，参数列表写 `void`。
- **调用**：`average(90, 85, 78)`。调用时传入的 `90, 85, 78` 叫**实参**，函数定义中的 `a, b, c` 叫**形参**。
- **声明**：只有函数头、以分号结尾，告诉编译器「有这样一个函数，它的参数和返回值是这些类型」。

### 1.1 为什么需要声明

C 编译器**从上到下**读取代码。如果去掉上面的声明，`main` 中调用 `average` 时，编译器还没有见过它：

```
nodecl.c:4:22: error: call to undeclared function 'average'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
nodecl.c:4:22: warning: format specifies type 'double' but the argument has type 'int' [-Wformat]
nodecl.c:7:8: error: conflicting types for 'average'
```

在古老的 C89 中，编译器会假设未声明的函数返回 `int`，这正是第二、三条信息的由来：它以为 `average` 返回 `int`，后来又看到了返回 `double` 的定义，两者冲突了。C99 起这种「隐式声明」不再被允许。

解决方法有两种：把函数定义写在调用之前；或者像上面那样，在文件开头写出声明。当程序由多个文件组成时，声明会放在**头文件**中，`#include <stdio.h>` 就是在包含 `printf` 等函数的声明（第 1.13 篇会详细讲）。

### 1.2 为什么要写函数

- **复用**：同样的逻辑只写一次，在多处调用。
- **分解**：把复杂的问题拆成几个简单的小问题，每个函数只做一件事。
- **可读**：一个好的函数名就是最好的注释，`if (isPrime(n))` 比一段循环代码清楚得多。

经验法则：一个函数超过一屏（大约 50 行），就应该考虑把它拆分。

## 2. 按值传递

```c
#include <stdio.h>

void swap(int a, int b)
{
    int t = a;
    a = b;
    b = t;
    printf("  swap 内部: a = %d, b = %d\n", a, b);
}

int main(void)
{
    int x = 1, y = 2;
    swap(x, y);
    printf("swap 之后: x = %d, y = %d\n", x, y);
    return 0;
}
```

运行结果：

```
  swap 内部: a = 2, b = 1
swap 之后: x = 1, y = 2
```

函数内部确实交换了，但 `x` 和 `y` 没有变。原因是：**C 语言的参数是按值传递的**，调用 `swap(x, y)` 时，`x` 和 `y` 的值被**复制**给了形参 `a` 和 `b`。`a`、`b` 是 `swap` 自己的局部变量，和 `x`、`y` 是不同的内存，修改它们不会影响 `x`、`y`。

那么函数怎样才能修改调用者的变量呢？答案是传递变量的**地址**，这正是 `scanf("%d", &age)` 中要写 `&` 的原因。这需要用到指针，第 1.8 篇会用指针实现一个真正的 `swap`。

### 2.1 不要漏掉 return

```c
int sign(int x)
{
    if (x > 0) return 1;
    if (x < 0) return -1;
}                                   // x == 0 时没有 return
```

```
noret.c:6:1: warning: non-void function does not return a value in all control paths [-Wreturn-type]
```

调用 `sign(0)`，分别用 `-O0` 和 `-O2` 编译：

```
-O0: sign(5) = 1, sign(0) = 1
-O2: sign(5) = 1, sign(0) = -1
```

两次的结果不一样，而且都是错的（应该返回 0）。有返回值的函数执行到末尾却没有 `return`，然后调用者使用了它的返回值，这是**未定义行为**。编译器给出了警告，一定要修复它。

## 3. 函数调用栈

每次调用函数时，系统会在一块叫作**栈**（stack）的内存区域中，为这次调用分配一块空间，叫作**栈帧**，用来存放它的参数、局部变量和返回地址。函数返回时，这块空间被释放。

用一个递归函数来观察栈帧：

```c
#include <stdio.h>

int depth = 0;

long long factorial(int n)
{
    int local = n;                         // 每一层调用都有自己的局部变量
    printf("%*s进入 factorial(%d)，local 的地址 %p\n", depth * 2, "", n, (void*)&local);
    ++depth;
    long long result = (n <= 1) ? 1 : n * factorial(n - 1);
    --depth;
    printf("%*s返回 factorial(%d) = %lld\n", depth * 2, "", n, result);
    return result;
}

int main(void)
{
    factorial(4);
    return 0;
}
```

运行结果：

```
进入 factorial(4)，local 的地址 0x16d5d6548
  进入 factorial(3)，local 的地址 0x16d5d64e8
    进入 factorial(2)，local 的地址 0x16d5d6488
      进入 factorial(1)，local 的地址 0x16d5d6428
      返回 factorial(1) = 1
    返回 factorial(2) = 2
  返回 factorial(3) = 6
返回 factorial(4) = 24
```

（`%*s` 用参数 `depth * 2` 作为宽度输出一个空字符串，实现缩进。）

- 一个函数**调用它自己**，叫作**递归**。`factorial(4)` 调用 `factorial(3)`，一直到 `factorial(1)` 不再递归，直接返回 1，这是**递归的出口**。然后各层**按相反的顺序**依次返回。
- 每一层调用都有**自己的** `local` 变量，地址各不相同。
- 地址**逐层递减**，每层相差 0x60 = 96 字节，这就是一个栈帧的大小。在大多数系统上，栈是从高地址向低地址增长的。

### 3.1 递归的代价：重复计算

斐波那契数列：F(0) = 0，F(1) = 1，F(n) = F(n-1) + F(n-2)。直接按定义写成递归非常自然，但效率如何？

```c
long long calls = 0;

long long fibRec(int n)                    // 递归版本
{
    ++calls;
    if (n <= 1) {
        return n;                          // 递归的出口
    }
    return fibRec(n - 1) + fibRec(n - 2);
}

long long fibLoop(int n)                   // 循环版本
{
    long long prev = 0, cur = 1;
    if (n == 0) return 0;
    for (int i = 2; i <= n; ++i) {
        long long next = prev + cur;
        prev = cur;
        cur = next;
    }
    return cur;
}
```

用 `-O2` 编译，测量耗时（`clock()` 来自 `<time.h>`）：

```
fibRec(10) = 55，调用了 177 次，耗时 0.0 ms
fibRec(20) = 6765，调用了 21891 次，耗时 0.1 ms
fibRec(30) = 832040，调用了 2692537 次，耗时 10.7 ms
fibRec(40) = 102334155，调用了 331160281 次，耗时 287.4 ms
fibLoop(90) = 2880067194370816120，耗时 0.000 ms
```

n 每增加 10，调用次数就增加 **100 多倍**。计算 F(40) 调用了 3.3 亿次！原因是大量的**重复计算**：`fibRec(40)` 调用 `fibRec(39)` 和 `fibRec(38)`，而 `fibRec(39)` 又会再计算一遍 `fibRec(38)`……调用次数随 n 呈**指数增长**。循环版本只需要 n 次加法，计算 F(90) 都不到 0.001 毫秒。

递归适合**问题本身就是递归结构**、而且子问题不重复的场景，比如遍历树（数据结构专栏会大量使用）、快速排序、汉诺塔。当子问题重复时，要改用循环或者把计算过的结果保存起来。

### 3.2 栈溢出

栈的大小是有限的。在 macOS 和 Linux 上，`ulimit -s` 显示默认的栈大小，这台机器上是 8176 KB，大约 8 MB。递归太深会怎样？

```c
long long sumTo(long long n)               // 1 + 2 + ... + n，递归实现
{
    if (n == 0) {
        return 0;
    }
    return n + sumTo(n - 1);
}

int main(void)
{
    printf("sumTo(10000) = %lld\n", sumTo(10000));
    fflush(stdout);
    printf("sumTo(10000000) = %lld\n", sumTo(10000000));
    return 0;
}
```

```
sumTo(10000) = 50005000
```

第二次调用要递归 1000 万层，每层一个栈帧，远远超过了 8 MB，程序崩溃，退出码 139（段错误）。这就是**栈溢出**（stack overflow）。用 AddressSanitizer 可以看得更清楚：

```
ERROR: AddressSanitizer: stack-overflow on address 0x00016efdbff0 (pc 0x000100628814 bp 0x00016efdc000 sp 0x00016efdbfe0 T0)
    #0 0x000100628814 in sumTo deep.c:4
```

（`fflush(stdout)` 让第一行输出在崩溃之前就被写出去。）同样的道理，**不要在函数中定义很大的局部数组**，比如 `int big[10000000];`（40 MB），它也会直接撑爆栈。大块内存应该用第 1.11 篇介绍的动态内存分配。

**忘了写递归的出口**，或者出口永远达不到，就会无限递归，结果也是栈溢出。

## 4. 作用域和生命周期

```c
#include <stdio.h>

int total = 0;                             // 全局变量：所有函数都能访问

void counter(void)
{
    static int calls = 0;                  // 静态局部变量：只初始化一次，值在调用之间保留
    int temp = 0;                          // 普通局部变量：每次调用都重新创建
    ++calls;
    ++temp;
    ++total;
    printf("calls = %d, temp = %d, total = %d\n", calls, temp, total);
}

int main(void)
{
    counter();
    counter();
    counter();

    int x = 1;
    {
        int x = 2;                         // 内层的 x 隐藏了外层的 x
        printf("内层 x = %d\n", x);
    }
    printf("外层 x = %d\n", x);
    return 0;
}
```

运行结果：

```
calls = 1, temp = 1, total = 1
calls = 2, temp = 1, total = 2
calls = 3, temp = 1, total = 3
内层 x = 2
外层 x = 1
```

| | 局部变量 | 静态局部变量 | 全局变量 |
|---|---|---|---|
| 定义位置 | 函数或代码块内 | 函数内，加 `static` | 所有函数之外 |
| 作用域（在哪里能用） | 定义它的代码块 | 定义它的代码块 | 整个文件（其他文件用 `extern` 声明后也能用） |
| 生命周期（什么时候存在） | 进入代码块时创建，离开时销毁 | 整个程序运行期间 | 整个程序运行期间 |
| 存放位置 | 栈 | 静态存储区 | 静态存储区 |
| 默认初始值 | **不确定** | 0 | 0 |

- **局部变量**每次调用都重新创建，所以 `temp` 每次都是 1。
- **静态局部变量** `calls` 只在第一次执行到时初始化，之后一直存在，值在多次调用之间保留。它的作用域仍然只在函数内部，外面访问不到。
- **全局变量** `total` 在任何函数中都能访问和修改。
- 内层代码块中定义的同名变量会**隐藏**外层的变量。这容易引起混淆，应该避免。

**尽量少用全局变量**。任何函数都能修改它，出了问题很难找到是谁改的；函数依赖全局变量，也就很难单独测试和复用。需要在多次调用之间保持状态时，优先考虑静态局部变量或者通过参数传递。

> `static` 用在全局变量和函数上时含义不同：表示「只在当前文件中可见」。这会在第 1.13 篇多文件编程中介绍。

## 5. 常见错误

1. **调用之前没有声明**：C99 起是编译错误。写好声明，或者把定义放在调用之前。
2. **以为函数能修改实参**：按值传递修改的是副本，需要传地址。
3. **有返回值的函数漏写 `return`**：未定义行为，实测 `-O0` 和 `-O2` 返回了不同的值。
4. **递归没有出口，或者递归太深**：栈溢出，程序崩溃。
5. **有重复子问题的递归**：调用次数指数增长，改用循环。
6. **在函数中定义很大的局部数组**：栈溢出。
7. **滥用全局变量**：难以排查、难以复用。
8. **使用未初始化的局部变量**：局部变量不会自动初始化为 0，而全局变量和静态变量会。

## 6. 小结

- 函数要先声明后调用；声明告诉编译器函数的参数和返回类型
- C 语言按值传递参数，函数修改形参不会影响实参
- 有返回值的函数在所有路径上都要 `return`
- 每次函数调用在栈上分配一个栈帧；递归的每一层都有自己的局部变量
- 递归要有出口；子问题重复时调用次数会指数增长（F(40) 调用了 3.3 亿次）；递归太深会栈溢出
- 局部变量在栈上，每次调用重新创建，不会自动初始化；静态局部变量只初始化一次，值在调用之间保留；全局变量到处可见，尽量少用

## 7. 练习

1. 写一个函数 `int isPrime(int n)`，判断 `n` 是否是质数，并用它输出 30 以内的所有质数。
2. 用递归实现求最大公约数的辗转相除法：`gcd(a, b) = gcd(b, a % b)`，当 `b` 为 0 时结果是 `a`。计算 `gcd(48, 18)`。
3. 下面的程序输出什么？

   ```c
   int next(void)
   {
       static int n = 10;
       return n++;
   }

   int main(void)
   {
       int a = next();
       int b = next();
       int c = next();
       printf("%d %d %d\n", a, b, c);
   }
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```c
   int isPrime(int n)
   {
       if (n < 2) {
           return 0;
       }
       for (int d = 2; d * d <= n; ++d) {
           if (n % d == 0) {
               return 0;
           }
       }
       return 1;
   }
   ```

   输出 `2 3 5 7 11 13 17 19 23 29`。把判断写成函数后，找到因数时可以直接 `return 0`，不再需要上一篇中的标志变量和 `break`。

2. 参考代码：

   ```c
   int gcd(int a, int b)
   {
       if (b == 0) {
           return a;
       }
       return gcd(b, a % b);
   }
   ```

   `gcd(48, 18)` → `gcd(18, 12)` → `gcd(12, 6)` → `gcd(6, 0)` → 6。每一步 `b` 都至少减小一半左右，递归层数很少，不用担心栈溢出。

3. 输出 `10 11 12`。`n` 是静态局部变量，只在第一次调用时初始化为 10，之后每次调用都在上一次的基础上加 1。`return n++` 返回的是加 1 之前的值。注意这里把三次调用写成了三条语句，如果写成 `printf("%d %d %d\n", next(), next(), next())`，C 语言没有规定三个参数的求值顺序，输出的顺序就不确定了。

</details>
