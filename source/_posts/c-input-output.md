---
title: 输入与输出
date: 2026-10-23 15:00:00
updated: 2026-10-23 15:00:00
categories:
  - C/C++
  - C语言基础
tags:
  - C语言
  - printf
  - scanf
order: 4
description: printf 和 scanf 是初学 C 语言时用得最多的两个函数，也是坑最多的两个函数。本文介绍 printf 的格式说明符和宽度精度控制，逐一复现格式不匹配、scanf 忘写 &、不检查返回值导致的死循环、%c 读到换行符、%s 缓冲区溢出等问题，并给出正确的写法。
disableNunjucks: true
---

> **本文目标**：熟练使用 `printf` 的格式说明符、宽度、精度和标志；会用 `scanf` 读取各种类型的数据，并检查它的返回值；能处理输入缓冲区中残留的字符；会用 `getchar` 和 `putchar` 逐个字符地处理输入。
>
> **前置知识**：《数据类型与变量》、《运算符与表达式》。循环会在下一篇详细讲，本文的几个例子用到了简单的 `while` 和 `for`。
>
> **编译环境**：`gcc -std=c11 -Wall -Wextra`。为了让结果可以重复，本文的输入都是通过管道提供的，比如 `printf '18 1.75\n' | ./scan`，效果和在键盘上输入 `18 1.75` 再按回车一样。因为输入的内容不会显示在屏幕上，示例在每个提示之后都多输出了一个换行。

## 1. printf

`printf` 的第一个参数是**格式字符串**，其中的**格式说明符**（以 `%` 开头）会依次被后面的参数替换。

### 1.1 格式说明符

```c
#include <stdio.h>

int main(void)
{
    int n = 255;
    double pi = 3.14159265;
    char c = 'A';
    printf("%%d  : %d\n", n);
    printf("%%x  : %x，%%X: %X，%%o: %o\n", n, n, n);
    printf("%%f  : %f\n", pi);
    printf("%%.2f: %.2f\n", pi);
    printf("%%e  : %e\n", 123456.789);
    printf("%%g  : %g 和 %g\n", 0.0001, 123456789.0);
    printf("%%c  : %c\n", c);
    printf("%%s  : %s\n", "hello");
    printf("%%p  : %p\n", (void*)&n);
    printf("%%zu : %zu\n", sizeof(double));
    printf("%%lld: %lld\n", 9000000000LL);
    return 0;
}
```

运行结果：

```
%d  : 255
%x  : ff，%X: FF，%o: 377
%f  : 3.141593
%.2f: 3.14
%e  : 1.234568e+05
%g  : 0.0001 和 1.23457e+08
%c  : A
%s  : hello
%p  : 0x16d71e568
%zu : 8
%lld: 9000000000
```

| 说明符 | 类型 | 说明 |
|---|---|---|
| `%d` / `%i` | `int` | 十进制整数 |
| `%u` | `unsigned int` | 无符号十进制 |
| `%ld` / `%lld` | `long` / `long long` | |
| `%x` / `%X` / `%o` | 无符号整数 | 十六进制（小写 / 大写）、八进制 |
| `%f` | `double` | 小数形式，默认 6 位小数 |
| `%e` | `double` | 科学计数法 |
| `%g` | `double` | 自动选择 `%f` 或 `%e` 中较短的一种，去掉末尾的 0 |
| `%c` | `char` | 一个字符 |
| `%s` | 字符串 | |
| `%p` | 指针 | 地址，参数要转换为 `void*` |
| `%zu` | `size_t` | `sizeof` 的结果 |
| `%%` | | 输出 `%` 本身 |

注意：`printf` 中 `float` 和 `double` 都用 `%f`，因为传给 `printf` 时 `float` 会被自动转换为 `double`。但下面会看到，`scanf` 中两者是不同的。

### 1.2 宽度、精度和标志

在 `%` 和字母之间，可以加上控制格式的选项：

```c
printf("[%5d]  [%-5d]  [%05d]  [%+d]\n", 42, 42, 42, 42);
printf("[%8.3f]  [%-8.1f]\n", pi, pi);
printf("[%10s]  [%-10s]  [%.3s]\n", "C语言", "hi", "abcdef");
```

```
[   42]  [42   ]  [00042]  [+42]
[   3.142]  [3.1     ]
[   C语言]  [hi        ]  [abc]
```

| 写法 | 含义 |
|---|---|
| `%5d` | **宽度** 5，不够时在左边补空格（右对齐） |
| `%-5d` | 左对齐，在右边补空格 |
| `%05d` | 用 0 补齐 |
| `%+d` | 正数也显示 `+` 号 |
| `%8.3f` | 宽度 8，保留 3 位小数（四舍五入） |
| `%.3s` | 最多输出字符串的 3 个字符 |

