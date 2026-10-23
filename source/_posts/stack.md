---
title: 栈
date: 2026-10-18 10:00:00
updated: 2026-10-23 20:20:00
categories:
  - 数据结构
  - 栈和队列
tags:
  - 数据结构
  - 栈
order: 1
description: 栈是只能在一端插入和删除的线性表，后进先出。本文用动态数组实现栈，并用三个可以逐步操作的演示讲解入栈出栈、括号匹配，以及把中缀表达式 3+4*(2-1) 转换为后缀表达式再求值的完整过程。
top_group_list: true
disableNunjucks: true
viz: [ds-stack]
---

> **本文目标**：理解栈的后进先出特性；会用数组实现栈；会用栈解决括号匹配问题；理解中缀表达式、后缀表达式，会用栈完成转换和求值；知道函数调用栈和栈的关系。
>
> **前置知识**：《顺序表》。

## 1. 什么是栈

**栈**（stack）是一种**只能在一端进行插入和删除**的线性表。允许操作的一端叫**栈顶**，另一端叫**栈底**。插入叫**入栈**（push），删除叫**出栈**（pop）。

就像一摞盘子：新盘子只能放在最上面，取盘子也只能从最上面取。所以**最后放进去的，最先被取出来**，这叫**后进先出**（LIFO，Last In First Out）。

生活和编程中的很多场景都有这个特点：浏览器的「后退」按钮、编辑器的「撤销」、函数调用的返回顺序（《函数》中的调用栈）。

## 2. 实现

栈只在一端操作，用顺序表实现非常合适：把数组的末尾作为栈顶，入栈和出栈都不需要移动其他元素。

```c
typedef struct {
    int* data;
    size_t top;                                 // 栈中元素的个数，data[top - 1] 是栈顶
    size_t capacity;
} Stack;

int stackInit(Stack* s, size_t capacity)
{
    s->data = malloc(capacity * sizeof(int));
    if (s->data == NULL) return 0;
    s->top = 0;
    s->capacity = capacity;
    return 1;
}

int push(Stack* s, int value)
{
    if (s->top == s->capacity) {
        size_t newCap = s->capacity * 2;
        int* p = realloc(s->data, newCap * sizeof(int));
        if (p == NULL) return 0;
        s->data = p;
        s->capacity = newCap;
    }
    s->data[s->top++] = value;
    return 1;
}

int pop(Stack* s, int* out)
{
    if (s->top == 0) return 0;                  // 栈空
    *out = s->data[--s->top];
    return 1;
}

int peek(const Stack* s, int* out)              // 查看栈顶，但不出栈
{
    if (s->top == 0) return 0;
    *out = s->data[s->top - 1];
    return 1;
}

int isEmpty(const Stack* s) { return s->top == 0; }

void stackFree(Stack* s)
{
    free(s->data);
    s->data = NULL;
    s->top = s->capacity = 0;
}
```

- `top` 既是元素个数，也是下一个元素要放的位置。`s->data[s->top++] = value` 先写入再加 1；`s->data[--s->top]` 先减 1 再读取。
- 对空栈执行 `pop` 叫**下溢**，必须检查并报告错误，不能读取 `data[-1]`。
- `push`、`pop`、`peek` 都是 O(1)（`push` 是均摊 O(1)，原因见《顺序表》）。

<div class="dsviz" data-viz="stack-ops"></div>

测试：

```c
Stack s;
stackInit(&s, 2);
for (int i = 1; i <= 5; ++i) push(&s, i * 10);
int x;
printf("出栈顺序:");
while (pop(&s, &x)) printf(" %d", x);
printf("，再 pop 返回 %d\n", pop(&s, &x));
stackFree(&s);
```

```
出栈顺序: 50 40 30 20 10，再 pop 返回 0
```

入栈的顺序是 10 到 50，出栈的顺序正好相反。初始容量只有 2，中途自动扩容了两次。

栈也可以用链表实现：把链表的头部作为栈顶，入栈就是头插，出栈就是删除第一个节点，同样是 O(1)。数组实现更紧凑、对缓存更友好，通常是首选。C++ 的 `std::stack` 默认基于 `deque`（《STL 容器》）。

## 3. 括号匹配

检查一段代码中的括号 `()`、`[]`、`{}` 是否正确地配对，是编译器和编辑器都要做的事。规则是：

- 遇到**左括号**，入栈；
- 遇到**右括号**，栈顶必须是**同类型的左括号**，弹出它；如果栈是空的，或者类型不对，就不匹配；
- 扫描结束后，**栈必须是空的**，否则说明有左括号没有配对。

为什么用栈？因为**最后出现的左括号，必须最先被配对**，这正是后进先出。

