---
title: gcc 编译的四个阶段
date: 2026-10-22 10:00:00
updated: 2026-10-22 10:00:00
categories:
  - Linux基础
  - 开发工具
tags:
  - Linux
  - gcc
order: 1
description: 预处理、编译、汇编、链接，一步步看 hello.c 如何变成可执行程序，以及 gcc 常用参数。
cover: /img/covers/linux.svg
top_group_list: true
---

## 1. 四个阶段

```
hello.c  --预处理-->  hello.i  --编译-->  hello.s  --汇编-->  hello.o  --链接-->  hello
 源文件               展开后的源文件       汇编代码            目标文件            可执行文件
```

| 阶段 | 命令 | 做了什么 |
| --- | --- | --- |
| 预处理 | `gcc -E hello.c -o hello.i` | 展开 `#include`、替换宏、删除注释、处理条件编译 |
| 编译 | `gcc -S hello.i -o hello.s` | 语法检查，把 C 代码翻译成汇编代码 |
| 汇编 | `gcc -c hello.s -o hello.o` | 把汇编代码翻译成机器码，生成目标文件 |
| 链接 | `gcc hello.o -o hello` | 合并目标文件和库，生成可执行文件 |

> 记忆技巧：参数是 **-E -S -c**，生成文件后缀是 **.i .s .o**。

## 2. 动手试一试

```c
// hello.c
#include <stdio.h>
#define MSG "Hello, gcc!"

int main(void)
{
    printf("%s\n", MSG);   // 预处理后 MSG 会被替换为字符串
    return 0;
}
```

```bash
gcc -E hello.c -o hello.i && tail -n 8 hello.i   # 宏已经被替换
gcc -S hello.i -o hello.s && head hello.s        # 查看汇编
gcc -c hello.s -o hello.o && file hello.o        # ELF relocatable
gcc hello.o -o hello && ./hello
```

## 3. 常用参数

| 参数 | 作用 |
| --- | --- |
| `-o file` | 指定输出文件名 |
| `-Wall` | 打开常用警告 |
| `-g` | 生成调试信息，配合 gdb 使用 |
| `-O0` / `-O2` | 优化级别 |
| `-I dir` | 指定头文件搜索目录 |
| `-L dir -l name` | 指定库目录和要链接的库（`libname.so` / `libname.a`） |
| `-std=c11` | 指定语言标准 |

```bash
gcc -Wall -g -std=c11 main.c util.c -I include -o app
```

## 4. 小结

- 平时一条 `gcc main.c -o main` 就完成了四个阶段
- 编译报错一般在**编译**阶段（语法错误），`undefined reference` 一般在**链接**阶段（找不到函数定义）
