---
title: 内存映射 mmap
date: 2026-10-24 10:45:00
updated: 2026-10-24 10:45:00
categories:
  - Linux基础
  - 文件IO
tags:
  - Linux
  - 文件IO
  - mmap
order: 4
description: mmap 把文件映射到进程的地址空间，访问内存就是访问文件。本文用 mmap 统计 320 MB 文件的行数，和 read 比较速度；看清页缓存：读一遍文件，buff/cache 就增加了 321 MB；用 MAP_SHARED 直接修改文件，对比 MAP_PRIVATE 的写时复制；映射 1 GB 的文件只占 0.1 MB 内存，访问 100 MB 后才占 100 MB；最后复现 mmap 特有的 Bus error。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用 `mmap` 读写文件；理解页缓存，以及 `read` 和 `mmap` 在数据路径上的区别；分清 `MAP_SHARED` 和 `MAP_PRIVATE`；理解映射是按需加载的（缺页）；知道 mmap 的适用场合和陷阱。
>
> **前置知识**：《文件描述符与系统调用》；《GCC 与 ELF 文件》中的 `/proc/PID/maps`。
>
> **实验环境**：Ubuntu 24.04 容器（aarch64），GCC 13.3，内存页大小 4096 字节（`getconf PAGESIZE`）。

## 1. 页缓存

在讲 mmap 之前，先看内核读文件时做了什么。磁盘比内存慢得多，所以内核会把读过的文件内容保存在内存中，下次再读同样的内容，就不必访问磁盘。这部分内存就是**页缓存**（page cache），以页（4 KB）为单位管理。

《进程与系统监控》中 `free` 显示的 `buff/cache`，主要就是页缓存。用 `fincore` 可以查看一个文件有多少页在缓存中。为了实验，写一个小程序请内核把文件从页缓存中丢掉（`posix_fadvise` 的 `POSIX_FADV_DONTNEED`，不需要 root 权限）：

```c
// evict.c：请内核把一个文件从页缓存中丢弃（不需要 root）
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd == -1) {
            perror(argv[i]);
            continue;
        }
        fdatasync(fd);
        posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
        close(fd);
    }
    return 0;
}
```

准备一个 320 MB、384 万行的日志文件（由《文本处理与管道》中的访问日志重复拼接而成），先把它从缓存中清除，再读一遍：

```bash
./evict big.log; fincore big.log; free -m | sed -n 2p
./mcount big.log                 # 读一遍文件（第 2 节的程序）
fincore big.log; free -m | sed -n 2p
```

```
RES PAGES   SIZE FILE
 0B     0 320.3M big.log
Mem:            1959         263         891           0         893        1696
3841940
   RES PAGES   SIZE FILE
320.3M 82000 320.3M big.log
Mem:            1959         257         576           0        1214        1702
```

读之前，`big.log` 在缓存中的页数是 0；读一遍之后，82000 页（82000 × 4096 = 320 MB）全部进入了缓存，`buff/cache` 从 893 MB 增加到 1214 MB，`free` 相应地减少了。但 `available`（可用内存）几乎没有变：页缓存随时可以被回收，给更需要内存的程序使用。这就是《进程与系统监控》中说「看 available，不要看 free」的原因。

所以**用 `read` 读文件时，数据走的路径是：磁盘 → 页缓存 → 复制到用户的缓冲区**。第二次读同一个文件时，第一步就省掉了。

## 2. mmap：把文件映射到内存

```c
#include <sys/mman.h>

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);
```

`mmap` 把文件 `fd` 从 `offset` 开始的 `length` 个字节，映射到进程的地址空间中，返回映射的起始地址。之后，**读写这块内存，就是读写文件**，不需要 `read`、`write`。

| 参数 | 常用值 |
|---|---|
| `addr` | `NULL`，由内核选择地址 |
| `prot` | `PROT_READ`、`PROT_WRITE`，要和 `open` 的方式一致 |
| `flags` | `MAP_SHARED`：修改写回文件，其他进程可见；`MAP_PRIVATE`：修改只对自己可见（第 4 节） |
| `offset` | 必须是页大小的整数倍，通常是 0 |

用 mmap 统计文件的行数：

