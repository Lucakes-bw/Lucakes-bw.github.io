---
title: 文件操作
date: 2026-10-23 18:20:00
updated: 2026-10-23 18:20:00
categories:
  - C/C++
  - C语言基础
tags:
  - C语言
  - 文件
  - IO
order: 14
description: 程序运行结束后，内存中的数据就消失了，要长期保存就得写入文件。本文介绍 fopen 的各种模式、文本文件的读写、用 errno 和 perror 处理错误、二进制文件的读写和随机访问，实测不关闭文件导致数据丢失，并用 goto 实现集中释放资源的文件复制函数。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用 `fopen` / `fclose` 打开和关闭文件，选择正确的打开模式；会用 `fprintf`、`fscanf`、`fgets` 读写文本文件；会用 `perror` 和 `errno` 报告错误；会用 `fwrite`、`fread`、`fseek` 读写二进制文件并随机访问；理解文件缓冲区；会用 `goto` 写出出错时也能正确释放资源的代码。
>
> **前置知识**：《字符串》、《动态内存》、《结构体、联合与枚举》。
>
> **编译环境**：`gcc -std=c11 -Wall -Wextra`。

## 1. 打开、读写、关闭

C 语言通过 **`FILE*`**（文件指针）操作文件，相关的函数都在 `<stdio.h>` 中。其实我们早就在用了：`printf` 写的是**标准输出** `stdout`，`scanf` 读的是**标准输入** `stdin`，它们也是 `FILE*`。

```c
#include <stdio.h>

int main(void)
{
    FILE* out = fopen("scores.txt", "w");             // 写入，文件不存在就创建，存在就清空
    if (out == NULL) {
        perror("打开 scores.txt 失败");
        return 1;
    }
    fprintf(out, "Alice 90\n");
    fprintf(out, "Bob 85\n");
    fprintf(out, "Carol 77\n");
    fclose(out);

    FILE* in = fopen("scores.txt", "r");              // 读取
    if (in == NULL) {
        perror("打开 scores.txt 失败");
        return 1;
    }
    char name[32];
    int score, total = 0, count = 0;
    while (fscanf(in, "%31s %d", name, &score) == 2) {
        printf("%s: %d\n", name, score);
        total += score;
        ++count;
    }
    fclose(in);
    if (count > 0) {
        printf("共 %d 人，平均分 %.1f\n", count, (double)total / count);
    }

    FILE* log = fopen("scores.txt", "a");             // 追加到末尾
    fprintf(log, "Dave 68\n");
    fclose(log);

    in = fopen("scores.txt", "r");
    char line[64];
    int lineNo = 0;
    while (fgets(line, sizeof(line), in) != NULL) {   // 逐行读取
        printf("第 %d 行: %s", ++lineNo, line);
    }
    fclose(in);
    return 0;
}
```

运行结果：

```
Alice: 90
Bob: 85
Carol: 77
共 3 人，平均分 84.0
第 1 行: Alice 90
第 2 行: Bob 85
第 3 行: Carol 77
第 4 行: Dave 68
```

（为了让示例简短，追加和第二次读取时省略了对 `fopen` 返回值的检查，实际代码中每次都要检查。）

- **`fopen(文件名, 模式)`** 打开文件，返回一个 `FILE*`。**失败时返回 `NULL`，必须检查**。
- **`fclose(f)`** 关闭文件。每个成功打开的文件都要关闭。
- **`fprintf(f, ...)`**、**`fscanf(f, ...)`** 和 `printf`、`scanf` 的用法完全一样，只是多了第一个参数，指定读写哪个文件。实际上 `printf(...)` 就等于 `fprintf(stdout, ...)`。
- **`fgets(buf, size, f)`** 读取一行，和《字符串》中从 `stdin` 读取的用法一样。
- 读取的循环条件和 `scanf` 一样：**检查返回值**。`fscanf` 返回成功读取的个数，`fgets` 在文件末尾返回 `NULL`。

常用的打开模式：

