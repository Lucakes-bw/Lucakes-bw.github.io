---
title: C 语言概述与第一个程序
date: 2026-10-15 10:00:00
updated: 2026-10-23 14:00:00
categories:
  - C/C++
  - C语言基础
tags:
  - C语言
  - 入门
order: 1
description: C 语言从哪里来、为什么今天还值得学。本文逐行讲解第一个 C 程序，演示编译和运行的过程，理解 main 函数的返回值，并学会阅读最常见的三种错误信息。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：了解 C 语言的历史和用途；能逐行解释 Hello World 程序；会用 gcc 编译和运行 C 程序；理解 `main` 函数的返回值；能读懂缺少分号、缺少头文件和链接错误三种常见的错误信息。
>
> **前置知识**：无。这是整个 C/C++ 专栏的第一篇。
>
> **编译环境**：`gcc -std=c11 -Wall -Wextra`。还没有安装编译器的话，先看《开发环境与编程工具》的第 2 节。

## 1. C 语言是什么

C 语言诞生于 1972 年的贝尔实验室，作者是 Dennis Ritchie。它最初是为了编写 **UNIX 操作系统**而设计的。半个世纪过去了，C 语言仍然是最重要的编程语言之一：

- **操作系统**：Linux、Windows、macOS 的内核主要是用 C 写的。
- **嵌入式和硬件**：单片机、路由器、汽车电子，几乎都在用 C。
- **基础软件**：Python 解释器（CPython）、Redis、SQLite、Nginx、Git 都是用 C 写的。
- **其他语言的基础**：C++、Java、C#、Go 的语法都深受 C 的影响。学会 C，再学这些语言会轻松得多。

C 语言的特点可以概括为两句话：

1. **贴近硬件，效率高**。C 语言可以直接操作内存地址，编译后的程序几乎和手写的汇编一样快。
2. **简单，但不安全**。C 语言的关键字只有几十个，但它几乎不做任何检查：数组越界、使用未初始化的变量、访问已经释放的内存，都不会报错，而是产生难以预料的结果。**学 C 语言，一半是学语法，一半是学怎么避开这些陷阱**。本章的每一篇都会实际复现这些陷阱。

C 语言有几个标准版本：C89/C90、C99、**C11**、C17、C23。本章使用 C11，它被所有主流编译器完整支持。

## 2. 第一个程序

新建一个文件 `hello.c`（C 语言源文件的扩展名是 `.c`），输入：

```c
#include <stdio.h>

int main(void)
{
    printf("Hello, C!\n");
    return 0;
}
```

逐行解释：

- **`#include <stdio.h>`**：包含标准输入输出库的头文件。`printf` 函数是在这个头文件中**声明**的，不包含它，编译器就不认识 `printf`。以 `#` 开头的行是**预处理指令**，在真正编译之前处理。
- **`int main(void)`**：定义 `main` 函数。**每个 C 程序都从 `main` 函数开始执行**。`int` 表示这个函数返回一个整数，`void` 表示它不接受任何参数。
- **`{` 和 `}`**：函数体的开始和结束。
- **`printf("Hello, C!\n");`**：调用 `printf` 函数输出一行文字。`\n` 是**换行符**，不写的话光标会停在这一行的末尾。**每条语句以分号 `;` 结束**。
- **`return 0;`**：结束 `main` 函数，并把 0 返回给操作系统，表示程序**正常结束**。

> C 语言区分大小写：`main` 和 `Main` 是两个不同的名字，`printf` 也不能写成 `Printf`。

## 3. 编译和运行

C 语言是**编译型语言**：源代码要先由**编译器**翻译成机器能执行的程序，然后才能运行。在终端中进入 `hello.c` 所在的目录，执行：

```bash
gcc -std=c11 -Wall -Wextra hello.c -o hello
./hello
```

```
Hello, C!
```

- `gcc` 是编译器，`hello.c` 是源文件，`-o hello` 指定生成的程序叫 `hello`。如果不写 `-o`，程序默认叫 `a.out`。
- `-std=c11` 指定使用 C11 标准；`-Wall -Wextra` 打开警告，帮助发现代码中的问题。**建议始终加上**。
- `./hello` 运行当前目录下的 `hello`。在 Windows 上生成的是 `hello.exe`，运行时输入 `hello.exe` 或 `.\hello.exe`。

编译的背后其实有预处理、编译、汇编、链接四个步骤，《开发环境与编程工具》的第 1 节用实际的输出详细展示了每一步。这里只需要记住：**修改了源代码之后，要重新编译才能看到效果**。

### 3.1 main 的返回值

`main` 返回的值会交给操作系统，叫作程序的**退出码**。在终端中可以用 `echo $?` 查看上一个程序的退出码（Windows 的 PowerShell 中用 `echo $LASTEXITCODE`）：

```bash
./hello
echo $?
```

```
Hello, C!
0
```

把 `return 0;` 改成 `return 1;`，重新编译运行：

```c
#include <stdio.h>

int main(void)
{
    printf("程序出错了，返回 1\n");
    return 1;
}
```

```bash
./ret
echo $?
```

```
程序出错了，返回 1
1
```

按照约定，**0 表示成功，非 0 表示失败**。其他程序（比如 shell 脚本、Makefile）就是根据这个值判断程序是否执行成功的：

```bash
./ret && echo "成功" || echo "失败"
```

```
程序出错了，返回 1
失败
```

> `main` 函数是唯一一个可以省略 `return` 的函数：C99 起，`main` 执行到末尾时自动返回 0。不过本章的示例都会明确写出 `return 0;`。

## 4. 再写一个程序：变量和注释