```c
int isBalanced(const char* text)
{
    Stack s;
    if (!stackInit(&s, 16)) return 0;
    int ok = 1;
    for (const char* p = text; *p != '\0' && ok; ++p) {
        char c = *p;
        if (c == '(' || c == '[' || c == '{') {
            push(&s, c);                        // 左括号：入栈，等待配对
        } else if (c == ')' || c == ']' || c == '}') {
            int open;
            if (!pop(&s, &open)) {
                ok = 0;                         // 右括号多了
            } else if ((c == ')' && open != '(') || (c == ']' && open != '[') || (c == '}' && open != '{')) {
                ok = 0;                         // 类型不匹配
            }
        }
    }
    if (!isEmpty(&s)) ok = 0;                   // 左括号多了
    stackFree(&s);
    return ok;
}
```

在演示中输入不同的字符串，看看三种不匹配的情况分别在哪一步被发现：

<div class="dsviz" data-viz="bracket"></div>

```
"{a[b(c)d]e}": 匹配
"([)]": 不匹配
"(()": 不匹配
"())": 不匹配
"": 匹配
```

`([)]` 中每种括号的数量都是对的，但它们交叉了：遇到 `)` 时栈顶是 `[`。只数括号的个数是发现不了这个错误的。

## 4. 表达式求值

### 4.1 后缀表达式

我们平时写的 `3 + 4 * 2` 叫**中缀表达式**：运算符在两个操作数中间。计算它要考虑优先级和括号，对计算机来说并不方便。

**后缀表达式**（也叫逆波兰表达式）把运算符放在两个操作数的**后面**：

| 中缀 | 后缀 |
|---|---|
| `3 + 4` | `3 4 +` |
| `3 + 4 * 2` | `3 4 2 * +` |
| `(3 + 4) * 2` | `3 4 + 2 *` |

后缀表达式**没有括号，也不需要优先级**，运算的顺序完全由符号的排列决定。用一个栈就能计算：从左到右扫描，**遇到数字就入栈；遇到运算符，就弹出两个数计算，再把结果入栈**。最后栈中剩下的就是结果。

```c
int evalPostfix(const char* postfix)
{
    Stack s;
    stackInit(&s, 16);
    for (const char* p = postfix; *p != '\0'; ++p) {
        if (isdigit((unsigned char)*p)) {
            push(&s, *p - '0');
        } else {
            int b, a;
            pop(&s, &b);                        // 注意顺序：先弹出的是右操作数
            pop(&s, &a);
            int r = *p == '+' ? a + b : *p == '-' ? a - b : *p == '*' ? a * b : a / b;
            push(&s, r);
        }
    }
    int result;
    pop(&s, &result);
    stackFree(&s);
    return result;
}
```

（为了简洁，这里只处理一位数字，也没有检查表达式是否合法。）

注意弹出的顺序：**先弹出的是右操作数**。对于减法和除法，顺序搞反了结果就错了：`8 3 -` 应该是 8 - 3 = 5，而不是 3 - 8。

### 4.2 中缀转后缀

把中缀表达式转换为后缀表达式，同样要用栈，这次栈中存放的是**运算符**：

- **数字**：直接输出；
- **左括号**：入栈；
- **右括号**：不断弹出并输出运算符，直到遇到左括号（左括号弹出但不输出）；
- **运算符 c**：先把栈顶**优先级不低于 c** 的运算符依次弹出并输出（它们应该先计算），然后 c 入栈；
- **扫描结束**：弹出并输出栈中所有剩余的运算符。

```c
int precedence(char op)
{
    return (op == '*' || op == '/') ? 2 : (op == '+' || op == '-') ? 1 : 0;
}

void toPostfix(const char* infix, char* out)
{
    Stack ops;
    stackInit(&ops, 16);
    size_t k = 0;
    int top;
    for (const char* p = infix; *p != '\0'; ++p) {
        char c = *p;
        if (isdigit((unsigned char)c)) {
            out[k++] = c;                       // 数字直接输出
        } else if (c == '(') {
            push(&ops, c);
        } else if (c == ')') {
            while (pop(&ops, &top) && top != '(') {
                out[k++] = (char)top;           // 弹出到左括号为止
            }
        } else if (precedence(c) > 0) {
            while (peek(&ops, &top) && precedence((char)top) >= precedence(c)) {
                pop(&ops, &top);
                out[k++] = (char)top;           // 弹出优先级不低于 c 的运算符
            }
            push(&ops, c);
        }
    }
    while (pop(&ops, &top)) {
        out[k++] = (char)top;
    }
    out[k] = '\0';
    stackFree(&ops);
}
```

`(` 的优先级是 0，所以运算符入栈时不会把 `(` 弹出，它像一道隔墙，把括号内外的运算符分开。逐步观察 `3+4*(2-1)` 的转换过程：

<div class="dsviz" data-viz="postfix"></div>

几个表达式的转换和求值结果：