注意 `%10s` 输出的 `C语言` 前面只补了 3 个空格：**宽度按字节计算**，「语言」两个汉字在 UTF-8 中占 6 个字节，`C语言` 一共 7 个字节。输出中文表格时，列会对不齐。

### 1.3 格式和参数不匹配

`printf` 无法检查参数的类型，它完全按照格式字符串去解释传进来的数据：

```c
double price = 9.99;
int count = 3;
printf("价格: %d\n", price);           // 用 %d 输出 double
printf("数量: %f\n", count);           // 用 %f 输出 int
printf("两个值: %d %d\n", count);       // 参数少了一个
```

编译器给出了警告：

```
mismatch.c:6:28: warning: format specifies type 'int' but the argument has type 'double' [-Wformat]
mismatch.c:7:28: warning: format specifies type 'double' but the argument has type 'int' [-Wformat]
mismatch.c:8:28: warning: more '%' conversions than data arguments [-Wformat-insufficient-args]
```

运行结果：

```
价格: 1202590843
数量: 0.000000
两个值: 3 0
```

三行全是错的，而且**这些都是未定义行为**，在不同的机器上会输出不同的垃圾值。`printf` 不会做任何类型转换：想用 `%d` 输出 `double` 的整数部分，要先写 `(int)price`。好在编译器能检查格式字符串，**只要打开 `-Wall`，这类错误都会被发现**。

## 2. scanf

`scanf` 按照格式字符串从键盘（标准输入）读取数据，存放到变量中：

```c
#include <stdio.h>

int main(void)
{
    int age;
    double height;
    printf("请输入年龄和身高: ");
    int n = scanf("%d %lf", &age, &height);     // double 要用 %lf
    printf("\nscanf 返回 %d，age = %d，height = %.2f\n", n, age, height);
    return 0;
}
```

输入 `18 1.75`：

```
请输入年龄和身高: 
scanf 返回 2，age = 18，height = 1.75
```

三个要点：

1. **变量前面要加 `&`**（取地址），`scanf` 需要知道把数据存到哪里。为什么要这样，学了指针之后就明白了。
2. **读取 `double` 要用 `%lf`**，读取 `float` 用 `%f`，这一点和 `printf` 不同。
3. **`scanf` 的返回值是成功读取的数据个数**。

### 2.1 陷阱一：忘了写 `&`

```c
int age = 0;
scanf("%d", age);                 // 忘了写 &
```

```
noamp.c:6:17: warning: format specifies type 'int *' but the argument has type 'int' [-Wformat]
```

输入 `18` 之后，程序直接**崩溃**了（退出码 139，段错误）。`scanf` 把 `age` 的值 0 当成了一个内存地址，试图把 18 写到地址 0 上。如果 `age` 碰巧是一个合法的地址，`scanf` 就会悄悄地改写某块无关的内存，后果更难预料。

### 2.2 陷阱二：不检查返回值

上面的程序如果输入 `18 abc`：

```
请输入年龄和身高: 
scanf 返回 1，age = 18，height = 0.00
```

`scanf` 只成功读取了 1 个数据，`height` 没有被赋值。这里显示的 0.00 只是碰巧，`height` 没有初始化，它的值是不确定的。**必须检查返回值**：

```c
int age = -1;
printf("请输入年龄: ");
if (scanf("%d", &age) != 1) {
    printf("\n输入的不是整数\n");
    return 1;
}
```

输入 `abc`：

```
请输入年龄: 
输入的不是整数
```

### 2.3 陷阱三：错误的输入留在缓冲区中

用户输入错误时，常见的做法是让用户重新输入：

```c
int x, tries = 0;
while (tries < 5) {
    printf("请输入一个正整数: ");
    if (scanf("%d", &x) == 1 && x > 0) {
        break;
    }
    printf("\n输入无效\n");
    ++tries;
}
```

先输入 `abc`，再输入 `7`：

```
请输入一个正整数: 
输入无效
请输入一个正整数: 
输入无效
请输入一个正整数: 
输入无效
请输入一个正整数: 
输入无效
请输入一个正整数: 
输入无效

循环结束，tries = 5
```

程序根本没有给我们输入 `7` 的机会，连续失败了 5 次（如果没有 `tries` 的限制，这就是一个**死循环**）。原因是：**键盘输入的内容先放在输入缓冲区中，`scanf` 从缓冲区中读取**。`%d` 遇到字母 `a` 时读取失败，但是 `abc` **仍然留在缓冲区中**。下一次 `scanf("%d")` 又从 `a` 开始读，又失败……

