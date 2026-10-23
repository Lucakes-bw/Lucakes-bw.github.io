---
title: 字符串
date: 2026-10-23 17:00:00
updated: 2026-10-23 17:00:00
categories:
  - C/C++
  - C语言基础
tags:
  - C语言
  - 字符串
order: 10
description: C 语言没有字符串类型，字符串就是以 '\0' 结尾的字符数组。本文介绍字符串的存储、字面量与字符数组的区别和 string.h 的常用函数，实测 strcpy 越界把 role 从 guest 改成 admin 的缓冲区溢出，讲解 snprintf、fgets、strtol 等安全的替代写法，以及命令行参数。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 C 字符串以 `'\0'` 结尾的存储方式；分清字符数组和字符串字面量；会用 `strlen`、`strcpy`、`strcat`、`strcmp`、`strstr` 等函数；认识缓冲区溢出的危害，会用 `snprintf`、`fgets` 等安全的写法；会用 `strtol` 把字符串可靠地转换为数字；会读取命令行参数。
>
> **前置知识**：《数组》、《指针进阶》。
>
> **编译环境**：`gcc -std=c11 -Wall -Wextra`。

## 1. 字符串就是以 '\0' 结尾的字符数组

C 语言没有专门的字符串类型。一个字符串就是**一串连续存放的字符，最后以一个值为 0 的字符 `'\0'` 结尾**。

```c
#include <stdio.h>
#include <string.h>

int main(void)
{
    char greeting[] = "Hello";                  // 编译器自动加上 '\0'
    char name[20] = "Tom";
    printf("greeting: sizeof = %zu, strlen = %zu\n", sizeof(greeting), strlen(greeting));
    printf("name:     sizeof = %zu, strlen = %zu\n", sizeof(name), strlen(name));
    printf("greeting 的每个字节:");
    for (size_t i = 0; i < sizeof(greeting); ++i) {
        printf(" %d", greeting[i]);
    }
    printf("\n");

    greeting[0] = 'J';                          // 字符数组可以修改
    printf("%s\n", greeting);
    return 0;
}
```

运行结果：

```
greeting: sizeof = 6, strlen = 5
name:     sizeof = 20, strlen = 3
greeting 的每个字节: 72 101 108 108 111 0
Jello
```

- `"Hello"` 有 5 个字符，但数组 `greeting` 占 **6** 个字节，最后一个字节是 0，也就是 `'\0'`，叫作**字符串结束符**。
- **`strlen`** 返回字符串的长度，**不包括 `'\0'`**。它的做法是从头开始数，直到遇到 `'\0'` 为止。
- `name` 数组有 20 个字节，存放的字符串只有 3 个字符。`sizeof` 是数组的容量，`strlen` 是字符串的实际长度。
- **存放 n 个字符的字符串，需要 n + 1 个字节的空间**。这是 C 字符串中最常出错的地方。

### 1.1 没有 '\0' 会怎样

```c
char after[] = "XYZ";
char noEnd[3] = {'a', 'b', 'c'};            // 没有 '\0'
printf("strlen(noEnd) = %zu\n", strlen(noEnd));
printf("noEnd = %s\n", noEnd);
```

```
strlen(noEnd) = 3
noEnd = abc
```

看起来一切正常，但这只是**碰巧**：`noEnd` 后面的那个字节恰好是 0。`strlen` 和 `printf("%s")` 都会一直读到 `'\0'` 为止，如果后面的内存中没有 0，它们就会一直读下去，越过数组的边界。AddressSanitizer 揭示了真相：

```
ERROR: AddressSanitizer: stack-buffer-overflow on address 0x00016b20a4b3
READ of size 6 at 0x00016b20a4b3 thread T0
    #0 0x0001052b2800 in strlen+0x22c
    #1 0x000104bf4968 in main noend.c:7
```

`strlen` 为了找 `'\0'`，读取了 6 个字节，越过了只有 3 个字节的数组。**所有的字符串函数都假设字符串以 `'\0'` 结尾**，自己构造字符串时，一定不能忘记它。

## 2. 字符数组和字符串字面量

```c
#include <stdio.h>
int main(void)
{
    char* s = "Hello";              // 指向字符串字面量
    printf("%s\n", s);
    fflush(stdout);
    s[0] = 'J';                     // 试图修改字面量
    printf("%s\n", s);
    return 0;
}
```

```
Hello
```

程序在修改 `s[0]` 时崩溃了（退出码 138）。原因是：

- **`char greeting[] = "Hello";`** 创建了一个**字符数组**，把字符串的内容**复制**进去。数组是自己的，可以随意修改。
- **`char* s = "Hello";`** 只是让指针 `s` 指向**字符串字面量**本身。字面量存放在程序的**只读区域**，修改它是未定义行为，在现代系统上通常会直接崩溃。