| 模式 | 含义 | 文件不存在时 | 文件已存在时 |
|---|---|---|---|
| `"r"` | 只读 | 失败 | 从头读取 |
| `"w"` | 只写 | 创建 | **清空原有内容** |
| `"a"` | 追加 | 创建 | 写在末尾，保留原有内容 |
| `"r+"` | 读写 | 失败 | 保留原有内容 |
| `"w+"` | 读写 | 创建 | 清空 |

在模式后面加上 **`b`**（如 `"rb"`、`"wb"`）表示二进制模式。在 Linux 和 macOS 上，文本模式和二进制模式没有区别；在 Windows 上，文本模式会把 `\n` 和 `\r\n` 相互转换，所以**读写二进制文件一定要加 `b`**。

**注意 `"w"` 会清空文件**。想要在已有的文件中添加内容，却用了 `"w"`，原来的数据就全部丢失了。

## 2. 错误处理：errno 和 perror

文件操作失败的原因很多：文件不存在、没有权限、磁盘满了……`fopen` 只返回 `NULL`，具体的原因记录在全局变量 **`errno`** 中（`<errno.h>`）：

```c
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    FILE* f = fopen("no_such_file.txt", "r");
    if (f == NULL) {
        perror("fopen");
        printf("errno = %d，含义: %s\n", errno, strerror(errno));
    }
    f = fopen("/no_permission.txt", "w");
    if (f == NULL) {
        perror("fopen /no_permission.txt");
    }
    return 0;
}
```

```
fopen: No such file or directory
errno = 2，含义: No such file or directory
fopen /no_permission.txt: Read-only file system
```

- **`perror(前缀)`** 把前缀和 `errno` 对应的错误描述输出到标准错误 `stderr`。
- **`strerror(errno)`** 返回错误描述的字符串，可以自己决定怎么输出。
- `errno` 只有在函数**报告失败之后**才有意义，而且下一次函数调用可能会改变它，所以要立即使用。

错误信息应该包含**出错的文件名**，比如 `perror(path)`，否则用户看到一句 `No such file or directory`，还是不知道是哪个文件找不到。

## 3. 文件缓冲区

`fprintf` 写入的数据并不会立即到达磁盘，而是先放在内存中的**缓冲区**里，攒够了一定的量，或者调用 `fclose`、`fflush` 时，才真正写入文件。这样可以大大减少访问磁盘的次数。但如果程序在关闭文件之前就被终止了呢？

```c
#include <signal.h>
#include <stdio.h>

int main(void)
{
    FILE* f = fopen("important.txt", "w");
    if (f == NULL) return 1;
    fprintf(f, "重要的数据\n");
    raise(SIGKILL);                         // 模拟程序在关闭文件之前被强制终止
}
```

```bash
./noclose
ls -l important.txt
```

程序被终止（退出码 137），`important.txt` 的大小是 **0 字节**：写入的数据还在缓冲区中，随着进程一起消失了。在 `fprintf` 之后加上一行 **`fflush(f);`**，同样被强制终止，文件的大小就是 16 字节了，数据被保存了下来。

- **`fclose`** 会先把缓冲区中的数据写入文件，再关闭文件。程序正常结束（从 `main` 返回或调用 `exit`）时，所有打开的文件也会被自动刷新和关闭。
- **`fflush(f)`** 立即把缓冲区中的数据写入文件。写日志时，如果希望程序崩溃前的日志不丢失，每写一条就 `fflush` 一次。
- 这和《开发环境与编程工具》中「程序崩溃时，重定向到文件的输出丢失」是同一个原因，也是 C++ 中 `std::endl` 比 `'\n'` 慢的原因（《字符串与 IO 流》）。

严格来说，`fclose` 也可能失败（比如刷新缓冲区时发现磁盘满了），重要的数据应该检查它的返回值，第 5 节的例子中就是这样做的。

## 4. 二进制文件

