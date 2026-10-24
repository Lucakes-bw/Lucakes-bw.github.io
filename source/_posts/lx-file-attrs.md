---
title: 文件属性与目录
date: 2026-10-24 10:35:00
updated: 2026-10-24 10:35:00
categories:
  - Linux基础
  - 文件IO
tags:
  - Linux
  - 文件IO
  - stat
order: 2
description: ls -l 显示的每一列信息从哪里来？本文用 stat 读取文件的属性（类型、权限、链接数、大小、时间），用 opendir、readdir 读取目录，自己实现一个 ls -l，在 /usr/bin 和 /etc 上的输出与真正的 ls -l 逐字节相同；用 strace 发现它比 ls 多了 1 万多次系统调用，原来是每查一次用户名就打开一次 /etc/passwd。还会看到一个 1 GB 却不占磁盘空间的稀疏文件。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用 `stat`、`lstat` 获取文件的属性，并解析其中的文件类型和权限；理解文件大小和占用空间的区别；分清修改时间和变更时间；会用 `opendir`、`readdir` 读取目录；能自己实现 `ls -l`，并用 strace 分析它的性能问题。
>
> **前置知识**：《文件描述符与系统调用》；《文件与目录操作》中的 inode、硬链接和软链接；《用户、权限与软件包》中的权限位。
>
> **实验环境**：Ubuntu 24.04 容器（aarch64），GCC 13.3。

## 1. stat：文件的属性

`stat` 命令显示一个文件的全部属性：

```bash
printf "hello\n" > a.txt
ln a.txt hard.txt          # 硬链接
stat a.txt
```

```
  File: a.txt
  Size: 6         	Blocks: 8          IO Block: 4096   regular file
Device: 0,44	Inode: 1070577     Links: 2
Access: (0644/-rw-r--r--)  Uid: ( 1001/   lucky)   Gid: ( 1001/   lucky)
Access: 2026-10-24 02:31:23.595459448 +0000
Modify: 2026-10-24 02:31:23.595459448 +0000
Change: 2026-10-24 02:31:23.596459457 +0000
 Birth: 2026-10-24 02:31:23.595459448 +0000
```

这些信息都保存在文件的 **inode** 中（文件名不在其中，它在目录里）。程序中用系统调用 `stat` 获取，结果放在 `struct stat` 中：

```c
#include <sys/stat.h>

int stat(const char *path, struct stat *st);     // 跟随符号链接
int lstat(const char *path, struct stat *st);    // 不跟随：获取符号链接本身的属性
int fstat(int fd, struct stat *st);              // 用已打开的文件描述符
```

| 字段 | 含义 | `stat` 命令中 |
|---|---|---|
| `st_mode` | 文件类型和权限 | `regular file`、`0644/-rw-r--r--` |
| `st_ino` | inode 编号 | `Inode: 1070577` |
| `st_nlink` | 硬链接数 | `Links: 2` |
| `st_uid`、`st_gid` | 所有者、所属组的编号 | `Uid: ( 1001/ lucky)` |
| `st_size` | 大小（字节） | `Size: 6` |
| `st_blocks` | 占用的磁盘块数，**以 512 字节为单位** | `Blocks: 8` |
| `st_atime` | 最后访问时间 | `Access` |
| `st_mtime` | 最后**修改**内容的时间 | `Modify` |
| `st_ctime` | 最后**变更** inode 的时间 | `Change` |

注意 `uid` 和 `gid` 只是数字，用户名 `lucky` 要再去 `/etc/passwd` 中查（第 4 节会看到这一点的影响）。

### 1.1 文件类型

`st_mode` 的高位是文件类型，用宏来判断：

