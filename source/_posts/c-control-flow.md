---
title: 流程控制
date: 2026-10-23 15:20:00
updated: 2026-10-23 15:20:00
categories:
  - C/C++
  - C语言基础
tags:
  - C语言
  - 循环
  - 分支
order: 5
description: 程序不只是从上到下顺序执行，还要根据条件选择分支、重复执行某些操作。本文介绍 if、switch、for、while、do-while、break、continue 和 goto，并复现 if 后面多写分号、悬空 else、switch 漏写 break、无符号循环变量、浮点数控制循环等常见错误。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：熟练使用 `if`、`switch` 实现分支，用 `for`、`while`、`do-while` 实现循环；会用 `break` 和 `continue` 控制循环；知道 `goto` 的合理用途；避开流程控制中的常见陷阱。
>
> **前置知识**：《运算符与表达式》、《输入与输出》。
>
> **编译环境**：`gcc -std=c11 -Wall -Wextra`。

## 1. if 语句

```c
#include <stdio.h>

const char* grade(int score)
{
    if (score < 0 || score > 100) {
        return "无效";
    } else if (score >= 90) {
        return "优秀";
    } else if (score >= 75) {
        return "良好";
    } else if (score >= 60) {
        return "及格";
    } else {
        return "不及格";
    }
}

int main(void)
{
    int scores[] = {95, 80, 60, 59, 101};
    for (int i = 0; i < 5; ++i) {
        printf("%d 分: %s\n", scores[i], grade(scores[i]));
    }
    return 0;
}
```

运行结果：

```
95 分: 优秀
80 分: 良好
60 分: 及格
59 分: 不及格
101 分: 无效
```

（这里用到了函数和数组，它们分别在第 1.6 篇和第 1.7 篇介绍，现在只需要关注 `if` 的部分。）

- `if (条件) { ... }`：条件为真（非 0）时执行花括号中的语句。
- `else if` 可以有任意多个，**从上到下依次判断，第一个成立的分支执行后，后面的分支都不再判断**。所以 80 分不会因为也满足 `score >= 60` 而输出「及格」。
- 把无效的输入放在最前面检查，后面的分支就不用再考虑它了。

### 1.1 陷阱：if 后面多了一个分号

```c
int balance = -50;
if (balance > 0);
{
    printf("余额充足，可以消费\n");
}
```

```
semi.c:5:21: warning: if statement has empty body [-Wempty-body]
余额充足，可以消费
```

余额是负数，却输出了「余额充足」。`if (balance > 0);` 后面的分号本身就是一条**空语句**，它才是 `if` 的主体；下面的花括号是一个独立的代码块，**无论条件是否成立都会执行**。`for` 和 `while` 后面多写分号也是同样的问题。

### 1.2 陷阱：悬空的 else

```c
int logged_in = 0, is_admin = 0;
if (logged_in)
    if (is_admin)
        printf("欢迎，管理员\n");
else
    printf("请先登录\n");
printf("程序结束\n");
```

```
dangling.c:8:5: warning: add explicit braces to avoid dangling else [-Wdangling-else]
程序结束
```

没有登录，却没有输出「请先登录」。缩进让人以为 `else` 属于外层的 `if`，但 C 语言的规则是：**`else` 和它前面最近的、还没有配对的 `if` 配对**。所以这个 `else` 实际属于 `if (is_admin)`，外层条件不成立时，整个内层都被跳过了。**编译器只看语法，不看缩进**。

**规则：`if`、`else`、`for`、`while` 的主体，即使只有一条语句，也总是加上花括号。** 这样上面两个陷阱都不会出现。

## 2. switch 语句

当需要把一个整数和多个**常量**比较时，`switch` 比一长串 `else if` 更清楚：

```c
char op = '*';
int a = 6, b = 7;
switch (op) {
case '+':
    printf("%d\n", a + b);
    break;
case '*':
    printf("%d * %d = %d\n", a, b, a * b);
    break;
default:
    printf("不支持的运算符 %c\n", op);
    break;
}
```

```
6 * 7 = 42
```

- `switch` 计算括号中表达式的值，然后**跳转**到值相同的 `case` 标签处开始执行。
- 没有匹配的 `case` 时，跳转到 **`default`**（如果有的话）。
- `case` 后面只能是**整数常量**（包括字符常量），不能是变量、范围，也不能是字符串或浮点数。