```c
// mcount.c：用 mmap 统计文件的行数
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
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
    struct stat st;
    fstat(fd, &st);
    if (st.st_size == 0) {                       // 长度为 0 的映射会失败
        printf("0\n");
        return 0;
    }
    char *p = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    close(fd);                                   // 映射建立后就可以关闭描述符

    long lines = 0;
    for (off_t i = 0; i < st.st_size; i++) {     // 像访问数组一样访问文件的内容
        if (p[i] == '\n') lines++;
    }
    printf("%ld\n", lines);
    munmap(p, st.st_size);
    return 0;
}
```

作为对比，用 `read` 每次读 64 KB 的版本：

```c
// rcount.c：用 read 统计文件的行数
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
    static char buf[65536];
    long lines = 0;
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n') lines++;
        }
    }
    printf("%ld\n", lines);
    close(fd);
    return 0;
}
```

在文件已经在页缓存中的情况下，各运行两次：

```bash
/usr/bin/time -f "%e 秒（用户态 %U，内核态 %S），次缺页 %R" ./rcount big.log
/usr/bin/time -f "%e 秒（用户态 %U，内核态 %S），次缺页 %R" ./mcount big.log
```

| 程序 | 时间 | 用户态 | 内核态 | 次缺页 |
|---|---|---|---|---|
| `rcount`（read） | 0.09、0.08 秒 | 0.06、0.07 | 0.02、0.01 | 86 |
| `mcount`（mmap） | 0.08、0.07 秒 | 0.07、0.06 | 0.00、0.00 | 5196、5195 |

两者都输出 3841940 行。mmap 版本快了一点，主要差别在**内核态时间**：`read` 版本的内核要把 320 MB 从页缓存**复制**到用户的缓冲区，花了 0.01 到 0.02 秒；mmap 版本直接访问页缓存中的那些页，没有复制。

另一个差别是**次缺页**（minor page fault）的次数。`mmap` 返回时，内核只是记下「这段地址对应这个文件」，并没有真的把页面映射进来。程序第一次访问某一页时，CPU 发现这一页还没有映射，触发一次**缺页**，内核找到页缓存中对应的页，填进页表，程序再继续执行。320 MB 是 82000 页，却只有约 5200 次缺页：每次缺页时，内核会顺便把附近已在缓存中的页也一起映射（平均每次约 16 页），以减少缺页的次数。「次」表示数据已经在页缓存中，不需要读磁盘；需要读磁盘的叫**主缺页**。

> **关于冷缓存的测量**：清空页缓存后再运行，`rcount` 约 0.12 秒、`mcount` 约 0.11 秒，比缓存中的只慢 0.03 秒，`cat` 一遍这个文件也只要 0.04 秒（缓存中 0.01 秒），相当于每秒 8 GB。这么快是因为容器运行在虚拟机中，虚拟机的「磁盘」只是 Mac 上的一个文件，它的内容已经在 Mac 的内存中了。在真实的机器上从磁盘读取，冷缓存会慢得多，页缓存的作用也明显得多。

## 3. 映射是按需加载的

映射 1 GB 的文件，只访问前 100 MB，看看进程实际占用的物理内存（`VmRSS`）：

```c
// lazy.c：映射 1 GB 的文件，只访问其中一部分，观察实际占用的内存
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static void rss(const char *when)
{
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            printf("%-24s %s", when, line);
        }
    }
    fclose(f);
}

int main(void)
{
    size_t size = 1UL << 30;                     // 1 GB
    int fd = open("sparse.img", O_RDONLY);
    if (fd == -1) {
        perror("sparse.img");
        return 1;
    }
    rss("映射之前");
    volatile char *p = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    rss("映射 1 GB 之后");

    long sum = 0;
    for (size_t i = 0; i < 100UL * 1024 * 1024; i += 4096) {   // 只访问前 100 MB，每页一个字节
        sum += p[i];
    }
    rss("访问 100 MB 之后");
    printf("sum = %ld\n", sum);
    return 0;
}
```

```bash
truncate -s 1G sparse.img       # 上一篇的稀疏文件
./lazy
```