文本文件用 `fprintf` 把数据转换成文字保存。**二进制文件**则用 **`fwrite`** 把内存中的字节**原样**写入文件，用 **`fread`** 原样读回：

```c
#include <stdio.h>

typedef struct {
    int id;
    char name[16];
    double score;
} Record;

int main(void)
{
    Record recs[3] = {{1, "Alice", 90.5}, {2, "Bob", 85.0}, {3, "Carol", 77.25}};

    FILE* f = fopen("records.dat", "wb");
    if (f == NULL) { perror("fopen"); return 1; }
    size_t written = fwrite(recs, sizeof(Record), 3, f);    // 一次写入整个数组
    fclose(f);
    printf("写入 %zu 条记录，每条 %zu 字节\n", written, sizeof(Record));

    f = fopen("records.dat", "rb");
    if (f == NULL) { perror("fopen"); return 1; }
    fseek(f, 0, SEEK_END);                                  // 移动到文件末尾
    long size = ftell(f);                                   // 当前位置 = 文件大小
    printf("文件大小 %ld 字节，共 %ld 条记录\n", size, size / (long)sizeof(Record));

    Record r;
    fseek(f, 2 * (long)sizeof(Record), SEEK_SET);           // 直接跳到第 3 条
    if (fread(&r, sizeof(Record), 1, f) == 1) {
        printf("第 3 条: %d %s %.2f\n", r.id, r.name, r.score);
    }
    rewind(f);                                              // 回到开头
    while (fread(&r, sizeof(Record), 1, f) == 1) {
        printf("  %d %-6s %.2f\n", r.id, r.name, r.score);
    }
    fclose(f);
    return 0;
}
```

运行结果：

```
写入 3 条记录，每条 32 字节
文件大小 96 字节，共 3 条记录
第 3 条: 3 Carol 77.25
  1 Alice  90.50
  2 Bob    85.00
  3 Carol  77.25
```

- **`fwrite(地址, 每个元素的大小, 元素个数, 文件)`** 返回成功写入的**元素个数**；**`fread`** 的参数相同，返回成功读取的元素个数，读到末尾时小于请求的个数。
- **`fseek(f, 偏移, 起点)`** 移动读写位置，起点可以是 `SEEK_SET`（文件开头）、`SEEK_CUR`（当前位置）、`SEEK_END`（文件末尾）。**`ftell(f)`** 返回当前的位置。`rewind(f)` 回到开头。
- 每条记录的大小固定（32 字节，其中包括《结构体、联合与枚举》中讲的填充），所以第 n 条记录的位置就是 `n * sizeof(Record)`，可以**直接跳过去读取**，不需要从头逐条读。这叫**随机访问**，是数据库文件的基本原理。

### 4.1 文本文件和二进制文件的对比

把同样的 5 个整数 `{1, 22, 333, 4444, 55555}` 分别用两种方式保存：

```c
for (int i = 0; i < 5; ++i) fprintf(t, "%d\n", nums[i]);    // 文本
fwrite(nums, sizeof(int), 5, b);                            // 二进制
```

两个文件恰好都是 20 字节。用 `xxd` 查看二进制文件的内容：

```
00000000: 0100 0000 1600 0000 4d01 0000 5c11 0000  ........M...\...
00000010: 03d9 0000                                ....
```

每个 `int` 占 4 个字节，低位字节在前（小端序）：`16 00 00 00` 是 0x16 = 22，`03 d9 00 00` 是 0xd903 = 55555。而文本文件可以直接用 `cat` 或者任何编辑器查看。

| | 文本文件 | 二进制文件 |
|---|---|---|
| 可读性 | 人可以直接阅读和编辑 | 需要专门的程序 |
| 大小 | 取决于数字的位数 | 固定（每个 `int` 4 字节） |
| 读写速度 | 需要在数字和文字之间转换 | 直接复制内存，更快 |
| 随机访问 | 每行长度不同，难以直接定位 | 记录大小固定，可以直接跳转 |
| 可移植性 | 好 | 差：依赖于字节序、类型大小、结构体填充 |