| 宏 | 类型 | `ls -l` 的第一个字符 |
|---|---|---|
| `S_ISREG(m)` | 普通文件 | `-` |
| `S_ISDIR(m)` | 目录 | `d` |
| `S_ISLNK(m)` | 符号链接 | `l` |
| `S_ISCHR(m)` | 字符设备（如终端、`/dev/null`） | `c` |
| `S_ISBLK(m)` | 块设备（如磁盘） | `b` |
| `S_ISFIFO(m)` | 命名管道（第 4.3 篇） | `p` |
| `S_ISSOCK(m)` | 套接字 | `s` |

```bash
ln -s a.txt link.txt; mkdir sub
stat -c "%n %F %s 字节 %b 块" a.txt link.txt sub /dev/null
```

```
a.txt regular file 6 字节 8 块
link.txt symbolic link 5 字节 0 块
sub directory 4096 字节 8 块
/dev/null character special file 0 字节 0 块
```

符号链接 `link.txt` 的大小是 5，就是它的内容 `a.txt` 的长度。对它调用 `stat` 会得到 `a.txt` 的属性，要获取链接本身的属性，必须用 `lstat`。

`st_mode` 的低 12 位是权限：9 个 `rwx` 位，加上 setuid（`S_ISUID`）、setgid（`S_ISGID`）、sticky（`S_ISVTX`）三位，用 `st_mode & S_IRUSR` 这样的写法检查。

### 1.2 大小和占用的空间

`a.txt` 只有 6 个字节，却占用了 8 块，也就是 8 × 512 = 4096 字节：文件系统按块分配空间，这里一块是 4 KB，再小的文件也至少占一块。反过来，文件也可以占用比它的大小**少得多**的空间：

```bash
truncate -s 1G sparse.img
ls -l sparse.img
du -h sparse.img
stat -c "%s %b" sparse.img
```

```
-rw-r--r-- 1 lucky lucky 1073741824 Oct 24 02:31 sparse.img
0	sparse.img
1073741824 0
```

大小是 1 GB，占用的空间却是 **0**。这是一个**稀疏文件**（sparse file）：从来没有写过的部分不分配磁盘块，读取时得到的是 0。虚拟机的磁盘镜像常常是稀疏文件，所以一个「100 GB」的磁盘镜像实际可能只占几 GB。`ls -l` 显示的是大小，`du` 显示的是占用的空间。

### 1.3 修改时间和变更时间

```bash
printf "v1\n" > t.txt
sleep 2; chmod 600 t.txt        # 只改权限
sleep 2; echo v2 >> t.txt       # 修改内容
```

```
修改 2026-10-24 02:32:33.719077799 +0000
变更 2026-10-24 02:32:33.719077799 +0000
--- chmod 之后
修改 2026-10-24 02:32:33.719077799 +0000
变更 2026-10-24 02:32:35.729093702 +0000
--- 写入之后
修改 2026-10-24 02:32:37.739109509 +0000
变更 2026-10-24 02:32:37.739109509 +0000
```

- `chmod` 只改变了 inode 中的权限，**变更时间**（ctime）更新了，**修改时间**（mtime）不变；
- 写入内容时两者都更新：内容变了，inode 中的大小也变了。

`ls -l` 显示的是修改时间。`make` 就是通过比较源文件和目标文件的修改时间，决定哪些文件需要重新编译的。注意 ctime 不是「创建时间」（creation time）；创建时间是上面 `stat` 输出中的 `Birth`，要用较新的系统调用 `statx` 才能获取。

## 2. 读取目录

目录也是一种文件，它的内容是一张「名字 → inode 编号」的表。用 `opendir`、`readdir`、`closedir` 读取：

```c
#include <dirent.h>

DIR *d = opendir("demo");
struct dirent *de;
while ((de = readdir(d)) != NULL) {
    printf("%lu %s\n", (unsigned long)de->d_ino, de->d_name);
}
closedir(d);
```

`struct dirent` 中只有很少的信息：名字 `d_name`、inode 编号 `d_ino`，以及部分文件系统提供的类型 `d_type`。大小、权限等要再对每个文件调用 `stat`。还要注意：