```
映射之前             VmRSS:	    1140 kB
映射 1 GB 之后       VmRSS:	    1272 kB
访问 100 MB 之后     VmRSS:	  103672 kB
sum = 0
```

映射 1 GB 之后，占用的内存几乎没有变；访问了前 100 MB 之后，才增加了约 100 MB。映射只是建立了「地址 → 文件位置」的对应关系，真正的页在访问时才加载。所以可以放心地映射一个很大的文件，只访问需要的部分。

程序的启动也利用了这一点：《GCC 与 ELF 文件》中看到，可执行文件和 `libc.so.6` 都出现在 `/proc/PID/maps` 中，它们就是被动态链接器 mmap 进来的。`libc.so.6` 有 1.7 MB，一个程序通常只用到其中很少的函数，没用到的部分不会被加载。而且所有进程映射的是页缓存中**同一份** libc，这就是《静态库与动态库》中说动态库的代码在内存中只有一份的原因。

`mmap` 还可以不对应任何文件（`MAP_ANONYMOUS`），用来分配内存。用 strace 看 `malloc` 分配 1 MB 时做了什么：

```
brk(NULL)                               = 0xbeb2fce0e000
brk(0xbeb2fce2f000)                     = 0xbeb2fce2f000
mmap(NULL, 1052672, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0xe7b03c33f000
munmap(0xe7b03c33f000, 1052672)         = 0
```

小块的内存（`malloc(100)`）用 `brk` 扩大堆来分配；1 MB 的大块直接用匿名 `mmap` 向内核申请（多出的 4096 字节用于 malloc 自己的管理信息），`free` 时用 `munmap` 立即还给内核。

## 4. MAP_SHARED 和 MAP_PRIVATE

用 mmap 修改文件：把文件中的小写字母改成大写。

```c
// upper.c：用 mmap 把文件中的小写字母改成大写（直接修改文件）
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "用法：%s shared|private 文件\n", argv[0]);
        return 2;
    }
    int flag = strcmp(argv[1], "shared") == 0 ? MAP_SHARED : MAP_PRIVATE;
    int fd = open(argv[2], O_RDWR);
    if (fd == -1) {
        perror(argv[2]);
        return 1;
    }
    struct stat st;
    fstat(fd, &st);
    char *p = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, flag, fd, 0);
    if (p == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    close(fd);

    for (off_t i = 0; i < st.st_size; i++) {
        p[i] = toupper((unsigned char)p[i]);     // 修改内存，就是修改文件
    }
    printf("映射中的内容：%.*s", (int)st.st_size, p);
    if (flag == MAP_SHARED) {
        msync(p, st.st_size, MS_SYNC);           // 等待修改写回磁盘
    }
    munmap(p, st.st_size);
    return 0;
}
```

```bash
printf "hello mmap\n" > t.txt
./upper private t.txt; cat t.txt
./upper shared t.txt; cat t.txt
```

```
映射中的内容：HELLO MMAP
文件的内容：hello mmap
映射中的内容：HELLO MMAP
文件的内容：HELLO MMAP
```

（「文件的内容：」是用 `echo -n` 打印的提示。）两次映射中的内容都变成了大写，但只有 `MAP_SHARED` 真正修改了文件：

- **`MAP_SHARED`**：写入直接修改页缓存中的页，内核稍后把它写回磁盘；其他映射了同一个文件的进程，立即就能看到修改。`msync` 等待写回完成，像 `fsync` 一样，用于确保数据真正落盘；
- **`MAP_PRIVATE`**：第一次写某一页时，内核把这一页**复制**一份给这个进程，之后的修改只发生在副本上（**写时复制**，copy-on-write），文件和其他进程都看不到。动态库的数据段就是这样映射的：每个进程可以修改自己的全局变量，互不影响。

`MAP_SHARED` 也可以用作进程间通信：两个进程映射同一个文件，一个写，另一个就能读到（第 4.3 篇）。

## 5. 陷阱：Bus error

映射之后，如果文件被截短了，访问超出文件末尾的那些页会怎样？