配置文件、日志、需要交换的数据通常用文本格式（或者 JSON、CSV 等）；图片、音频、数据库等对大小和速度有要求的场合用二进制格式。

## 5. 集中释放资源：goto cleanup

一个函数中打开了多个文件、申请了内存，中途任何一步都可能失败。每个失败的分支都要释放**在它之前**已经获得的所有资源，很容易遗漏。《流程控制》中提到，`goto` 的一个被广泛接受的用法就是解决这个问题：

```c
#include <stdio.h>
#include <stdlib.h>

int copyFile(const char* src, const char* dst)
{
    int ok = 0;
    FILE* in = NULL;
    FILE* out = NULL;
    char* buf = NULL;

    in = fopen(src, "rb");
    if (in == NULL) { perror(src); goto cleanup; }
    out = fopen(dst, "wb");
    if (out == NULL) { perror(dst); goto cleanup; }
    buf = malloc(4096);
    if (buf == NULL) { fprintf(stderr, "内存不足\n"); goto cleanup; }

    size_t n;
    while ((n = fread(buf, 1, 4096, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            perror("写入失败");
            goto cleanup;
        }
    }
    if (ferror(in)) { perror("读取失败"); goto cleanup; }
    ok = 1;

cleanup:                                    // 无论从哪里跳过来，都会释放所有资源
    free(buf);
    if (out != NULL && fclose(out) != 0) { perror("关闭失败"); ok = 0; }
    if (in != NULL) fclose(in);
    return ok;
}

int main(int argc, char* argv[])
{
    if (argc != 3) {
        fprintf(stderr, "用法: %s 源文件 目标文件\n", argv[0]);
        return 1;
    }
    if (!copyFile(argv[1], argv[2])) {
        return 1;
    }
    printf("复制完成\n");
    return 0;
}
```

分别测试正常复制、源文件不存在、目标目录不存在三种情况：

```bash
./copy records.dat backup.dat && cmp records.dat backup.dat && echo "内容一致"
./copy missing.dat x.dat
./copy records.dat /no_dir/x.dat
```

```
复制完成
内容一致
missing.dat: No such file or directory
/no_dir/x.dat: No such file or directory
```

后两种情况的退出码都是 1。第三种情况中，源文件已经成功打开了，然后才失败，用 `leaks` 检查：`0 leaks for 0 total leaked bytes.`，打开的源文件和其他资源都被正确释放了。

这种写法的要点：

1. 所有资源变量在开头**初始化为 `NULL`**。
2. 任何一步失败，都 **`goto cleanup`**。
3. `cleanup` 部分**按相反的顺序**释放资源，只释放已经获得的（`free(NULL)` 是安全的，`fclose(NULL)` 不是，所以要检查）。
4. 函数只有**一个出口**。

Linux 内核等大型 C 项目中大量使用这种模式。C++ 的 RAII（析构函数、智能指针、`std::fstream`）则让编译器自动完成了这些工作。

## 6. 常见错误

1. **不检查 `fopen` 的返回值**：文件打开失败时，对 `NULL` 调用 `fprintf` 会崩溃。
2. **用 `"w"` 打开已有的文件**：原有内容被清空。追加用 `"a"`，修改用 `"r+"`。
3. **忘记 `fclose`**：缓冲区中的数据可能丢失，打开的文件数量也有上限。
4. **错误信息中没有文件名**：用户不知道是哪个文件出了问题。
5. **在 Windows 上用文本模式读写二进制文件**：数据被破坏。二进制文件总是用 `"rb"` / `"wb"`。
6. **不检查 `fread` / `fwrite` 的返回值**：读到末尾或写入失败时没有察觉。
7. **用 `while (!feof(f))` 控制读取循环**：`feof` 只在一次读取**失败之后**才变为真，这样写会多处理一次最后的数据。应该用读取函数的返回值控制循环。
8. **在不同的平台之间直接交换二进制文件**：字节序、类型大小、结构体填充可能不同。

