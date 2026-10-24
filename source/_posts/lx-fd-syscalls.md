---
title: 文件描述符与系统调用
date: 2026-10-24 10:30:00
updated: 2026-10-24 10:30:00
categories:
  - Linux基础
  - 文件IO
tags:
  - Linux
  - 系统调用
  - 文件IO
order: 1
description: open、read、write、close 是 Linux 文件 IO 的基础。本文用它们写一个简易的 cat，用 strace 看程序执行了哪些系统调用；用演示讲清楚文件描述符表、打开文件表和 inode 的关系；实测复制 16 MB 的文件，缓冲区为 1 字节时 3355 万次系统调用、6.7 秒，4 KB 时只要 0.006 秒；最后解释 stdio 的缓冲为什么让 printf 和 write 的输出顺序颠倒。
top_group_list: true
disableNunjucks: true
viz: [lx-fd]
---

> **本文目标**：理解什么是系统调用；会用 `open`、`read`、`write`、`close` 读写文件，并正确处理错误；会用 `strace` 查看程序的系统调用；理解文件描述符、打开文件表、inode 三者的关系；理解缓冲区大小对性能的影响，以及 stdio 缓冲的行为。
>
> **前置知识**：C 语言的《文件操作》（`fopen`、`fread` 等）；《进程与系统监控》中的 `/proc/PID/fd`。
>
> **实验环境**：Ubuntu 24.04 容器（aarch64），GCC 13.3，strace 6.8。

## 1. 系统调用

C 语言中读写文件用的 `fopen`、`fread`、`fprintf` 来自 C 标准库，它们在任何操作系统上都能用。但标准库自己并不能读写磁盘，真正操作硬件的是**内核**。程序要请内核做事，就要通过**系统调用**（system call）：一种特殊的函数调用，CPU 从用户态切换到内核态，执行内核中的代码，完成后再切换回来。

```
你的程序  →  C 标准库（fopen、printf……）  →  系统调用（open、write……）  →  内核  →  磁盘、终端
```

Linux 提供了几百个系统调用。和文件有关的最基本的四个：

| 系统调用 | 原型 | 作用 |
|---|---|---|
| `open` | `int open(const char *path, int flags, ... /* mode */);` | 打开文件，返回一个**文件描述符** |
| `read` | `ssize_t read(int fd, void *buf, size_t count);` | 最多读 `count` 个字节，返回实际读到的字节数，0 表示文件结束 |
| `write` | `ssize_t write(int fd, const void *buf, size_t count);` | 写入，返回实际写入的字节数 |
| `close` | `int close(int fd);` | 关闭 |

它们在头文件 `<fcntl.h>` 和 `<unistd.h>` 中声明。严格地说，C 程序中调用的 `open` 也是 glibc 中的一个函数，它只是把参数放进寄存器，然后执行一条特殊的指令进入内核，这样的函数叫作系统调用的**包装函数**。

**文件描述符**（file descriptor，fd）就是一个小的非负整数，代表进程打开的一个文件。之后的 `read`、`write`、`close` 都通过它来指定文件。每个进程启动时已经打开了三个：

| fd | 名字 | 对应的 stdio |
|---|---|---|
| 0 | 标准输入 `STDIN_FILENO` | `stdin` |
| 1 | 标准输出 `STDOUT_FILENO` | `stdout` |
| 2 | 标准错误 `STDERR_FILENO` | `stderr` |

## 2. 写一个简易的 cat

```c
// mycat.c
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "用法：%s 文件\n", argv[0]);
        return 2;
    }
    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        perror(argv[1]);
        return 1;
    }
    printf("fd = %d\n", fd);
    fflush(stdout);

    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        if (write(STDOUT_FILENO, buf, n) != n) {
            perror("write");
            return 1;
        }
    }
    if (n == -1) {
        perror("read");
        return 1;
    }
    close(fd);
    return 0;
}
```

```bash
gcc -Wall -O2 mycat.c -o mycat
printf "hello\nworld\n" > a.txt
./mycat a.txt
```

```
fd = 3
hello
world
```

- `open` 的第二个参数是**标志**：`O_RDONLY` 只读、`O_WRONLY` 只写、`O_RDWR` 读写，可以用 `|` 加上 `O_CREAT`（不存在时创建）、`O_TRUNC`（清空）、`O_APPEND`（追加）等；
- 打开的文件得到的描述符是 **3**，因为 0、1、2 已经被占用了；
- `read` 返回 0 表示读到了文件末尾，返回 -1 表示出错；
- `ssize_t` 是有符号的 `size_t`，这样才能返回 -1。

### 2.1 错误处理：errno 和 perror