**规则：指向字符串字面量的指针，总是声明为 `const char*`**，这样试图修改时编译器会报错，而不是等到运行时崩溃：

```c
const char* s = "Hello";
```

## 3. string.h 中的常用函数

```c
#include <stdio.h>
#include <string.h>

int main(void)
{
    char buf[32];
    strcpy(buf, "Hello");                        // 复制
    strcat(buf, ", World");                      // 拼接
    printf("buf = \"%s\"，长度 %zu\n", buf, strlen(buf));

    const char* a = "apple";
    const char* b = "banana";
    printf("strcmp(a, b) = %d\n", strcmp(a, b) < 0 ? -1 : 1);
    printf("strcmp(a, \"apple\") = %d\n", strcmp(a, "apple"));

    char input[16];
    strcpy(input, "apple");
    printf("input == \"apple\": %d，strcmp: %d\n", input == a, strcmp(input, a));

    const char* found = strstr(buf, "World");    // 查找子串
    printf("strstr: \"%s\"，位置 %td\n", found, found - buf);
    const char* comma = strchr(buf, ',');        // 查找字符
    printf("strchr: 位置 %td\n", comma - buf);
    return 0;
}
```

运行结果：

```
buf = "Hello, World"，长度 12
strcmp(a, b) = -1
strcmp(a, "apple") = 0
input == "apple": 0，strcmp: 0
strstr: "World"，位置 7
strchr: 位置 5
```

| 函数 | 作用 |
|---|---|
| `strlen(s)` | 长度，不包括 `'\0'` |
| `strcpy(dst, src)` | 把 `src` 复制到 `dst`（包括 `'\0'`） |
| `strcat(dst, src)` | 把 `src` 接到 `dst` 的末尾 |
| `strcmp(a, b)` | 按字典顺序比较：`a < b` 返回负数，相等返回 0，`a > b` 返回正数 |
| `strchr(s, c)` | 查找字符 `c` 第一次出现的位置，返回指针，找不到返回 `NULL` |
| `strstr(s, sub)` | 查找子串，返回指针，找不到返回 `NULL` |

- **比较字符串内容要用 `strcmp`，不能用 `==`**。上面 `input` 和 `a` 的内容都是 `"apple"`，但 `input == a` 是 0，因为 `==` 比较的是两个**地址**（和《数组》中数组不能用 `==` 比较是同一个道理）。
- `strcmp` 只保证返回值的**符号**，不保证是 -1 或 1，所以要写 `strcmp(a, b) < 0`，而不是 `strcmp(a, b) == -1`。
- `strchr` 和 `strstr` 返回的是指针，和字符串的起始地址相减，就得到了下标。

## 4. 缓冲区溢出

`strcpy` 和 `strcat` **不检查目标数组有多大**，它们会一直复制，直到遇到源字符串的 `'\0'`。

```c
#include <stdio.h>
#include <string.h>

void login(const char* input)
{
    char role[8] = "guest";
    char user[8];
    strcpy(user, input);                      // 不检查长度
    printf("user = %s, role = %s\n", user, role);
}

int main(int argc, char* argv[])
{
    login(argc > 1 ? argv[1] : "tom");
    return 0;
}
```

`login` 把用户输入的名字复制到 8 个字节的 `user` 中，`role` 是这个用户的角色。先关闭 macOS 默认开启的保护（`-D_FORTIFY_SOURCE=0`，后面会解释），用正常的输入和一个精心构造的输入分别运行：

```bash
gcc -std=c11 -Wall -Wextra -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 login.c -o login
./login tom
./login AAAAAAAAadmin
```

```
user = tom, role = guest
user = AAAAAAAAadmin, role = admin
```

第二次运行，`role` 从 `guest` 变成了 `admin`！输入的前 8 个字符 `AAAAAAAA` 填满了 `user`，后面的 `admin` 和 `'\0'` 溢出到了紧挨着的 `role` 中。这就是**缓冲区溢出攻击**的基本原理：通过超长的输入改写程序中的其他数据。真实的攻击还可以改写函数的返回地址，让程序去执行攻击者指定的代码。这类漏洞在历史上造成了无数安全事故。

现代编译器和系统提供了一些保护。在 macOS 上默认编译（开启了 `_FORTIFY_SOURCE`）时，同样的输入会让程序在 `strcpy` 处检测到溢出并立即终止（退出码 133）。如果目标大小和源字符串都是编译时已知的，比如直接写 `strcpy(user, "administrator");`，编译器甚至会直接警告：

```
warning: 'strcpy' will always overflow; destination buffer has size 8, but the source string has length 14 (including NUL byte) [-Wfortify-source]
```

对这个版本，AddressSanitizer 也能准确地报告：