### 2.1 陷阱：漏写 break

```c
void describe(int day)
{
    switch (day) {
    case 1:
        printf("星期一");
    case 2:
        printf("星期二");
    case 3:
        printf("星期三");
        break;
    default:
        printf("其他");
    }
    printf("\n");
}
```

分别传入 1、2、3、7：

```
星期一星期二星期三
星期二星期三
星期三
其他
```

`case` 只是一个**跳转的入口**，不是一个独立的分支。跳转到 `case 1` 之后，程序会**一直往下执行**，经过 `case 2`、`case 3`，直到遇到 `break` 或者 `switch` 结束。这叫**贯穿**（fall through）。**每个 `case` 的最后都要写 `break`**（或者 `return`）。

这个错误用 `-Wall -Wextra` 在 Clang 上不会报告，需要额外加上 `-Wimplicit-fallthrough`（GCC 的 `-Wextra` 已经包含了它）：

```
sw.c:8:5: warning: unannotated fall-through between switch labels [-Wimplicit-fallthrough]
sw.c:10:5: warning: unannotated fall-through between switch labels [-Wimplicit-fallthrough]
```

### 2.2 有意的贯穿：多个 case 共用代码

贯穿也有正当的用途：让多个值执行同一段代码：

```c
int daysInMonth(int month, int leap)
{
    switch (month) {
    case 1: case 3: case 5: case 7: case 8: case 10: case 12:
        return 31;
    case 4: case 6: case 9: case 11:
        return 30;
    case 2:
        return leap ? 29 : 28;
    default:
        return -1;                  // 无效的月份
    }
}
```

```
2 月: 29 天，4 月: 30 天，13 月: -1
```

多个 `case` 标签之间没有任何语句，这种写法不会触发警告。

## 3. 循环

C 语言有三种循环：

```c
#include <stdio.h>

int main(void)
{
    int sum = 0;
    for (int i = 1; i <= 100; ++i) {          // for：已知次数
        sum += i;
    }
    printf("1 + 2 + ... + 100 = %d\n", sum);

    int n = 12345, digits = 0;
    while (n != 0) {                          // while：不知道要循环几次
        n /= 10;
        ++digits;
    }
    printf("12345 有 %d 位\n", digits);

    int m = 0, count = 0;
    do {                                      // do-while：至少执行一次
        m /= 10;
        ++count;
    } while (m != 0);
    printf("0 有 %d 位\n", count);
    return 0;
}
```

运行结果：

```
1 + 2 + ... + 100 = 5050
12345 有 5 位
0 有 1 位
```

| 循环 | 执行顺序 | 适用场景 |
|---|---|---|
| `for (初始化; 条件; 更新) { 循环体 }` | 初始化 → 判断条件 → 循环体 → 更新 → 判断条件 → …… | 次数确定，比如遍历 1 到 100 |
| `while (条件) { 循环体 }` | 先判断条件，再执行循环体 | 次数不确定，比如一直读到输入结束 |
| `do { 循环体 } while (条件);` | 先执行循环体，再判断条件 | 至少要执行一次，比如显示菜单 |

注意第三个例子：用 `while` 计算 0 有几位，循环一次都不会执行，结果是 0 位；用 `do-while` 才能得到正确的 1 位。另外，`do-while` 的最后**有一个分号**。

`for` 的初始化部分可以定义变量（C99 起），比如 `int i = 1`，这个 `i` 只在循环中有效，循环结束后就不存在了。

### 3.1 嵌套循环

循环中可以再写循环。九九乘法表：

```c
for (int i = 1; i <= 9; ++i) {
    for (int j = 1; j <= i; ++j) {
        printf("%d×%d=%-3d", j, i, i * j);
    }
    printf("\n");
}
```

```
1×1=1  
1×2=2  2×2=4  
1×3=3  2×3=6  3×3=9  
1×4=4  2×4=8  3×4=12 4×4=16 
1×5=5  2×5=10 3×5=15 4×5=20 5×5=25 
1×6=6  2×6=12 3×6=18 4×6=24 5×6=30 6×6=36 
1×7=7  2×7=14 3×7=21 4×7=28 5×7=35 6×7=42 7×7=49 
1×8=8  2×8=16 3×8=24 4×8=32 5×8=40 6×8=48 7×8=56 8×8=64 
1×9=9  2×9=18 3×9=27 4×9=36 5×9=45 6×9=54 7×9=63 8×9=72 9×9=81 
```

