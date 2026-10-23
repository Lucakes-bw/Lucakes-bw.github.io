---
title: 指针入门
date: 2026-10-16 10:00:00
updated: 2026-10-16 10:00:00
categories:
  - C/C++
  - C语言基础
tags:
  - C语言
  - 指针
order: 8
description: 指针就是地址。理解指针变量、取地址、解引用，以及指针作为函数参数的用法。
top_group_list: true
---

## 1. 什么是指针

内存中的每个字节都有一个编号，这个编号就是**地址**。**指针变量**就是用来保存地址的变量。

```c
int a = 10;
int *p = &a;   // p 保存的是变量 a 的地址
```

- `&a`：取变量 `a` 的地址
- `int *p`：定义一个指向 `int` 的指针变量
- `*p`：解引用，访问 `p` 所指向的内存，也就是 `a` 本身

```c
#include <stdio.h>

int main(void)
{
    int a = 10;
    int *p = &a;

    printf("a 的值: %d\n", a);
    printf("a 的地址: %p\n", (void *)&a);
    printf("p 的值: %p\n", (void *)p);
    printf("*p 的值: %d\n", *p);

    *p = 20;                  // 通过指针修改 a
    printf("修改后 a = %d\n", a);
    return 0;
}
```

## 2. 指针作为函数参数

C 语言函数参数是**值传递**。想在函数内部修改外部变量，就需要传地址：

```c
void swap(int *x, int *y)
{
    int tmp = *x;
    *x = *y;
    *y = tmp;
}

int main(void)
{
    int a = 1, b = 2;
    swap(&a, &b);   // 交换后 a = 2, b = 1
    return 0;
}
```

## 3. 常见错误

1. **野指针**：指针未初始化就使用。定义时没有明确指向，就初始化为 `NULL`
2. **空指针解引用**：对 `NULL` 执行 `*p` 会导致程序崩溃
3. **指针类型不匹配**：`int *` 不应该指向 `double` 变量

## 4. 小结

- 指针 = 地址，指针变量 = 保存地址的变量
- `&` 取地址，`*` 解引用
- 想在函数内修改外部变量，就传它的地址