系统调用失败时返回 -1，并把错误的原因（一个整数）存进全局变量 **`errno`**。`perror` 打印出这个原因对应的文字：

```bash
./mycat nope.txt; echo "exit=$?"
./mycat /etc/shadow; echo "exit=$?"
./mycat /etc; echo "exit=$?"
```

```
nope.txt: No such file or directory
exit=1
/etc/shadow: Permission denied
exit=1
fd = 3
read: Is a directory
exit=1
```

| 情况 | errno | 文字 |
|---|---|---|
| 文件不存在 | `ENOENT` | No such file or directory |
| 没有权限（《用户、权限与软件包》） | `EACCES` | Permission denied |
| 对目录调用 `read` | `EISDIR` | Is a directory |

注意最后一种：目录可以用 `open` 打开，`read` 时才失败。**每个系统调用的返回值都要检查**，`man 2 open` 的 ERRORS 一节列出了它可能返回的所有错误（第 2 节是系统调用，第 3 节是库函数）。

## 3. strace：看程序执行了哪些系统调用

**strace** 记录一个程序执行的所有系统调用、参数和返回值，是 Linux 上排查问题的利器。`-o` 把结果写入文件，`-e trace=` 只看指定的几种：

```bash
strace -o trace.txt -e trace=openat,read,write,close ./mycat a.txt > /dev/null
cat trace.txt
```

```
openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3
close(3)                                = 0
openat(AT_FDCWD, "/lib/aarch64-linux-gnu/libc.so.6", O_RDONLY|O_CLOEXEC) = 3
read(3, "\177ELF\2\1\1\3\0\0\0\0\0\0\0\0\3\0\267\0\1\0\0\0\360\206\2\0\0\0\0\0"..., 832) = 832
close(3)                                = 0
openat(AT_FDCWD, "a.txt", O_RDONLY)     = 3
write(1, "fd = 3\n", 7)                 = 7
read(3, "hello\nworld\n", 4096)         = 12
write(1, "hello\nworld\n", 12)          = 12
read(3, "", 4096)                       = 0
close(3)                                = 0
+++ exited with 0 +++
```

- 前 5 行发生在 `main` 之前：动态链接器查缓存 `/etc/ld.so.cache`，打开 `libc.so.6` 并读取它的 ELF 头（`\177ELF`），这正是《静态库与动态库》中讲的加载过程；
- 然后是我们的代码：`open` 实际调用的是 `openat`（`AT_FDCWD` 表示相对于当前目录），返回 3；
- 第一次 `read` 读到 12 个字节，第二次返回 0，文件结束。

不加 `-e` 时会看到全部 40 个系统调用，大部分是程序启动时的内存映射（`mmap`）等准备工作。`strace -c` 统计每种系统调用的次数，`strace -p PID` 可以连接到正在运行的进程，看它卡在了哪个系统调用上。

## 4. 文件描述符表、打开文件表和 inode

文件描述符只是一个整数，它背后是内核中的三层结构：

| 层 | 归属 | 记录的内容 |
|---|---|---|
| **文件描述符表** | 每个进程一张 | 下标是 fd，每一项指向打开文件表中的一项 |
| **打开文件表** | 内核中所有进程共用 | 每次 `open` 新建一项：**当前偏移**（读写到了哪里）、打开方式（只读、追加……）、引用计数 |
| **inode** | 每个文件一个 | 文件的大小、权限、数据在磁盘上的位置（《文件与目录操作》） |

用下面的程序验证：同一个文件打开两次，再关闭一个、打开另一个文件：

```c
// fdtable.c
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void show(const char *what)
{
    printf("--- %s\n", what);
    fflush(stdout);
    char cmd[128];
    snprintf(cmd, sizeof cmd, "ls -l /proc/%d/fd | awk 'NR > 1 {print $9, $10, $11}'", getpid());
    if (system(cmd) != 0) {
        perror("system");
    }
}

int main(void)
{
    char buf[8] = {0};

    int a = open("a.txt", O_RDONLY);
    int b = open("a.txt", O_RDONLY);           // 同一个文件再打开一次
    printf("a = %d, b = %d\n", a, b);
    show("打开两次之后");

    if (read(a, buf, 5) != 5) return 1;
    printf("从 a 读到 \"%s\"，a 的位置 %ld，b 的位置 %ld\n", buf, (long)lseek(a, 0, SEEK_CUR), (long)lseek(b, 0, SEEK_CUR));
    if (read(b, buf, 5) != 5) return 1;
    printf("从 b 读到 \"%s\"\n", buf);

    close(a);
    int c = open("/etc/hostname", O_RDONLY);
    printf("关闭 a 之后再打开一个文件：c = %d\n", c);
    show("关闭 a、打开 c 之后");
    return 0;
}
```