外层循环控制行，内层循环控制每一行中的列。内层循环的次数 `j <= i` 依赖于外层的 `i`，所以每一行比上一行多一个。`%-3d` 让乘积左对齐并占 3 个字符的宽度，各列就对齐了。

## 4. break 和 continue

- **`break`**：立即**跳出**当前循环（或 `switch`）。
- **`continue`**：跳过本次循环剩下的语句，直接开始**下一次**循环（在 `for` 循环中，会先执行「更新」部分）。

```c
#include <stdio.h>

int main(void)
{
    printf("100 以内的质数:");
    for (int n = 2; n <= 100; ++n) {
        int isPrime = 1;
        for (int d = 2; d * d <= n; ++d) {    // 只需要试到 √n
            if (n % d == 0) {
                isPrime = 0;
                break;                        // 找到一个因数就够了，跳出内层循环
            }
        }
        if (!isPrime) {
            continue;                         // 不是质数，直接开始下一次外层循环
        }
        printf(" %d", n);
    }
    printf("\n");
    return 0;
}
```

```
100 以内的质数: 2 3 5 7 11 13 17 19 23 29 31 37 41 43 47 53 59 61 67 71 73 79 83 89 97
```

判断 `n` 是不是质数时，只需要检查 2 到 √n：如果 `n` 有一个大于 √n 的因数，那么它一定还有一个小于 √n 的因数。条件写成 `d * d <= n`，避免了计算平方根（浮点数）。

### 4.1 break 只跳出一层

在二维的表格中查找第一个负数：

```c
int grid[3][4] = {{1, 2, 3, 4}, {5, -6, 7, 8}, {9, 10, -11, 12}};
for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 4; ++c) {
        if (grid[r][c] < 0) {
            printf("找到负数 %d 在 (%d, %d)\n", grid[r][c], r, c);
            break;                   // 只跳出了内层循环
        }
    }
}
```

```
找到负数 -6 在 (1, 1)
找到负数 -11 在 (2, 2)
```

只想找第一个，却找到了两个。**`break` 只能跳出它所在的那一层循环**，外层循环继续执行了。有两种解决方法：

```c
int found = 0;
for (int r = 0; r < 3 && !found; ++r) {      // 方法一：用标志变量
    for (int c = 0; c < 4; ++c) {
        if (grid[r][c] < 0) {
            printf("标志变量: 第一个负数 %d 在 (%d, %d)\n", grid[r][c], r, c);
            found = 1;
            break;
        }
    }
}

for (int r = 0; r < 3; ++r) {                // 方法二：goto
    for (int c = 0; c < 4; ++c) {
        if (grid[r][c] < 0) {
            printf("goto:     第一个负数 %d 在 (%d, %d)\n", grid[r][c], r, c);
            goto done;
        }
    }
}
done:
```

```
标志变量: 第一个负数 -6 在 (1, 1)
goto:     第一个负数 -6 在 (1, 1)
```

第三种方法，也往往是最好的方法，是把查找写成一个函数，找到后直接 `return`。

### 4.2 关于 goto

`goto 标签;` 无条件地跳转到同一个函数中的 `标签:` 处。滥用 `goto` 会让程序的流程四处乱跳，难以理解，所以通常被认为是不好的做法。但在 C 语言中，它有两种被广泛接受的用法：

1. **跳出多层嵌套循环**，如上所示。
2. **集中的错误处理和资源释放**：函数中申请了多个资源（内存、文件），中途任何一步出错，都 `goto` 到函数末尾统一释放。Linux 内核中大量使用这种写法。第 1.14 篇讲文件操作时会看到这个例子。

除此之外，不要使用 `goto`。

## 5. 循环的陷阱

### 5.1 无符号的循环变量

```c
for (unsigned int i = 3; i >= 0; --i) {      // 倒数 3、2、1、0？
    printf("%u ", i);
    ...
}
```

```
3 2 1 0 4294967295 4294967294 ……
```

（示例中加了一个计数器，输出 6 个数后强制退出，否则就是死循环。）无符号数永远 `>= 0`，`i` 从 0 再减 1，回绕成了最大值。用 `-Wall -Wextra` 在 Clang 上编译**没有警告**，要加上 `-Wtype-limits`（GCC 的 `-Wextra` 已经包含了它）：

