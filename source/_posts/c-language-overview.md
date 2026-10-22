---
title: C 语言概述与第一个程序
date: 2026-10-15 10:00:00
updated: 2026-10-15 10:00:00
categories:
  - C/C++
  - C语言基础
tags:
  - C语言
  - 入门
order: 1
description: C 语言的特点、开发环境搭建，以及从源代码到可执行程序的完整过程。
top_group_list: true
---

## 1. 为什么学 C 语言

C 语言诞生于 1972 年，至今仍是操作系统、嵌入式、数据库、网络服务器等底层软件的主力语言。Linux 内核、Redis、Nginx 都是用 C 写的。

学习 C 语言的收获：

- **理解计算机的工作方式**：指针、内存布局、栈和堆，这些概念在 C 中是直接暴露的
- **为 C++ 打基础**：C++ 几乎完全兼容 C 的语法
- **学习数据结构和系统编程的通用语言**：本站的数据结构、Linux 系统编程专栏都使用 C/C++

## 2. 第一个程序

```c
#include <stdio.h>

int main(void)
{
    printf("Hello, World!\n");
    return 0;
}
```

逐行解释：

| 代码 | 含义 |
| --- | --- |
| `#include <stdio.h>` | 引入标准输入输出头文件，`printf` 声明在这里 |
| `int main(void)` | 程序入口，返回值类型为 `int` |
| `printf(...)` | 向标准输出打印字符串，`\n` 表示换行 |
| `return 0;` | 返回 0 表示程序正常结束 |

## 3. 编译与运行

在 Linux 或 macOS 终端中：

```bash
gcc hello.c -o hello   # 编译，生成可执行文件 hello
./hello                # 运行
```

输出：

```
Hello, World!
```

> 从 `hello.c` 到 `hello` 实际经历了预处理、编译、汇编、链接四个阶段，详见 Linux 专栏的《gcc 编译的四个阶段》。

## 4. 小结

- C 语言程序从 `main` 函数开始执行
- 使用 `#include` 引入头文件
- 使用 `gcc` 编译源文件得到可执行程序