- 结果中包括 `.`（目录自己）和 `..`（上一级目录）；
- **`readdir` 返回的顺序不确定**，不是按名字排序的。`ls -U` 按目录中的原始顺序显示：

```bash
ls -U demo
ls demo
```

```
tool sparse.img sub shared hard.txt link.txt a.txt 
a.txt hard.txt link.txt shared sparse.img sub tool 
```

`ls` 的有序输出是它自己排序的结果。

## 3. 自己实现 ls -l

综合上面的内容，实现一个 `ls -l`。分三步：读出所有的目录项并获取属性；计算每一列的宽度，让输出对齐；逐行输出。

```c
// myls.c：简易的 ls -l
#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct entry {
    char name[256];
    struct stat st;
};

static int by_name(const void *a, const void *b)
{
    return strcmp(((const struct entry *)a)->name, ((const struct entry *)b)->name);
}

// 把 st_mode 转换成 drwxr-xr-x 这样的字符串
static void mode_string(mode_t m, char s[11])
{
    s[0] = S_ISDIR(m) ? 'd' : S_ISLNK(m) ? 'l' : S_ISCHR(m) ? 'c' : S_ISBLK(m) ? 'b'
         : S_ISFIFO(m) ? 'p' : S_ISSOCK(m) ? 's' : '-';
    const char *rwx = "rwxrwxrwx";
    for (int i = 0; i < 9; i++) {
        s[i + 1] = (m & (0400 >> i)) ? rwx[i] : '-';
    }
    if (m & S_ISUID) s[3] = (m & S_IXUSR) ? 's' : 'S';
    if (m & S_ISGID) s[6] = (m & S_IXGRP) ? 's' : 'S';
    if (m & S_ISVTX) s[9] = (m & S_IXOTH) ? 't' : 'T';
    s[10] = '\0';
}

static const char *user_name(uid_t uid)
{
    static char buf[16];
    struct passwd *pw = getpwuid(uid);
    if (pw) return pw->pw_name;
    snprintf(buf, sizeof buf, "%u", (unsigned)uid);
    return buf;
}

static const char *group_name(gid_t gid)
{
    static char buf[16];
    struct group *gr = getgrgid(gid);
    if (gr) return gr->gr_name;
    snprintf(buf, sizeof buf, "%u", (unsigned)gid);
    return buf;
}

int main(int argc, char *argv[])
{
    const char *dir = argc > 1 ? argv[1] : ".";
    DIR *d = opendir(dir);
    if (d == NULL) {
        perror(dir);
        return 1;
    }

    // 第一步：读出所有的目录项，并获取它们的属性
    size_t n = 0, cap = 64;
    struct entry *list = malloc(cap * sizeof *list);
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;           // 和 ls 一样，不显示隐藏文件
        if (n == cap) {
            cap *= 2;
            list = realloc(list, cap * sizeof *list);
        }
        snprintf(list[n].name, sizeof list[n].name, "%s", de->d_name);
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        if (lstat(path, &list[n].st) == -1) {          // lstat：不跟随符号链接
            perror(path);
            continue;
        }
        n++;
    }
    closedir(d);
    qsort(list, n, sizeof *list, by_name);

    // 第二步：计算每一列的宽度和总块数
    long long blocks = 0;
    int wl = 0, wu = 0, wg = 0, ws = 0;
    for (size_t i = 0; i < n; i++) {
        struct stat *st = &list[i].st;
        blocks += st->st_blocks;
        int len;
        len = snprintf(NULL, 0, "%lu", (unsigned long)st->st_nlink);   if (len > wl) wl = len;
        len = strlen(user_name(st->st_uid));                           if (len > wu) wu = len;
        len = strlen(group_name(st->st_gid));                          if (len > wg) wg = len;
        len = snprintf(NULL, 0, "%lld", (long long)st->st_size);       if (len > ws) ws = len;
    }
    printf("total %lld\n", blocks / 2);                // st_blocks 以 512 字节为单位，ls 以 1 KB 为单位

    // 第三步：逐行输出
    time_t now = time(NULL);
    for (size_t i = 0; i < n; i++) {
        struct stat *st = &list[i].st;
        char mode[11], when[32];
        mode_string(st->st_mode, mode);
        // 半年之内的文件显示时间，更早的显示年份
        const char *fmt = (now - st->st_mtime < 182 * 24 * 3600) ? "%b %e %H:%M" : "%b %e  %Y";
        strftime(when, sizeof when, fmt, localtime(&st->st_mtime));
        printf("%s %*lu %-*s %-*s %*lld %s %s", mode, wl, (unsigned long)st->st_nlink,
               wu, user_name(st->st_uid), wg, group_name(st->st_gid),
               ws, (long long)st->st_size, when, list[i].name);
        if (S_ISLNK(st->st_mode)) {
            char target[4096], path[4096];
            snprintf(path, sizeof path, "%s/%s", dir, list[i].name);
            ssize_t len = readlink(path, target, sizeof target - 1);
            if (len != -1) {
                target[len] = '\0';
                printf(" -> %s", target);
            }
        }
        printf("\n");
    }
    free(list);
    return 0;
}
```