## 7. 小结

- `fopen` 打开文件，失败返回 `NULL`；`fclose` 关闭文件；`"w"` 会清空文件，`"a"` 追加，二进制文件加 `b`
- `fprintf` / `fscanf` / `fgets` 读写文本文件，用法和标准输入输出一样；用返回值控制读取循环
- 失败的原因在 `errno` 中，用 `perror` 或 `strerror` 输出，并带上文件名
- 写入的数据先进入缓冲区；没有 `fclose` 或 `fflush` 就被终止，实测数据全部丢失
- `fwrite` / `fread` 原样读写内存，`fseek` / `ftell` 实现随机访问；二进制文件快、紧凑，但不可移植
- 多个资源时用 `goto cleanup` 集中释放，保证每条失败路径都不泄漏

## 8. 练习

1. 写一个函数 `int filterScores(const char* src, const char* dst, int minScore)`，从第 1 节的 `scores.txt` 中读取每个人的成绩，把不低于 `minScore` 的写入另一个文件，返回写入的人数，出错时返回 -1。
2. 写一个函数 `int updateScore(const char* path, int index, double newScore)`，修改第 4 节 `records.dat` 中第 `index` 条记录（从 0 开始）的成绩，不能重写整个文件。
3. 把第 4 节的 `records.dat` 复制到另一台电脑上，用相同的代码读取，可能会遇到什么问题？

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```c
   int filterScores(const char* src, const char* dst, int minScore)
   {
       FILE* in = fopen(src, "r");
       if (in == NULL) { perror(src); return -1; }
       FILE* out = fopen(dst, "w");
       if (out == NULL) { perror(dst); fclose(in); return -1; }
       char name[32];
       int score, kept = 0;
       while (fscanf(in, "%31s %d", name, &score) == 2) {
           if (score >= minScore) {
               fprintf(out, "%s %d\n", name, score);
               ++kept;
           }
       }
       fclose(in);
       fclose(out);
       return kept;
   }
   ```

   对第 1 节运行后的 `scores.txt`（4 个人），`filterScores("scores.txt", "good.txt", 80)` 返回 2，`good.txt` 中是 Alice 90 和 Bob 85。注意第二个 `fopen` 失败时，要先关闭已经打开的 `in`。

2. 用 `"r+b"` 模式打开（读写，不清空），定位到这条记录，读出、修改、再写回：

   ```c
   int updateScore(const char* path, int index, double newScore)
   {
       FILE* f = fopen(path, "r+b");                 // 读写，不清空
       if (f == NULL) { perror(path); return 0; }
       Record r;
       long offset = (long)index * (long)sizeof(Record);
       if (fseek(f, offset, SEEK_SET) != 0 || fread(&r, sizeof r, 1, f) != 1) {
           fclose(f);
           return 0;
       }
       r.score = newScore;
       fseek(f, offset, SEEK_SET);                   // 读完之后位置后移了，要回到这条记录的开头
       int ok = fwrite(&r, sizeof r, 1, f) == 1;
       return fclose(f) == 0 && ok;
   }
   ```

   `updateScore("records.dat", 1, 99.5)` 之后，Bob 的成绩变成了 99.50，文件大小仍然是 96 字节；`updateScore("records.dat", 5, 1.0)` 因为没有第 6 条记录，`fread` 失败，返回 0。另外，C 标准规定在读和写之间切换时，必须调用 `fseek` 或 `fflush`，这里的第二个 `fseek` 正好满足了这个要求。

3. 如果另一台电脑的**字节序**不同（大端序），每个 `int` 和 `double` 的字节顺序都会被读反；如果 `int` 的**大小**不同，或者编译器对结构体的**填充**方式不同，每条记录的大小和成员的位置都会对不上。需要在不同平台之间交换数据时，应该使用文本格式，或者明确规定每个字段的大小（用 `int32_t` 等类型）和字节序，逐个字段地读写，而不是直接 `fwrite` 整个结构体。

</details>