解决方法是读取失败时，把这一行剩下的字符都读掉：

```c
int r = scanf("%d", &x);
if (r == 1 && x > 0) {
    break;
}
if (r == EOF) {
    printf("\n输入结束\n");
    return 1;
}
printf("\n输入无效\n");
int ch;
while ((ch = getchar()) != '\n' && ch != EOF) {}   // 丢弃这一行剩下的字符
```

依次输入 `abc`、`-3`、`7`：

```
请输入一个正整数: 
输入无效
请输入一个正整数: 
输入无效
请输入一个正整数: 
读到 7，tries = 2
```

`scanf` 在没有任何输入可读时（比如用户按了 Ctrl+D，或者输入文件已经读完）返回 **`EOF`**（一个负数，通常是 -1），也要处理，否则同样会死循环。

### 2.4 陷阱四：`%c` 读到了换行符

```c
int n;
char choice;
printf("请输入数量: ");
scanf("%d", &n);
printf("\n确认吗（y/n）: ");
scanf("%c", &choice);
printf("\nn = %d，choice 的编码是 %d\n", n, choice);
```

依次输入 `3` 和 `y`：

```
请输入数量: 
确认吗（y/n）: 
n = 3，choice 的编码是 10
```

程序没有等我们输入 `y`，`choice` 的编码是 10，也就是换行符 `'\n'`。输入 `3` 后按下的回车，在缓冲区中留下了一个 `'\n'`。`%d` 读取数字时会**自动跳过前面的空白字符**（空格、制表符、换行），但 **`%c` 不会**，它读到的就是这个换行符。

修复方法是在 `%c` 前面加一个空格，**格式字符串中的空格表示「跳过任意多个空白字符」**：

```c
scanf(" %c", &choice);          // %c 前面加一个空格：跳过空白字符
```

```
n = 3，choice = y
```

### 2.5 陷阱五：`%s` 缓冲区溢出

`%s` 读取一个单词（遇到空白字符停止），存放到字符数组中：

```c
char name[8];
printf("请输入名字: ");
scanf("%s", name);              // 没有限制长度
```

数组 `name` 只能存放 7 个字符（还要留一个位置给字符串的结束标记，第 1.10 篇会讲）。输入 `Christopher`（11 个字符），用 AddressSanitizer 检查：

```
ERROR: AddressSanitizer: stack-buffer-overflow on address 0x00016dd864c8
WRITE of size 12 at 0x00016dd864c8 thread T0
    #0 0x000102841fdc in scanf_common(...)
    #1 0x000102842490 in scanf+0x68
    #2 0x000102078944 in main str.c:7
```

`scanf` 写入了 12 个字节（11 个字符加上结束标记），超出了数组的范围，改写了相邻的内存。这就是**缓冲区溢出**，它不仅会导致程序出错，还是历史上最常见的安全漏洞之一，攻击者可以通过精心构造的输入控制程序。**用 `%s` 时一定要指定最大宽度**：

```c
scanf("%7s", name);             // 最多读 7 个字符，留一个位置给 '\0'
```

```
你好，Christo
```

多余的字符被留在了缓冲区中，但至少不会越界写入了。读取一整行（包括空格）应该用 `fgets`，第 1.10 篇会介绍。

### 2.6 scanf 小结

| 规则 | 原因 |
|---|---|
| 变量前加 `&`（字符数组除外） | `scanf` 需要地址 |
| `double` 用 `%lf` | `%f` 对应 `float` |
| 检查返回值是否等于期望读取的个数 | 输入可能出错或结束 |
| 读取失败后清空这一行 | 错误的输入留在缓冲区中 |
| 读字符用 `" %c"` | 跳过前面残留的换行符 |
| `%s` 要写宽度，比如 `%19s` | 防止缓冲区溢出 |

正因为 `scanf` 有这么多陷阱，很多实际项目中的做法是：**先用 `fgets` 读取一整行，再从这一行中解析数据**（用 `sscanf` 或 `strtol`）。这会在字符串一篇中介绍。

## 3. getchar 和 putchar

`getchar()` 从输入读取**一个字符**，`putchar(c)` 输出一个字符。下面的程序把输入的小写字母转换为大写，并统计字符数和行数：

```c
#include <stdio.h>

int main(void)
{
    int ch;                          // 注意是 int，不是 char
    long chars = 0, lines = 0;
    while ((ch = getchar()) != EOF) {
        ++chars;
        if (ch == '\n') {
            ++lines;
        }
        putchar(ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch);   // 小写转大写后原样输出
    }
    printf("共 %ld 个字符，%ld 行\n", chars, lines);
    return 0;
}
```