要点：

- 用 **`lstat`** 而不是 `stat`，这样符号链接显示为 `l`，再用 `readlink` 读出它指向的路径；
- `mode_string` 把 `st_mode` 转换成 10 个字符。setuid 位显示在所有者的 `x` 位置上：有执行权限显示 `s`，没有显示 `S`；sticky 位显示在其他人的 `x` 位置上（`t` 或 `T`）；
- `getpwuid`、`getgrgid` 把编号转换成名字，它们从 `/etc/passwd`、`/etc/group` 中查找；
- `total` 一行是所有文件占用的块数之和，`ls` 以 1 KB 为单位，`st_blocks` 以 512 字节为单位，所以要除以 2；
- 时间的格式：半年之内的文件显示「月 日 时:分」，更早的显示「月 日 年」，这和 `ls` 的规则相同。

在一个包含普通文件、硬链接、符号链接、sticky 目录、可执行文件和稀疏文件的目录中测试，和真正的 `ls -l` 比较：

```bash
gcc -Wall -Wextra -O2 myls.c -o myls
./myls demo > m.txt; ls -l demo > r.txt
diff m.txt r.txt && echo SAME
cat m.txt
```

```
SAME
total 36
-rw------- 2 lucky lucky          6 Oct 24 02:31 a.txt
-rw------- 2 lucky lucky          6 Oct 24 02:31 hard.txt
lrwxrwxrwx 1 lucky lucky          5 Oct 24 02:31 link.txt -> a.txt
drwxrwxrwt 2 lucky lucky       4096 Oct 24 02:31 shared
-rw-r--r-- 1 lucky lucky 1073741824 Oct 24 02:31 sparse.img
drwxr-xr-x 2 lucky lucky       4096 Oct 24 02:31 sub
-rwxr-xr-x 1 lucky lucky      71368 Oct 24 02:31 tool
```

完全相同。`a.txt` 和 `hard.txt` 的链接数是 2；`shared` 设置了 sticky 位，显示为 `t`；1 GB 的 `sparse.img` 对 `total` 没有任何贡献。在系统目录上测试：`/usr/bin`（536 个文件）、`/etc`、`/tmp` 的输出都和 `ls -l` **逐字节相同**。

`/dev` 不同：

```
crw-rw-rw- 1 root root 1, 3 Oct 24 01:49 /dev/null     ← ls -l
crw-rw-rw- 1 root root  0 Oct 24 01:49 null           ← myls
```

对于设备文件，`ls` 显示的不是大小，而是**主设备号和次设备号**（`1, 3`），内核根据它们找到对应的驱动程序。这留作练习。

（这个实现假设没有设置语言环境：容器中的 `LANG` 为空，`ls` 按字节排序，月份也显示为英文。在中文环境中，`ls` 的排序规则和日期格式都会不同。）