```
3+4*2      后缀: 342*+    值: 11
(3+4)*2    后缀: 34+2*    值: 14
3+4*(2-1)  后缀: 3421-*+  值: 7
8-3-2      后缀: 83-2-    值: 3
9/3*2+1    后缀: 93/2*1+  值: 7
```

注意 `8-3-2`：减法是**左结合**的，应该计算 (8 - 3) - 2 = 3。这就是为什么弹出条件是「优先级**不低于**」（`>=`）：遇到第二个 `-` 时，栈中的第一个 `-` 和它优先级相同，要先弹出，保证从左往右计算。如果写成 `>`，结果会变成 8 - (3 - 2) = 7。

这个算法叫**调度场算法**（Shunting Yard），是编译器和计算器处理表达式的基础。所有的程序都用 `-fsanitize=address,undefined` 检查过，`leaks` 报告没有泄漏。

## 5. 栈与递归

《函数》中讲过，每次函数调用都会在**调用栈**上压入一个栈帧，函数返回时弹出。最后调用的函数最先返回，这正是栈。所以：

- **递归可以改写成用显式的栈来实现的循环**。当递归太深、可能导致栈溢出时，就需要这样改写。第 3 章的二叉树非递归遍历就是例子。
- 反过来，用栈解决的问题，往往也可以用递归来写，比如括号匹配可以写成递归下降的解析器。

## 6. 常见错误

1. **对空栈执行 `pop` 或 `peek` 时不检查**：读取越界的内存。
2. **`top` 的含义不统一**：有的代码中 `top` 指向栈顶元素，有的指向下一个空位，混用会导致差一错误。本文中 `top` 是元素个数，栈顶是 `data[top - 1]`。
3. **后缀表达式求值时弹出顺序搞反**：减法和除法的结果错误。
4. **中缀转后缀时把 `>=` 写成 `>`**：左结合的运算符计算顺序错误。
5. **括号匹配时只比较数量**：发现不了交叉的括号。
6. **扫描结束后忘了检查栈是否为空**。

## 7. 小结

- 栈只能在栈顶插入和删除，后进先出
- 用数组实现，`push`、`pop`、`peek` 都是 O(1)；对空栈操作要检查
- 括号匹配：左括号入栈，右括号与栈顶配对，最后栈必须为空
- 后缀表达式不需要括号和优先级，用一个数字栈即可求值；中缀转后缀用一个运算符栈，弹出优先级不低于当前运算符的运算符
- 函数调用栈就是栈；递归可以改写成用显式栈的循环

## 8. 练习

1. 设计一个栈，除了 `push` 和 `pop`，还支持 `getMin` 返回栈中的最小值，要求三个操作都是 O(1)。
2. 计算后缀表达式 `52-3*` 的值，写出每一步之后栈中的内容。
3. 入栈的顺序是 1、2、3（中间可以随时出栈），下面哪个出栈顺序是不可能的？(a) 3 2 1，(b) 2 3 1，(c) 3 1 2，(d) 1 3 2。

<details>
<summary>点击查看答案</summary>

1. 用一个辅助数组 `mins`，`mins[i]` 记录栈中前 i + 1 个元素的最小值。入栈时 `mins[top]` 等于新元素和 `mins[top - 1]` 中较小的那个；出栈时一起弹出，`getMin` 直接返回 `mins[top - 1]`：

   ```c
   typedef struct {
       int data[CAP];
       int mins[CAP];                              // mins[i]：前 i + 1 个元素中的最小值
       int top;
   } MinStack;

   int msPush(MinStack* s, int v)
   {
       if (s->top == CAP) return 0;
       s->data[s->top] = v;
       s->mins[s->top] = (s->top == 0 || v < s->mins[s->top - 1]) ? v : s->mins[s->top - 1];
       s->top++;
       return 1;
   }

   int msMin(const MinStack* s) { return s->mins[s->top - 1]; }
   ```

   依次入栈 5 3 7 2 8，最小值依次是 5 3 3 2 2；再依次出栈 8 2 7 3，最小值依次变为 2 3 3 5。这是「以空间换时间」的又一个例子。

2. `5` 入栈：[5]；`2` 入栈：[5, 2]；`-`：弹出 2 和 5，计算 5 - 2 = 3，入栈：[3]；`3` 入栈：[3, 3]；`*`：弹出 3 和 3，计算 3 × 3 = 9，入栈：[9]。结果是 9，对应的中缀表达式是 (5 - 2) * 3。

3. (c) 不可能。3 第一个出栈，说明 1、2 都已经入栈且还在栈中，2 在 1 的上面，所以接下来出栈的只能是 2，不可能是 1。其他三个都可以：(a) 依次入栈 1 2 3 后全部出栈；(b) 入 1、入 2、出 2、入 3、出 3、出 1；(d) 入 1、出 1、入 2、入 3、出 3、出 2。

</details>