```
ERROR: AddressSanitizer: stack-buffer-overflow on address 0x00016cffa4a8
WRITE of size 14 at 0x00016cffa4a8 thread T0
    #0 0x0001034baaf0 in strcpy+0x458
    #1 0x000102e0495c in main overflow.c:8
```

但这些保护都不能代替**正确的代码**：不同的平台、不同的编译选项，保护是否存在都不一定。

### 4.1 安全的替代写法

```c
#include <stdio.h>
#include <string.h>

int main(void)
{
    char user[8];
    char role[8] = "guest";

    strncpy(user, "administrator", sizeof(user));     // 不会越界，但……
    printf("strncpy 之后 user 的最后一个字节: %d\n", user[sizeof(user) - 1]);

    int n = snprintf(user, sizeof(user), "%s", "administrator");   // 推荐
    printf("snprintf: user = \"%s\"，role = \"%s\"，需要 %d 个字符\n", user, role, n);
    if ((size_t)n >= sizeof(user)) {
        printf("结果被截断了\n");
    }

    char line[64];
    snprintf(line, sizeof(line), "%s 的成绩是 %d 分（%.1f%%）", "Tom", 92, 92.0);
    printf("%s\n", line);
    return 0;
}
```

运行结果：

```
strncpy 之后 user 的最后一个字节: 116
snprintf: user = "adminis"，role = "guest"，需要 13 个字符
结果被截断了
Tom 的成绩是 92 分（92.0%）
```

- **`strncpy` 不是安全版的 `strcpy`**：源字符串太长时，它复制了 n 个字符就停下，**不会添加 `'\0'`**。上面 `user` 的最后一个字节是 116（字符 `t`），而不是 0，`user` 已经不是一个合法的字符串了。
- **`snprintf(dst, size, 格式, ...)`** 是最推荐的写法：它**最多写入 size - 1 个字符，并且总是添加 `'\0'`**。返回值是「如果空间足够，本应写入的字符数」，大于等于 size 就说明结果被截断了。
- `snprintf` 同时也是拼接字符串、把数字转换为字符串的通用工具，比 `strcat` 加上一堆转换函数方便得多。

## 5. 读取一行：fgets

《输入与输出》中提到，`scanf("%s")` 遇到空格就停止，而且容易溢出。读取一整行应该用 **`fgets`**：

```c
#include <stdio.h>
#include <string.h>

int main(void)
{
    char line[16];
    while (fgets(line, sizeof(line), stdin) != NULL) {
        size_t len = strlen(line);
        int complete = len > 0 && line[len - 1] == '\n';
        if (complete) {
            line[len - 1] = '\0';                  // 去掉末尾的换行符
        }
        printf("读到 [%s]%s\n", line, complete ? "" : "（这一行还没有读完）");
    }
    return 0;
}
```

输入三行 `hello world`、`this line is definitely too long`、`ok`：

```
读到 [hello world]
读到 [this line is de]（这一行还没有读完）
读到 [finitely too lo]（这一行还没有读完）
读到 [ng]
读到 [ok]
```

- `fgets(buf, size, stdin)` **最多读取 size - 1 个字符**，并且总是添加 `'\0'`，不会溢出。读到文件末尾时返回 `NULL`。
- 和 `scanf` 不同，`fgets` 会**保留行末的换行符**，通常需要手动去掉。
- 如果一行比缓冲区长，`fgets` 只读取一部分，剩下的留给下一次读取。判断方法是看最后一个字符是不是 `'\n'`。

**不要使用 `gets`**。它没有缓冲区大小的参数，无法防止溢出，已经在 C11 中被正式删除了。

### 5.1 把字符串转换为数字

读入一行之后，经常需要把它转换为数字。`atoi` 很简单，但它无法报告错误：

```c
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

int parseInt(const char* s, int* out)
{
    char* end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s) return 0;                      // 一个数字都没有
    while (*end == ' ' || *end == '\n') ++end;
    if (*end != '\0') return 0;                  // 后面还有多余的字符
    if (errno == ERANGE || v < -2147483647L - 1 || v > 2147483647L) return 0;   // 超出范围
    *out = (int)v;
    return 1;
}
```

对不同的输入，比较 `atoi` 和 `parseInt`：

```
42             atoi = 42           parseInt = 42
"  -17\n"      atoi = -17          parseInt = -17
12abc          atoi = 12           parseInt 失败
abc            atoi = 0            parseInt 失败
99999999999    atoi = 1215752191   parseInt 失败
```

`atoi` 把 `"abc"` 转换为 0，调用者无法分辨这是输入了 0 还是输入错误；`"12abc"` 被悄悄地当成了 12；超出范围的数字更是得到了一个莫名其妙的值。

**`strtol(s, &end, 10)`** 按十进制转换，并通过 `end` 告诉我们**转换停在了哪里**（这里用到了上一篇的二级指针：`strtol` 要修改我们的指针变量 `end`）。如果 `end` 没有移动，说明一个数字都没有；如果 `end` 后面还有其他字符，说明输入不完全是数字；超出 `long` 的范围时，`errno` 被设置为 `ERANGE`。