```c
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void)
{
    int fd = open("small.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1 || ftruncate(fd, 8192) == -1) {      // 文件长 8 KB
        perror("small.txt");
        return 1;
    }
    char *p = mmap(NULL, 8192, PROT_READ, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    printf("p[5000] = %d\n", p[5000]);
    fflush(stdout);
    if (ftruncate(fd, 100) == -1) {                    // 文件被截短到 100 字节
        perror("ftruncate");
        return 1;
    }
    printf("p[50] = %d\n", p[50]);
    fflush(stdout);
    printf("p[5000] = %d\n", p[5000]);                 // 这一页已经不对应文件中的任何内容
    return 0;
}
```

```bash
./bus; echo "exit=$?"
```

```
p[5000] = 0
p[50] = 0
Bus error (core dumped)
exit=135
```

文件截短到 100 字节后，`p[50]` 仍在文件范围内，可以访问；`p[5000]` 所在的第二页已经不对应文件中的任何内容，访问它时进程收到 **SIGBUS** 信号（总线错误，7 号信号），退出码 135 = 128 + 7。

用 `read` 读文件时，文件被别人截短，`read` 只会返回 0 或较少的字节数，程序可以正常处理；用 mmap 时却会直接崩溃。所以如果文件可能被其他进程修改长度（比如正在增长的日志文件），用 mmap 要格外小心。

## 6. 什么时候用 mmap

| 适合 mmap | 适合 read |
|---|---|
| 随机访问文件的不同位置（数据库、索引文件） | 从头到尾顺序读一遍 |
| 文件很大，只访问其中一部分 | 文件可能被截短或者正在增长 |
| 多个进程共享同一份数据（动态库、共享内存） | 读管道、终端、网络（它们不能 mmap） |
| 想把文件当作数组或结构体来访问 | 文件很小（建立映射本身有开销） |

顺序读一遍文件时，两者的速度差别不大（上面测得 0.08 秒对 0.09 秒），`read` 的代码更简单，也不会遇到 Bus error。mmap 的真正优势在于随机访问和共享。

## 7. 常见错误

1. **不检查 `MAP_FAILED`**：`mmap` 失败时返回的是 `MAP_FAILED`（即 `(void *)-1`，实测打印出来是 `0xffffffffffffffff`），不是 `NULL`。
2. **映射长度为 0 的文件**：`mmap` 返回 `EINVAL`。先用 `fstat` 检查大小。
3. **`prot` 和 `open` 的方式不一致**：用 `O_RDONLY` 打开，却要 `PROT_WRITE` 加 `MAP_SHARED`，`mmap` 返回 `EACCES`。
4. **用 mmap 扩展文件**：映射不能让文件变长，写超出文件末尾的部分会 SIGBUS。要先用 `ftruncate` 把文件扩展到需要的大小。
5. **以为 `MAP_PRIVATE` 的修改会保存**：写时复制，文件不会改变。
6. **`offset` 不是页大小的整数倍**：`mmap` 返回 `EINVAL`。
7. **用 `free` 的 free 一列判断内存不足**：页缓存会让 free 变小，看 available。

## 8. 小结

- 内核用页缓存保存读过的文件内容，`buff/cache` 就是它；`read` = 页缓存 → 复制到用户缓冲区
- `mmap` 把文件映射到地址空间，访问内存就是访问文件，省去了复制；`munmap` 解除映射
- 映射按需加载：第一次访问某页时触发缺页，内核才把它映射进来；映射 1 GB 只访问 100 MB，只占 100 MB 内存
- `MAP_SHARED` 的修改写回文件、对其他进程可见；`MAP_PRIVATE` 写时复制，只改自己的副本
- 动态库通过 mmap 加载，所有进程共享同一份；大块的 `malloc` 用匿名 mmap
- 访问已被截短的文件区域会收到 SIGBUS
- 顺序读时 mmap 和 read 差别不大；随机访问、大文件、共享数据时 mmap 更有优势

## 9. 练习

1. 用 mmap 实现 `tail -n 1`：输出文件的最后一行（提示：从映射的末尾向前找 `\n`）。
2. 用 mmap 复制文件：源文件用 `PROT_READ` 映射，目标文件先 `ftruncate` 到相同大小，再用 `MAP_SHARED` 映射，最后 `memcpy`。
3. 一个进程用 `MAP_SHARED` 映射了文件并修改了内容，但还没有调用 `msync`，这时另一个进程用 `read` 读这个文件，能读到修改后的内容吗？