```
warning: result of comparison of unsigned expression >= 0 is always true [-Wtautological-unsigned-zero-compare]
```

倒序循环时使用有符号的 `int`。

### 5.2 用浮点数控制循环

```c
int count = 0;
for (double x = 0.0; x != 1.0; x += 0.1) {
    if (++count > 20) {
        printf("x 已经是 %.17f，还没有等于 1.0\n", x);
        break;
    }
}
```

```
x 已经是 2.00000000000000044，还没有等于 1.0
```

本意是循环 10 次，但 0.1 累加 10 次的结果不精确地等于 1.0，`x != 1.0` 永远成立，循环停不下来。**用整数控制循环次数**，需要浮点数时在循环体中计算：

```c
for (int i = 0; i < 10; ++i) {
    double x = i * 0.1;
    ...
}
```

### 5.3 差一错误

循环多执行一次或少执行一次，叫作**差一错误**（off-by-one error），是最常见的循环 bug。比如遍历一个有 10 个元素的数组，下标是 0～9，条件应该写 `i < 10`，写成 `i <= 10` 就会越界（第 1.7 篇会用 AddressSanitizer 复现）。

一个好习惯是统一使用 **「从 0 开始，`<` 结束」** 的写法 `for (int i = 0; i < n; ++i)`，它恰好执行 n 次。

## 6. 常见错误

1. **`if`、`for`、`while` 后面多写了分号**：主体变成了空语句。
2. **省略花括号导致悬空 else**：`else` 和最近的 `if` 配对。总是写花括号。
3. **`switch` 漏写 `break`**：贯穿到下一个 `case`。加上 `-Wimplicit-fallthrough` 检查。
4. **`case` 后面用变量或范围**：只能是整数常量。
5. **以为 `break` 能跳出多层循环**：只跳出一层。
6. **用无符号变量做倒序循环**：死循环。
7. **用浮点数控制循环**：可能永远达不到终止条件。
8. **差一错误**：`<=` 和 `<` 分不清。
9. **循环条件中的变量在循环体中没有被修改**：死循环。

## 7. 小结

- `if` / `else if` / `else` 从上到下判断，执行第一个成立的分支；`else` 与最近的未配对 `if` 配对，所以总是加花括号
- `switch` 跳转到匹配的 `case`，然后一直执行到 `break`；`case` 只能是整数常量；多个 `case` 可以共用代码
- `for` 用于次数确定的循环，`while` 用于次数不确定的循环，`do-while` 至少执行一次
- `break` 跳出一层循环，`continue` 开始下一次循环；跳出多层循环用标志变量、函数返回或 `goto`
- 不要用无符号数做倒序循环，不要用浮点数控制循环，注意差一错误
- `-Wimplicit-fallthrough`、`-Wtype-limits` 能发现 `-Wall -Wextra` 在 Clang 上漏掉的问题

## 8. 练习

1. 输出一个 n 行的等腰三角形（n = 4 时如下所示）：

   ```
      *
     ***
    *****
   *******
   ```

2. 用 `while` 循环计算一个正整数的各位数字之和，比如 9875 的结果是 29。
3. 下面的程序会输出什么？为什么？

   ```c
   int i;
   for (i = 0; i < 5; i++);
   {
       printf("i = %d\n", i);
   }
   ```

<details>
<summary>点击查看答案</summary>

1. 第 `row` 行有 `n - row` 个空格和 `2 * row - 1` 个星号：

   ```c
   int n = 4;
   for (int row = 1; row <= n; ++row) {
       for (int s = 0; s < n - row; ++s) {
           putchar(' ');
       }
       for (int k = 0; k < 2 * row - 1; ++k) {
           putchar('*');
       }
       putchar('\n');
   }
   ```

2. 用 `% 10` 取出个位，用 `/ 10` 去掉个位：

   ```c
   int num = 9875, sum = 0;
   while (num != 0) {
       sum += num % 10;
       num /= 10;
   }
   printf("各位数字之和: %d\n", sum);
   ```

   输出 `各位数字之和: 29`。

3. 只输出一行 `i = 5`。`for` 后面的分号使循环体成为空语句，循环空转 5 次，`i` 变成 5；花括号中的 `printf` 是循环结束后的一条普通语句，只执行一次。编译器会给出警告：

   ```
   warning: for loop has empty body [-Wempty-body]
   ```

</details>