## 4. 用 strace 找性能问题

输出正确了，再看看效率。用 strace 统计两者在 `/usr/bin` 上的系统调用：

```bash
strace -c ./myls /usr/bin > /dev/null
```

```
 26.92    0.012001           3      3216           newfstatat
 17.51    0.007805           3      2149           openat
  0.01    0.000003           1         2           getdents64
100.00    0.044574           3     14170        26 total
```

`ls -l` 只用了 1742 次系统调用（上一篇的练习 3），`myls` 却用了 14170 次，其中 `openat` 有 2149 次。目录中只有 536 个文件，`myls` 打开了哪些文件？

```bash
strace -e trace=openat ./myls /usr/bin 2>&1 >/dev/null | awk -F'"' '{print $2}' | sort | uniq -c | sort -rn | head -3
```

```
   1072 /etc/passwd
   1072 /etc/group
      1 /usr/bin
```

`/etc/passwd` 和 `/etc/group` 各被打开了 1072 次。536 个文件，计算列宽时查一次用户名，输出时又查一次，每次 `getpwuid` 都要打开 `/etc/passwd`、读取、关闭，正好 536 × 2 = 1072 次。而 `/usr/bin` 中所有文件的所有者都是 root，结果每次都一样。

**把上一次的查询结果记下来**：编号和上次相同，就直接返回上次的名字。

```c
static const char *user_name(uid_t uid)
{
    static uid_t last = (uid_t)-1;
    static char buf[64];
    if (uid != last) {                       // 和上一次查的不同，才去读 /etc/passwd
        struct passwd *pw = getpwuid(uid);
        if (pw) snprintf(buf, sizeof buf, "%s", pw->pw_name);
        else snprintf(buf, sizeof buf, "%u", (unsigned)uid);
        last = uid;
    }
    return buf;
}
```

`group_name` 也做同样的修改后，输出仍然和 `ls -l` 相同，系统调用的变化：

| 版本 | 系统调用总数 | 其中 `openat` | 运行时间 |
|---|---|---|---|
| 原来的 `myls` | 14170 | 2149 | 0.005 秒 |
| 缓存用户名和组名 | 1330 | 19 | 0.001 秒 |
| 真正的 `ls -l` | 1742 | 12 | 0.001 秒 |

系统调用减少到原来的十分之一以下，比 `ls -l` 还少（`ls` 多出的主要是对每个文件查询 SELinux 安全上下文的 `lgetxattr`）。注意 `getpwuid` 返回的指针指向一块静态内存，下次调用时会被覆盖，所以要把名字**复制**到自己的 `buf` 中，而不是只保存指针。

这个例子说明：**一个看起来普通的库函数，背后可能有很多次系统调用**。程序慢的时候，`strace -c` 是很好的第一步。

## 5. 常见错误

1. **用 `stat` 处理符号链接**：得到的是链接指向的文件的属性，指向的文件不存在时还会失败。需要链接本身的信息时用 `lstat`。
2. **遍历目录时只用 `d_name` 调用 `stat`**：`d_name` 只是名字，不是路径，要和目录名拼接起来。
3. **递归遍历时没有跳过 `.` 和 `..`**：无限递归。
4. **以为 `readdir` 的结果是排好序的**。
5. **把 ctime 当作创建时间**：它是 inode 最后一次变更的时间。
6. **用 `ls -l` 的大小估计磁盘占用**：稀疏文件可能很大却不占空间，小文件至少占一块。用 `du`。
7. **在循环中保存 `getpwuid` 返回的指针**：下一次调用会覆盖它的内容。

## 6. 小结

