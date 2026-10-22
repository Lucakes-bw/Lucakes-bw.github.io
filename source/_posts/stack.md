---
title: 栈的原理与实现
date: 2026-10-18 10:00:00
updated: 2026-10-18 10:00:00
categories:
  - 数据结构
  - 栈和队列
tags:
  - 数据结构
  - 栈
order: 1
description: 栈是后进先出（LIFO）的线性表。本文用数组实现一个顺序栈，并用它完成括号匹配。
cover: /img/covers/data-structure.svg
---

## 1. 什么是栈

栈（Stack）是只允许在一端进行插入和删除的线性表，特点是**后进先出**（Last In First Out）。

- **入栈 push**：把元素放到栈顶
- **出栈 pop**：取出栈顶元素
- **栈顶 top**：查看栈顶元素但不取出

生活中的例子：一摞盘子，最后放上去的最先被拿走。

## 2. 顺序栈实现

```c
#include <stdbool.h>

#define MAX_SIZE 100

typedef struct {
    char data[MAX_SIZE];
    int top;            // 栈顶下标，-1 表示空栈
} Stack;

void stack_init(Stack *s)          { s->top = -1; }
bool stack_empty(const Stack *s)   { return s->top == -1; }

bool stack_push(Stack *s, char c)
{
    if (s->top == MAX_SIZE - 1) return false;   // 栈满
    s->data[++s->top] = c;
    return true;
}

bool stack_pop(Stack *s, char *out)
{
    if (stack_empty(s)) return false;           // 栈空
    *out = s->data[s->top--];
    return true;
}
```

## 3. 应用：括号匹配

判断字符串中的括号 `()[]{}` 是否正确配对：

```c
bool is_valid(const char *str)
{
    Stack s;
    stack_init(&s);
    for (const char *p = str; *p; p++) {
        if (*p == '(' || *p == '[' || *p == '{') {
            stack_push(&s, *p);
        } else if (*p == ')' || *p == ']' || *p == '}') {
            char left;
            if (!stack_pop(&s, &left)) return false;
            if ((*p == ')' && left != '(') ||
                (*p == ']' && left != '[') ||
                (*p == '}' && left != '{'))
                return false;
        }
    }
    return stack_empty(&s);
}
```

## 4. 小结

- 栈是后进先出的线性表，所有操作的时间复杂度都是 O(1)
- 函数调用、表达式求值、括号匹配、撤销操作都用到了栈