输入两行 `hello, world` 和 `C is fun`：

```
HELLO, WORLD
C IS FUN
共 22 个字符，2 行
```

- `while ((ch = getchar()) != EOF)` 是逐个字符读取输入的**标准写法**：先把读到的字符赋给 `ch`，再判断它是不是 `EOF`。注意赋值要加括号，因为 `!=` 的优先级比 `=` 高。
- 在终端中手动输入时，按 **Ctrl+D**（Windows 上是 Ctrl+Z 再回车）表示输入结束。
- **`ch` 必须是 `int` 类型**。`getchar` 返回的是 0～255 之间的字符，或者 `EOF`（-1），一共 257 种可能，`char` 只能表示 256 种。练习 3 会实测用 `char` 时出现的问题。

这个程序还可以用来处理文件：`./count < input.txt` 把文件作为输入，这叫**输入重定向**。

## 4. 常见错误

1. **格式说明符和参数类型不匹配**：输出垃圾值，打开 `-Wall` 能发现。
2. **`scanf` 忘了写 `&`**：程序崩溃或改写其他内存。
3. **用 `%f` 读取 `double`**：应该用 `%lf`。
4. **不检查 `scanf` 的返回值**：变量可能没有被赋值。
5. **读取失败后没有清空缓冲区**：死循环。
6. **用 `%c` 读字符时读到了换行符**：写成 `" %c"`。
7. **`%s` 不限制宽度**：缓冲区溢出。
8. **用 `char` 保存 `getchar` 的返回值**：无法正确判断 `EOF`。

## 5. 小结

- `printf` 用格式说明符输出各种类型；宽度、精度、`-`、`0`、`+` 控制格式；宽度按字节计算
- 格式和参数不匹配是未定义行为，`-Wall` 能检查出来
- `scanf` 的变量前要加 `&`，`double` 用 `%lf`
- **一定要检查 `scanf` 的返回值**；读取失败的内容会留在缓冲区中，要手动清除
- `%d` 会跳过空白，`%c` 不会，用 `" %c"`；`%s` 要指定最大宽度
- `getchar` / `putchar` 逐个字符处理输入输出，保存 `getchar` 返回值的变量必须是 `int`

## 6. 练习

1. 读取两个整数，输出它们的和、差、积、商（商保留 2 位小数）。要处理输入错误和除数为 0 的情况。
2. 用 `printf` 的宽度控制，输出 1 到 5 的平方和立方，排成整齐的三列（循环在下一篇讲，这里可以先模仿第 3 节的写法）。
3. 下面的程序把 `ch` 定义成了 `char`。输入 `ab\xffcd`（`\xff` 是一个值为 255 的字节，可以用 `printf 'ab\xffcd' | ./ex3` 输入），会输出什么？为什么？

   ```c
   char ch;
   long count = 0;
   while ((ch = getchar()) != EOF) {
       ++count;
   }
   printf("读到 %ld 个字节\n", count);
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```c
   int a, b;
   printf("请输入两个整数: ");
   if (scanf("%d %d", &a, &b) != 2) {
       printf("\n输入错误\n");
       return 1;
   }
   printf("\n%d + %d = %d\n", a, b, a + b);
   printf("%d - %d = %d\n", a, b, a - b);
   printf("%d * %d = %d\n", a, b, a * b);
   if (b != 0) {
       printf("%d / %d = %.2f\n", a, b, (double)a / b);
   } else {
       printf("除数不能为 0\n");
   }
   ```

   输入 `7 2`，输出 `7 / 2 = 3.50`。注意要先转换为 `double` 再做除法。

2. 参考代码：

   ```c
   printf("%4s %6s %8s\n", "n", "n^2", "n^3");
   for (int n = 1; n <= 5; ++n) {
       printf("%4d %6d %8d\n", n, n * n, n * n * n);
   }
   ```

   ```
      n    n^2      n^3
      1      1        1
      2      4        8
      3      9       27
      4     16       64
      5     25      125
   ```

   表头和数据使用相同的宽度，就能对齐。

3. 输出 `读到 2 个字节`，后面的 3 个字节都没有读到。在这台机器上 `char` 是有符号的（范围 -128～127），值为 255 的字节存入 `char` 后变成了 -1，正好等于 `EOF`，循环就提前结束了。把 `ch` 改成 `int` 后，可以正确读到 5 个字节。（在 `char` 是无符号的平台上，情况相反：`ch` 永远不等于 -1，循环永远不会结束。）

</details>