（`lseek(fd, 0, SEEK_CUR)` 返回当前的偏移；`show` 用 `ls -l /proc/PID/fd` 列出进程打开的文件。）在终端中运行：

```
a = 3, b = 4
--- 打开两次之后
0 -> /dev/pts/0
1 -> /dev/pts/0
2 -> /dev/pts/0
3 -> /home/lucky/ch3/fd/a.txt
4 -> /home/lucky/ch3/fd/a.txt
从 a 读到 "hello"，a 的位置 5，b 的位置 0
从 b 读到 "hello"
关闭 a 之后再打开一个文件：c = 3
--- 关闭 a、打开 c 之后
0 -> /dev/pts/0
1 -> /dev/pts/0
2 -> /dev/pts/0
3 -> /etc/hostname
4 -> /home/lucky/ch3/fd/a.txt
```

两个现象：

1. 从 `a` 读了 5 个字节后，`a` 的位置是 5，`b` 的位置仍然是 0，从 `b` 读到的还是 `hello`。**偏移不是文件的属性**，而是记录在打开文件表的每一项中，两次 `open` 各有一项，互不影响；
2. 关闭 `a`（3）之后，新打开的文件得到的描述符又是 3。**`open` 总是返回当前最小的空闲描述符。**

下面的演示逐步展示了这个过程。点「运行示例」，或者自己 open、read、close，观察三张表的变化：

<div class="dsviz" data-viz="fd-table"></div>

0、1、2 共用打开文件表中的同一项（引用 3）：Shell 打开终端后，用复制描述符的方法让三个描述符指向它，第 3.3 篇会讲这个方法。引用计数的意义在于：一项可以被多个描述符共享，只有引用计数变为 0，这一项才真正被删除。`fork` 出的子进程也会和父进程共享这些项（第 4.1 篇），所以父子进程写同一个文件时，偏移是共享的。

**一个进程能打开的描述符数量是有限的**：`ulimit -n` 查看，实验容器中是 1024。打开文件后忘记 `close`，描述符会越积越多（文件描述符泄漏），长时间运行的服务器最终会因为 `Too many open files`（`EMFILE`）而无法打开新的文件或网络连接。

## 5. 缓冲区大小和性能

每次系统调用都要在用户态和内核态之间切换，有固定的开销。`mycat` 每次读 4096 个字节，如果每次只读 1 个字节会怎样？写一个可以指定缓冲区大小的复制程序：

```c
// copy.c
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr, "用法：%s 源文件 目标文件 缓冲区大小\n", argv[0]);
        return 2;
    }
    size_t size = strtoul(argv[3], NULL, 10);
    char *buf = malloc(size);
    int in = open(argv[1], O_RDONLY);
    int out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (in == -1 || out == -1 || buf == NULL) {
        perror("open");
        return 1;
    }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    long calls = 0;
    ssize_t n;
    while ((n = read(in, buf, size)) > 0) {
        if (write(out, buf, n) != n) {
            perror("write");
            return 1;
        }
        calls += 2;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double sec = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    printf("缓冲区 %6zu 字节：%9ld 次系统调用，%7.3f 秒\n", size, calls, sec);
    close(in);
    close(out);
    free(buf);
    return 0;
}
```

`open` 的第三个参数 `0644` 是新建文件时的权限，只在有 `O_CREAT` 时使用，实际的权限还要去掉 `umask` 中的位（《用户、权限与软件包》）。复制一个 16 MB 的文件：

```bash
head -c 16M /dev/urandom > data.bin
for s in 1 16 256 4096 65536 1048576; do ./copy data.bin out.bin $s; done
```

```
缓冲区      1 字节： 33554432 次系统调用，  6.680 秒
缓冲区     16 字节：  2097152 次系统调用，  0.426 秒
缓冲区    256 字节：   131072 次系统调用，  0.030 秒
缓冲区   4096 字节：     8192 次系统调用，  0.006 秒
缓冲区  65536 字节：      512 次系统调用，  0.003 秒
缓冲区 1048576 字节：       32 次系统调用，  0.003 秒
```

- 缓冲区为 1 字节时，16 MB 要读写各 1677 万次，共 3355 万次系统调用，用了 6.68 秒，平均每次约 0.2 微秒；
- 缓冲区每大 16 倍，系统调用次数就少 16 倍，时间也接近按比例减少；
- 到 4 KB 时只要 0.006 秒，比 1 字节快了 1000 多倍；再增大，提升就很小了，这时的时间主要花在复制数据本身上。