```c
#include <stdio.h>

/* 计算圆的面积 */
int main(void)
{
    double radius = 2.5;                        // 半径
    double area = 3.14159 * radius * radius;    // 面积 = πr²
    printf("半径为 %.1f 的圆，面积是 %.2f\n", radius, area);
    return 0;
}
```

运行结果：

```
半径为 2.5 的圆，面积是 19.63
```

- **`double radius = 2.5;`** 定义了一个**变量**：`double` 是它的类型（带小数的数），`radius` 是它的名字，`2.5` 是初始值。下一篇会详细介绍各种类型。
- **`/* ... */`** 和 **`//`** 是**注释**，编译器会忽略它们。`/* */` 可以跨越多行，`//` 注释到这一行的末尾（C99 起支持）。注释是写给人看的，用来解释代码**为什么**这样写。
- **`printf` 中的 `%.1f` 和 `%.2f`** 是**格式说明符**，分别被后面的 `radius` 和 `area` 替换，`.1`、`.2` 表示保留几位小数。第 1.4 篇会详细介绍。

## 5. 读懂错误信息

初学时最常遇到的不是逻辑错误，而是编译错误。**编译器的错误信息会告诉你出错的文件、行号和原因**，一定要学会读它。

### 5.1 缺少分号

```c
printf("Hello, C!\n")         // 少了分号
return 0;
```

```
nosemi.c:5:26: error: expected ';' after expression
    5 |     printf("Hello, C!\n")
      |                          ^
      |                          ;
1 error generated.
```

`nosemi.c:5:26` 表示**第 5 行第 26 列**。编译器甚至直接指出了应该在 `^` 的位置加上 `;`。

### 5.2 缺少头文件

把第一行的 `#include <stdio.h>` 删掉：

```
noinc.c:3:5: error: call to undeclared library function 'printf' with type 'int (const char *, ...)'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]
noinc.c:3:5: note: include the header <stdio.h> or explicitly provide a declaration for 'printf'
```

编译器不认识 `printf`，并且在 `note` 中提示应该包含 `<stdio.h>`。**`error` 说明问题是什么，`note` 往往给出了解决方法**。（有些较旧的编译器在这里只给出警告，程序仍然能编译，所以更要重视警告。）

### 5.3 找不到 main

把 `main` 写成了 `Main`：

```
Undefined symbols for architecture arm64:
  "_main", referenced from:
      <initial-undefines>
ld: symbol(s) not found for architecture arm64
```

这次的错误信息中没有行号，而且提到了 `ld`。这是**链接错误**：每个程序都必须有一个 `main` 函数作为入口，链接器找不到它。在 Linux 的 GCC 上，这个错误显示为 `undefined reference to 'main'`。

**看错误信息的技巧**：

1. **从第一个错误开始改**。一个错误常常会引发后面一连串的错误，改掉第一个，后面的可能就消失了。
2. 错误的**实际位置可能在报告位置的前面**。比如缺少分号，有时会在下一行报错。
3. 看不懂的英文错误信息，复制到搜索引擎中搜索，几乎都能找到答案。

## 6. 常见错误

1. **漏写分号**：每条语句都要以 `;` 结尾，但 `#include` 行和函数头 `int main(void)` 后面不加分号。
2. **使用中文标点**：中文的分号 `；`、括号 `（）`、引号 `“”` 会导致编译错误。代码中的符号必须是英文半角的。
3. **忘记包含头文件**：使用 `printf` 要 `#include <stdio.h>`。
4. **大小写写错**：`Main`、`Printf`、`Return` 都是错误的。
5. **修改代码后忘记重新编译**：运行的还是旧程序。
6. **`printf` 中忘记写 `\n`**：下一次输出会接在同一行，终端的提示符也可能出现在输出的后面。

## 7. 小结

- C 语言诞生于 1972 年，广泛用于操作系统、嵌入式和基础软件，特点是高效、简单，但几乎不做安全检查
- 每个 C 程序从 `main` 函数开始执行；`#include <stdio.h>` 引入 `printf` 的声明；语句以分号结尾
- `gcc -std=c11 -Wall -Wextra hello.c -o hello` 编译，`./hello` 运行
- `main` 的返回值是程序的退出码，0 表示成功，非 0 表示失败，可以用 `echo $?` 查看
- 注释有 `/* */` 和 `//` 两种
- 错误信息给出了文件名、行号、列号和原因；从第一个错误开始改；提到 `ld` 的是链接错误

## 8. 练习

1. 修改第 4 节的程序，同时输出圆的面积和周长（周长 = 2πr）。
2. 用 `printf` 输出下面的图案：

   ```
   *****
   *   *
   *****
   ```

3. 下面的程序有两处错误，编译器会报告什么？

   ```c
   #include <stdio.h>
   int main(void)
   {
       int x = 3;
       printf("%d\n", x * 2)
       return 0
   }
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```c
   double radius = 2.5;
   double area = 3.14159 * radius * radius;
   double circumference = 2 * 3.14159 * radius;
   printf("面积 %.2f，周长 %.2f\n", area, circumference);
   ```

   输出 `面积 19.63，周长 15.71`。

2. 每一行用一个 `printf`：

   ```c
   printf("*****\n");
   printf("*   *\n");
   printf("*****\n");
   ```

3. 两处都是缺少分号，编译器报告两个错误：

   ```
   ex2.c:5:26: error: expected ';' after expression
   ex2.c:6:13: error: expected ';' after return statement
   2 errors generated.
   ```

   分别在 `printf(...)` 和 `return 0` 后面加上分号即可。

</details>