**「先用 `fgets` 读一行，再用 `strtol` 解析」**，是比 `scanf` 可靠得多的输入方式。

## 6. 命令行参数

`main` 函数的完整形式可以接收**命令行参数**：

```c
#include <stdio.h>

int main(int argc, char* argv[])
{
    printf("argc = %d\n", argc);
    for (int i = 0; i < argc; ++i) {
        printf("argv[%d] = \"%s\"\n", i, argv[i]);
    }
    return 0;
}
```

```bash
./argv hello "two words" 42
```

```
argc = 4
argv[0] = "./argv"
argv[1] = "hello"
argv[2] = "two words"
argv[3] = "42"
```

- **`argc`** 是参数的个数，**`argv`** 是一个字符串数组（`char*` 的数组），`argv[0]` 是程序本身的名字。
- 参数之间用空格分隔，用引号括起来的部分算作一个参数。
- 所有的参数都是**字符串**，`"42"` 需要用 `strtol` 转换才能当作数字使用。
- 使用 `argv[i]` 之前要检查 `argc`，否则可能访问不存在的参数。第 4 节的 `login` 示例就是这样做的。

## 7. 常见错误

1. **没有给 `'\0'` 留空间**：n 个字符需要 n + 1 个字节。
2. **自己构造的字符数组忘了 `'\0'`**：字符串函数会越界读取。
3. **修改字符串字面量**：崩溃。指向字面量的指针用 `const char*`。
4. **用 `==` 比较字符串**：比较的是地址，用 `strcmp`。
5. **判断 `strcmp(a, b) == -1`**：应该判断 `< 0`。
6. **`strcpy` / `strcat` 的目标空间不够**：缓冲区溢出，实测 `role` 被改成了 `admin`。用 `snprintf`。
7. **以为 `strncpy` 一定会添加 `'\0'`**：源字符串太长时不会。
8. **使用 `gets`**：已被删除，用 `fgets`。
9. **用 `atoi` 转换用户输入**：无法发现错误，用 `strtol`。

## 8. 小结

- C 字符串是以 `'\0'` 结尾的字符数组，n 个字符需要 n + 1 个字节；`strlen` 不包括 `'\0'`
- 字符数组的内容可以修改；字符串字面量是只读的，用 `const char*` 指向它
- 比较内容用 `strcmp`，查找用 `strchr` / `strstr`，复制和拼接推荐用 `snprintf`
- `strcpy`、`strcat` 不检查长度，会造成缓冲区溢出；`strncpy` 可能不添加 `'\0'`
- 读一行用 `fgets`（不要用 `gets`），转换数字用 `strtol`（不要用 `atoi`）
- `main(int argc, char* argv[])` 接收命令行参数，参数都是字符串

## 9. 练习

1. 不使用 `strlen`，用指针实现 `size_t myStrlen(const char* s)`。
2. 写一个函数 `void reverse(char* s)`，在原地反转一个字符串，比如 `"Hello"` 变成 `"olleH"`。注意处理空字符串。
3. 写一个函数 `int isPalindrome(const char* s)`，判断一个字符串是否是回文（正着读和反着读一样），用 `"level"`、`"hello"`、`"a"`、`"abba"`、`""` 测试。

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```c
   size_t myStrlen(const char* s)
   {
       const char* p = s;
       while (*p != '\0') {
           ++p;
       }
       return (size_t)(p - s);
   }
   ```

   `p` 一直向后移动到 `'\0'`，两个指针相减就是长度。`myStrlen("Hello")` 返回 5，`myStrlen("")` 返回 0。

2. 参考代码：

   ```c
   void reverse(char* s)
   {
       size_t n = strlen(s);
       if (n < 2) return;
       for (size_t i = 0, j = n - 1; i < j; ++i, --j) {
           char t = s[i];
           s[i] = s[j];
           s[j] = t;
       }
   }
   ```

   必须先判断 `n < 2`：如果 `s` 是空字符串，`n - 1` 对于无符号的 `size_t` 会回绕成一个巨大的数。另外，传入的必须是字符数组（`char a[] = "Hello";`），不能是字符串字面量，否则修改时会崩溃。

3. 参考代码：

   ```c
   int isPalindrome(const char* s)
   {
       size_t n = strlen(s);
       for (size_t i = 0; i < n / 2; ++i) {
           if (s[i] != s[n - 1 - i]) {
               return 0;
           }
       }
       return 1;
   }
   ```

   `"level"`、`"a"`、`"abba"`、`""` 是回文，`"hello"` 不是。空字符串时 `n / 2` 是 0，循环不执行，直接返回 1。三个答案都用 `-fsanitize=address,undefined` 测试过。

</details>