所以**读写文件时要使用足够大的缓冲区**，4 KB 到 64 KB 是常见的选择。（测试的文件刚刚写入，内容都在内存的页缓存中，所以这里测的是系统调用的开销，没有包括磁盘的速度。页缓存在第 3.4 篇讲。）

## 6. stdio 的缓冲

C 标准库的 `fgetc`、`fputc` 每次只处理一个字符，会不会也这么慢？

```c
// scopy.c 的主要部分
int c;
while ((c = fgetc(in)) != EOF) {      // 每次只读 1 个字节
    fputc(c, out);
}
fclose(out);
```

```
fgetc/fputc：0.053 秒
```

只要 0.053 秒，比每次 1 字节的 `read`/`write` 快了 100 多倍。用 strace 统计它的系统调用：

```bash
strace -c -e trace=read,write ./scopy data.bin out2.bin
```

```
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 53.68    0.008451           2      4097           write
 46.32    0.007292           1      4098           read
------ ----------- ----------- --------- --------- ----------------
100.00    0.015743           1      8195           total
```

只有 4098 次 `read` 和 4097 次 `write`。16 MB / 4096 = 4096，复制数据正好用了 4096 次 `read` 和 4096 次 `write`，每次 4096 字节；`read` 多出的两次是程序启动时读取 libc 的 ELF 头和最后读到文件末尾返回 0 的那一次，`write` 多出的一次是最后 `printf` 输出计时结果。**`FILE` 内部有一个缓冲区**：`fgetc` 第一次被调用时，用一次 `read` 读入 4096 字节，之后的 4095 次 `fgetc` 直接从缓冲区中取，不需要系统调用；`fputc` 也是先写进缓冲区，满了才 `write` 一次。

stdio 的缓冲让「一个字符一个字符地处理」也足够快。不过每个字节仍然要调用一次 `fgetc` 和 `fputc`，所以 0.053 秒还是比直接用 4 KB 缓冲区 `read` 的 0.006 秒慢。

### 6.1 printf 和 write 的输出顺序

缓冲也会带来让人困惑的现象：

```c
// order.c
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    printf("1. printf\n");
    write(STDOUT_FILENO, "2. write\n", 9);
    printf("3. printf\n");
    return 0;
}
```

```bash
./order                 # 输出到终端
./order > o.txt; cat o.txt
./order | cat
```

```
1. printf
2. write
3. printf
```

```
2. write
1. printf
3. printf
```

```
2. write
1. printf
3. printf
```

输出到终端时顺序正常，输出到文件或管道时，`write` 的那一行跑到了最前面。strace 看得很清楚：

```
write(1, "2. write\n", 9)               = 9
write(1, "1. printf\n3. printf\n", 20)  = 20
```

两次 `printf` 的内容都留在了缓冲区中，程序结束时才一起用一次 `write` 写出；而 `write` 是直接的系统调用，立即写出。`stdout` 的缓冲方式取决于它连接的是什么：

| `stdout` 连接到 | 缓冲方式 | 什么时候真正写出 |
|---|---|---|
| 终端 | **行缓冲** | 遇到 `\n`、缓冲区满、`fflush`、程序正常结束 |
| 文件、管道 | **全缓冲** | 缓冲区满、`fflush`、程序正常结束 |
| （`stderr` 总是） | **无缓冲** | 立即 |

所以：

- **不要混用 stdio 和 `read`/`write` 操作同一个文件**；必须混用时，在切换前调用 `fflush`。`mycat.c` 中 `printf` 之后的 `fflush(stdout)` 就是这个作用；
- 程序崩溃时，缓冲区中还没写出的内容会丢失（《开发环境与编程工具》中重定向到文件后输出为空，就是这个原因）；调试信息应该输出到 `stderr`；
- 用 `setvbuf` 可以修改缓冲方式。

## 7. 常见错误

1. **不检查返回值**：`open` 失败返回 -1，接着用 -1 去 `read`，得到 `EBADF`，真正的原因被掩盖了。
2. **以为 `read` 一定读满**：`read(fd, buf, 4096)` 可能只返回 100，比如读到文件末尾、从管道或网络读取时。要用循环，以返回值为准。`write` 也可能只写入一部分。
3. **把 `read` 读到的数据当作字符串**：`read` 不会在末尾加 `\0`。
4. **忘记 `close`**：描述符泄漏，最终 `Too many open files`。
5. **`O_CREAT` 不给第三个参数**：新文件的权限是栈上的随机值。
6. **每次读写 1 个字节**：比用 4 KB 的缓冲区慢 1000 多倍。
7. **混用 `printf` 和 `write`**：输出顺序错乱。