<details>
<summary>点击查看答案</summary>

1. ```c
   #include <fcntl.h>
   #include <stdio.h>
   #include <sys/mman.h>
   #include <sys/stat.h>
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
       struct stat st;
       fstat(fd, &st);
       if (st.st_size == 0) return 0;
       char *p = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
       if (p == MAP_FAILED) {
           perror("mmap");
           return 1;
       }
       off_t end = st.st_size;
       if (p[end - 1] == '\n') end--;               // 忽略末尾的换行
       off_t start = end;
       while (start > 0 && p[start - 1] != '\n') start--;
       fwrite(p + start, 1, end - start, stdout);
       putchar('\n');
       munmap(p, st.st_size);
       close(fd);
       return 0;
   }
   ```

   ```bash
   ./mtail /etc/passwd
   tail -n 1 /etc/passwd
   ```

   ```
   lucky:x:1001:1001::/home/lucky:/bin/bash
   lucky:x:1001:1001::/home/lucky:/bin/bash
   ```

   不论文件多大，它只访问了最后一行所在的一两页。用 `read` 实现同样的功能，就要用 `lseek` 跳到文件末尾附近再读，mmap 写起来更自然。

2. ```c
   #include <fcntl.h>
   #include <stdio.h>
   #include <string.h>
   #include <sys/mman.h>
   #include <sys/stat.h>
   #include <unistd.h>

   int main(int argc, char *argv[])
   {
       if (argc != 3) {
           fprintf(stderr, "用法：%s 源文件 目标文件\n", argv[0]);
           return 2;
       }
       int in = open(argv[1], O_RDONLY);
       int out = open(argv[2], O_RDWR | O_CREAT | O_TRUNC, 0644);   // 映射要求可读，所以用 O_RDWR
       if (in == -1 || out == -1) {
           perror("open");
           return 1;
       }
       struct stat st;
       fstat(in, &st);
       if (st.st_size == 0) return 0;
       if (ftruncate(out, st.st_size) == -1) {      // 先把目标文件扩展到相同的大小
           perror("ftruncate");
           return 1;
       }
       char *src = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, in, 0);
       char *dst = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, out, 0);
       if (src == MAP_FAILED || dst == MAP_FAILED) {
           perror("mmap");
           return 1;
       }
       memcpy(dst, src, st.st_size);
       munmap(src, st.st_size);
       munmap(dst, st.st_size);
       close(in);
       close(out);
       return 0;
   }
   ```

   ```bash
   ./mcp big.log copy.log && cmp big.log copy.log && echo same
   ```

   ```
   same
   ```

   复制 320 MB 用了 0.10 秒，`cp` 用了 0.11 秒，相差不大。注意目标文件要用 `O_RDWR` 打开：`MAP_SHARED` 加 `PROT_WRITE` 的映射要求文件以可读可写的方式打开，只用 `O_WRONLY` 会得到 `EACCES`。

3. 能。实测：

   ```c
   #include <fcntl.h>
   #include <stdio.h>
   #include <string.h>
   #include <sys/mman.h>
   #include <unistd.h>

   int main(void)
   {
       int fd = open("t2.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
       if (fd == -1 || write(fd, "old data\n", 9) != 9) return 1;
       char *p = mmap(NULL, 9, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
       if (p == MAP_FAILED) return 1;
       memcpy(p, "NEW", 3);                         // 修改映射，不调用 msync

       char buf[16] = {0};
       int fd2 = open("t2.txt", O_RDONLY);          // 用另一个描述符 read
       if (read(fd2, buf, 9) != 9) return 1;
       printf("read 读到：%s", buf);
       return 0;
   }
   ```

   ```
   read 读到：NEW data
   ```

   `MAP_SHARED` 的映射和 `read` 用的是页缓存中**同一页**，修改映射就是修改页缓存，`read` 立即就能读到，不需要等待写回磁盘。`msync` 解决的是另一个问题：确保数据已经写到磁盘上，断电也不会丢失。

</details>