- `stat`、`lstat`、`fstat` 获取 inode 中的属性：类型和权限（`st_mode`）、链接数、所有者、大小、占用的块数、三个时间
- `S_ISDIR` 等宏判断文件类型；`lstat` 不跟随符号链接，`readlink` 读取链接的内容
- 大小和占用空间不同：稀疏文件 1 GB 占 0 块，6 字节的文件占 8 块（4 KB）
- mtime 是内容的修改时间，ctime 是 inode 的变更时间（`chmod` 只改变 ctime）
- `opendir`、`readdir`、`closedir` 读取目录，结果包括 `.` 和 `..`，顺序不确定
- 自己实现的 `ls -l` 和真正的输出逐字节相同；用 `strace -c` 发现并修复了重复打开 `/etc/passwd` 的问题，系统调用从 14170 次减少到 1330 次

## 7. 练习

1. 修改 `myls`，让设备文件和 `ls -l` 一样显示主设备号和次设备号（提示：`st_rdev`，以及 `<sys/sysmacros.h>` 中的 `major()`、`minor()`）。
2. 写一个程序，递归地统计一个目录树中普通文件的个数和总大小，和 `find` 的结果对比。
3. 为什么 `/usr/bin` 中 536 个文件的 `total` 不等于它们的大小之和除以 1024？

<details>
<summary>点击查看答案</summary>

1. 计算宽度和输出大小时，对设备文件改为输出 `major, minor`：

   ```c
   #include <sys/sysmacros.h>

   // 输出大小的地方
   if (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode)) {
       printf("%u, %u", major(st->st_rdev), minor(st->st_rdev));
   } else {
       printf("%lld", (long long)st->st_size);
   }
   ```

   实测几个设备文件，和 `ls -l /dev` 的对应行相同：

   ```
   crw-rw-rw- 1 root root 1, 7 Oct 24 01:49 full
   crw-rw-rw- 1 root root 1, 3 Oct 24 01:49 null
   crw-rw-rw- 1 root root 5, 0 Oct 24 01:49 tty
   crw-rw-rw- 1 root root 1, 5 Oct 24 01:49 zero
   ```

   `/dev/null`、`/dev/zero`、`/dev/full` 的主设备号都是 1，由内核中同一个「内存设备」驱动实现，次设备号区分具体是哪一个。这里的设备号恰好都是一位数；要在任何目录下都和 `ls -l` 完全对齐，还需要分别计算主、次设备号的宽度。

2. ```c
   #include <dirent.h>
   #include <stdio.h>
   #include <string.h>
   #include <sys/stat.h>

   static long files = 0;
   static long long bytes = 0;

   static void walk(const char *dir)
   {
       DIR *d = opendir(dir);
       if (d == NULL) {
           perror(dir);
           return;
       }
       struct dirent *de;
       while ((de = readdir(d)) != NULL) {
           if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
               continue;                      // 跳过 . 和 ..，否则会无限递归
           }
           char path[4096];
           snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
           struct stat st;
           if (lstat(path, &st) == -1) {
               perror(path);
               continue;
           }
           if (S_ISDIR(st.st_mode)) {
               walk(path);
           } else if (S_ISREG(st.st_mode)) {
               files++;
               bytes += st.st_size;
           }
       }
       closedir(d);
   }

   int main(int argc, char *argv[])
   {
       walk(argc > 1 ? argv[1] : ".");
       printf("%ld 个文件，共 %lld 字节\n", files, bytes);
       return 0;
   }
   ```

   ```bash
   ./tree_count /usr/include
   find /usr/include -type f | wc -l
   find /usr/include -type f -printf "%s\n" | awk '{s += $1} END {print s}'
   ```

   ```
   2400 个文件，共 23678839 字节
   2400
   23678839
   ```

   结果完全相同。用 `lstat` 而不是 `stat` 很重要：目录中如果有指向上级目录的符号链接，用 `stat` 会把它当作目录进入，造成无限递归。glibc 也提供了现成的遍历函数 `nftw`。

3. `total` 是**占用的块数**之和，不是大小之和。每个文件按 4 KB 的块分配空间，一个 100 字节的文件也占 4 KB；而稀疏文件又可能占得比大小少。符号链接如果很短，内容直接保存在 inode 中，占 0 块。

</details>