## 8. 小结

- 系统调用是程序请求内核服务的接口；`open` 返回文件描述符，`read`、`write`、`close` 通过它操作文件；失败返回 -1，原因在 `errno` 中，用 `perror` 打印
- `strace` 显示程序执行的系统调用，`-e trace=` 过滤，`-c` 统计，`-p` 连接到运行中的进程
- 三层结构：进程的文件描述符表 → 内核的打开文件表（偏移、打开方式、引用计数）→ inode；`open` 返回最小的空闲描述符
- 系统调用有固定开销：16 MB 文件，1 字节缓冲区 6.68 秒，4 KB 缓冲区 0.006 秒
- stdio 自带缓冲区，`fgetc` 也只是每 4096 字节调用一次 `read`；`stdout` 接终端时行缓冲，接文件或管道时全缓冲，不要和 `write` 混用

## 9. 练习

1. 用 `open`、`read`、`write` 实现 `cp 源文件 目标文件`。
2. 下面的程序输出到终端和输出到文件时，分别会输出什么？

   ```c
   #include <stdio.h>
   #include <stdlib.h>

   int main(void)
   {
       printf("start");
       abort();         // 异常终止
   }
   ```
3. 用 strace 统计 `ls -l /usr/bin > /dev/null` 执行了多少次系统调用，哪种最多？

<details>
<summary>点击查看答案</summary>

1. 和 `copy.c` 的核心相同，注意处理 `write` 只写了一部分的情况：

   ```c
   #include <fcntl.h>
   #include <stdio.h>
   #include <unistd.h>

   static int write_all(int fd, const char *buf, ssize_t n)
   {
       while (n > 0) {
           ssize_t w = write(fd, buf, n);
           if (w == -1) {
               return -1;
           }
           buf += w;
           n -= w;
       }
       return 0;
   }

   int main(int argc, char *argv[])
   {
       if (argc != 3) {
           fprintf(stderr, "用法：%s 源文件 目标文件\n", argv[0]);
           return 2;
       }
       int in = open(argv[1], O_RDONLY);
       if (in == -1) {
           perror(argv[1]);
           return 1;
       }
       int out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
       if (out == -1) {
           perror(argv[2]);
           return 1;
       }
       char buf[65536];
       ssize_t n;
       while ((n = read(in, buf, sizeof buf)) > 0) {
           if (write_all(out, buf, n) == -1) {
               perror("write");
               return 1;
           }
       }
       if (n == -1) {
           perror("read");
           return 1;
       }
       close(in);
       close(out);
       return 0;
   }
   ```

   ```
   $ ./mycp data.bin copy.bin && cmp data.bin copy.bin && echo same
   same
   $ ./mycp nope.txt x
   nope.txt: No such file or directory
   ```

2. 两种情况都**不会输出 `start`**。`start` 后面没有 `\n`，即使连接到终端（行缓冲），它也还在缓冲区中；`abort` 让进程被 SIGABRT 信号终止，不会执行正常退出时刷新缓冲区的步骤。实测在终端中运行，只显示 Shell 的提示：

   ```
   Aborted (core dumped)
   exit=134
   ```

   重定向到文件时，`ab.txt` 是 0 字节。退出码 134 = 128 + 6（SIGABRT）；`(core dumped)` 是因为实验容器打开了 core dump（《GDB 与 core dump》），`ulimit -c 0` 时只显示 `Aborted`。把 `printf` 换成 `fprintf(stderr, ...)`，或者在 `abort` 前 `fflush(stdout)`，`start` 就能输出。

3. ```bash
   strace -c ls -l /usr/bin > /dev/null
   ```

   ```
   % time     seconds  usecs/call     calls    errors syscall
   ------ ----------- ----------- --------- --------- ----------------
    35.03    0.002635           4       537           statx
    22.88    0.001721           3       537       537 lgetxattr
    14.64    0.001101           2       436           listxattr
    14.44    0.001086         543         2           getdents64
     8.81    0.000663           6       101           readlinkat
   ...
   100.00    0.007522           4      1743       547 total
   ```

   共 1743 次系统调用。`/usr/bin` 中有 536 个文件，`statx`（获取文件的属性）调用了 537 次，基本上每个文件一次，它是最多的；`getdents64` 读取目录的内容，只用了 2 次。`lgetxattr` 查询文件的 SELinux 安全上下文（`ls` 依赖 `libselinux`，见《静态库与动态库》的练习 3），537 次全部返回 `ENODATA`，因为这些文件没有设置它。下一篇会用 `stat` 和读目录的函数自己实现一个 `ls -l`。

</details>
