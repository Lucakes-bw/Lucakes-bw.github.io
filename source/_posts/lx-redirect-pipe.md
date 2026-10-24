---
title: 重定向与管道的原理
date: 2026-10-24 10:40:00
updated: 2026-10-24 10:40:00
categories:
  - Linux基础
  - 文件IO
tags:
  - Linux
  - 文件IO
  - 管道
order: 3
description: Shell 中的 > 和 | 是怎样实现的？答案是 dup2 和 pipe 两个系统调用。本文用 dup2 实现「命令 > 文件」，用 pipe 实现「命令1 | 命令2」，配有描述符变化的逐步演示；用 strace 看 bash 自己是怎样做的；解释 2>&1 的位置为什么重要；实测管道的容量是 64 KB；最后复现一个经典的 bug：少写两行 close，程序就永远卡住。
top_group_list: true
disableNunjucks: true
viz: [lx-fd]
---

> **本文目标**：会用 `dup`、`dup2` 复制文件描述符；理解 Shell 的重定向是怎样实现的，以及 `2>&1` 的含义；会用 `pipe` 创建管道，自己实现 `命令1 | 命令2`；理解管道的读端什么时候得到文件结束，以及为什么要关闭不用的描述符；了解 SIGPIPE。
>
> **前置知识**：《文件描述符与系统调用》中的三层结构和「最小空闲描述符」规则；《文本处理与管道》中重定向和管道的用法。本文会用到 `fork` 和 `exec`，它们在第 4.1 篇详细讲，这里只需要知道：`fork` 复制出一个子进程，`exec` 把当前进程换成另一个程序。
>
> **实验环境**：Ubuntu 24.04 容器（aarch64），GCC 13.3，bash 5.2。

## 1. dup 和 dup2

```c
#include <unistd.h>

int dup(int oldfd);               // 复制 oldfd，返回最小的空闲描述符
int dup2(int oldfd, int newfd);   // 让 newfd 指向 oldfd 所指的那一项；newfd 原来打开着就先关闭
```

**复制**的意思是：新的描述符和原来的描述符指向打开文件表中的**同一项**，共享偏移，这一项的引用计数加 1。上一篇演示中 0、1、2 共用一项，就是 Shell 用这种方法做到的。

`dup2` 是重定向的核心：

```c
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    printf("1. 这一行输出到终端\n");
    fflush(stdout);

    int saved = dup(STDOUT_FILENO);              // 先复制一份标准输出，以便恢复
    int fd = open("out.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(fd, STDOUT_FILENO);                     // 让 1 指向 out.txt
    close(fd);

    printf("2. 这一行进入了 out.txt\n");
    fflush(stdout);

    dup2(saved, STDOUT_FILENO);                  // 恢复
    close(saved);
    printf("3. 又回到了终端\n");
    return 0;
}
```

```
1. 这一行输出到终端
3. 又回到了终端
--- out.txt
2. 这一行进入了 out.txt
```

`dup2(fd, STDOUT_FILENO)` 之后，fd 1 指向了 `out.txt`，`printf` 写向 fd 1 的内容就进入了文件，而 `printf` 本身完全不知道。注意每次切换前都调用了 `fflush`，否则缓冲区中的内容会在切换之后才写出，跑到错误的地方去（上一篇的第 6 节）。

## 2. 实现「命令 > 文件」

Shell 运行 `ls > out.txt` 时，不能在自己的进程中改 fd 1，否则之后 Shell 自己的输出也进了文件。它的做法是：`fork` 出一个子进程，**在子进程中**打开文件、`dup2`，然后 `exec` 运行命令。

```c
// redirect.c：实现 命令 > 文件
#include <fcntl.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "用法：%s 输出文件 命令 [参数...]\n", argv[0]);
        return 2;
    }
    pid_t pid = fork();                          // 创建一个子进程
    if (pid == 0) {                              // 子进程
        int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            perror(argv[1]);
            _exit(1);
        }
        dup2(fd, STDOUT_FILENO);                 // 标准输出改为这个文件
        close(fd);
        execvp(argv[2], &argv[2]);               // 运行命令，它继承了改好的描述符
        perror(argv[2]);                         // 只有 exec 失败才会执行到这里
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);                    // 父进程等待子进程结束
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
```

```bash
./redirect list.txt ls -l /etc/hostname /etc/hosts; echo "exit=$?"
cat list.txt
```

```
exit=0
-rw-r--r-- 1 root root   7 Oct 24 01:49 /etc/hostname
-rw-r--r-- 1 root root 166 Oct 24 01:49 /etc/hosts
```

关键在于：**`exec` 换掉了进程的程序，但保留了它的文件描述符表。**`ls` 一启动，它的 fd 1 就已经指向 `list.txt`，它照常向标准输出写，结果就进了文件。所以任何程序都能被重定向，不需要为此写一行代码。

出错的情况：

```bash
./redirect x.txt nosuchcmd; echo "exit=$?"
./redirect x.txt ls /nope; echo "exit=$?"
```

```
nosuchcmd: No such file or directory
exit=127
ls: cannot access '/nope': No such file or directory
exit=2
```

`exec` 失败时，子进程以 127 退出，这和 Shell 中 `command not found` 的退出码相同（《进程与系统监控》）。`ls` 的错误信息仍然出现在终端上，因为只重定向了 fd 1，fd 2（标准错误）还指向终端。

下面的演示逐步展示了描述符的变化。有多个进程时，为了避免箭头交叉，每个描述符的格子中直接写出了它指向打开文件表中的哪一项：

<div class="dsviz" data-viz="redir-file"></div>

### 2.1 2>&1 的顺序

Shell 中的 `2>&1` 就是 `dup2(1, 2)`：让 fd 2 指向 fd 1 **此刻**所指的地方。重定向**从左到右**依次执行，所以顺序很重要：

```bash
ls /etc/hostname /nope > o1.txt 2>&1
cat o1.txt
```

```
ls: cannot access '/nope': No such file or directory
/etc/hostname
```

```bash
ls /etc/hostname /nope 2>&1 > o2.txt
cat o2.txt
```

```
ls: cannot access '/nope': No such file or directory
/etc/hostname
```

第二条命令中，错误信息出现在了**终端**上，`o2.txt` 中只有 `/etc/hostname`（上面最后两行中，第一行是命令执行时显示在终端上的，第二行是 `cat o2.txt` 的结果）。用 `dup2` 来理解：

| 写法 | 执行的操作 | 结果 |
|---|---|---|
| `> o1.txt 2>&1` | ① fd 1 → `o1.txt`；② `dup2(1, 2)`：fd 2 → fd 1 此刻指向的 `o1.txt` | 两者都进文件 |
| `2>&1 > o2.txt` | ① `dup2(1, 2)`：fd 2 → fd 1 此刻指向的**终端**；② fd 1 → `o2.txt` | 只有标准输出进文件 |

`2>&1` 不是「让 2 永远跟着 1」，而是一次性的复制。

## 3. 管道

```c
#include <unistd.h>

int pipe(int p[2]);     // p[0] 是读端，p[1] 是写端
```

`pipe` 在内核中创建一个缓冲区，返回两个描述符：写进 `p[1]` 的数据，可以从 `p[0]` 读出来，先进先出。

```c
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    int p[2];
    if (pipe(p) == -1) return 1;                 // p[0] 读端，p[1] 写端
    printf("p[0] = %d, p[1] = %d\n", p[0], p[1]);

    if (write(p[1], "hello", 5) != 5) return 1;
    char buf[16] = {0};
    if (read(p[0], buf, sizeof buf - 1) == -1) return 1;
    printf("读到 \"%s\"\n", buf);

    // 没有人读，一直写，直到管道满
    fcntl(p[1], F_SETFL, O_NONBLOCK);            // 满了不阻塞，而是返回 -1
    long total = 0;
    while (write(p[1], "x", 1) == 1) {
        total++;
    }
    printf("写了 %ld 字节后管道满了\n", total);
    return 0;
}
```

```
p[0] = 3, p[1] = 4
读到 "hello"
写了 65536 字节后管道满了
```

管道的缓冲区是 **64 KB**。缓冲区满了，`write` 就会**阻塞**（等待），直到有人读走一些数据；这里把写端设为非阻塞模式（`O_NONBLOCK`），满了就返回 -1，用来测出容量。同样，管道空的时候 `read` 会阻塞，直到有数据写入。

所以 `cat 大文件 | grep xxx` 中，如果 `grep` 处理得慢，`cat` 写满 64 KB 后就会停下来等，不会把整个文件都读进内存。管道自带「流量控制」。

### 3.1 什么时候读到文件结束

管道读端的 `read` 什么时候返回 0（文件结束）？规则是：**缓冲区空了，并且所有的写端都已经关闭。**只要还有任何一个描述符指向写端，内核就认为「以后可能还会有数据」，`read` 就会一直等待。这条规则决定了下一节代码的写法。

## 4. 实现「命令1 | 命令2」

```c
// mypipe.c：实现 命令1 | 命令2，两个命令用 -- 分开
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int sep = 1;
    while (sep < argc && strcmp(argv[sep], "--") != 0) sep++;
    if (sep == 1 || sep >= argc - 1) {
        fprintf(stderr, "用法：%s 命令1 [参数...] -- 命令2 [参数...]\n", argv[0]);
        return 2;
    }
    argv[sep] = NULL;                            // 把 -- 换成 NULL，argv 就分成了两个参数列表
    char **cmd1 = &argv[1], **cmd2 = &argv[sep + 1];

    int p[2];
    if (pipe(p) == -1) {
        perror("pipe");
        return 1;
    }

    pid_t left = fork();
    if (left == 0) {                             // 左边的命令：标准输出接到管道的写端
        dup2(p[1], STDOUT_FILENO);
        close(p[0]);
        close(p[1]);
        execvp(cmd1[0], cmd1);
        perror(cmd1[0]);
        _exit(127);
    }

    pid_t right = fork();
    if (right == 0) {                            // 右边的命令：标准输入接到管道的读端
        dup2(p[0], STDIN_FILENO);
        close(p[0]);
        close(p[1]);
        execvp(cmd2[0], cmd2);
        perror(cmd2[0]);
        _exit(127);
    }

    close(p[0]);                                 // 父进程自己不用管道，两端都要关闭
    close(p[1]);
    int status;
    waitpid(left, NULL, 0);
    waitpid(right, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
```

```bash
./mypipe ls /usr/bin -- wc -l
ls /usr/bin | wc -l
./mypipe sort /home/lucky/logs/access.log -- head -2
```

```
536
536
10.0.0.5 - - [23/Oct/2026:08:00:00 +0800] "GET /index.html HTTP/1.1" 200 20922 71ms
10.0.0.5 - - [23/Oct/2026:08:02:38 +0800] "GET /posts/linked-list/ HTTP/1.1" 200 32881 57ms
```

和 Shell 的管道结果相同。每个子进程用 `dup2` 把管道的一端接到自己的标准输入或标准输出，然后**关闭原来的两个描述符**；父进程也关闭两端。下面的演示逐步展示了三个进程的描述符表，先点「正确版本」：

<div class="dsviz" data-viz="redir-pipe"></div>

### 4.1 少写两行 close 会怎样

把父进程中的两行 `close` 删掉：

```c
    // 忘了关闭管道
    int status;
    waitpid(left, NULL, 0);
```

```bash
timeout 3 ./mypipe_bug ls /usr/bin -- wc -l; echo "exit=$?"
```

```
exit=124
```

什么都没有输出，3 秒后被 `timeout` 终止（退出码 124 表示超时）。程序卡住了。在它卡住时查看：

```bash
./mypipe_bug ls /usr/bin -- wc -l &
ps -o pid,ppid,stat,cmd --ppid $!
```

```
    PID    PPID STAT CMD
   6834    6831 S    wc -l
```

`ls` 早已结束，`wc` 还在，状态是 `S`（睡眠）：它在等待从管道读数据。原因正是 3.1 节的规则：`ls` 退出时关闭了它的写端，但**父进程的 fd 4 仍然指向写端**，所以 `wc` 永远读不到文件结束，永远不会输出结果；父进程又在 `waitpid` 中等 `wc` 结束。两者互相等待，谁也不会动。在上面的演示中点「忘了关闭」，可以看到写端的引用计数始终降不到 0。

同样的道理，子进程中的 `close(p[0]); close(p[1]);` 也不能省：`wc` 自己的 fd 4 如果没关，它就是在等待自己。**使用管道时，每个进程都要关闭自己不用的那一端。**

### 4.2 bash 是怎样做的

用 `strace -f`（同时跟踪子进程）看 bash 执行 `ls /usr/bin | wc -l` 的过程，只保留管道和描述符相关的调用：

```bash
strace -f -e trace=pipe2,dup3,close,execve -o sf.txt bash -c "ls /usr/bin | wc -l"
```

```
6880  pipe2([3, 4], 0)                  = 0
6880  close(4)                          = 0
6881  close(3)                          = 0
6881  dup3(4, 1, 0 <unfinished ...>
6881  close(4)                          = 0
6880  close(3)                          = 0
6882  dup3(3, 0, 0)                     = 0
6882  close(3)                          = 0
6881  execve("/usr/bin/ls", ["ls", "/usr/bin"], 0xc5012b3ab480 /* 6 vars */) = 0
6882  execve("/usr/bin/wc", ["wc", "-l"], 0xc5012b3ab480 /* 6 vars */) = 0
```

第一列是进程号：6880 是 bash，6881 是 `ls`，6882 是 `wc`。和我们的 `mypipe` 几乎一样：创建管道得到 3 和 4；左边的子进程把 4 复制到 1，关闭 3、4；右边的子进程把 3 复制到 0，关闭 3；bash 自己关闭了 4 和 3。（顺序和我们的略有不同：bash 在创建右边的子进程之前就关闭了写端 4，所以右边的子进程根本没有继承到它，也就不需要再关闭。）

`dup3` 是什么？aarch64 上的 Linux 没有 `dup2` 这个系统调用，glibc 的 `dup2` 函数用 `dup3` 实现，效果相同。`pipe2` 和 `pipe` 的关系也是这样。

## 5. SIGPIPE：读端关闭之后再写

反过来，如果读端都关闭了，写端还在写，会怎样？

```bash
yes | head -2
echo "PIPESTATUS: ${PIPESTATUS[*]}"
```

```
y
y
PIPESTATUS: 141 0
```

`yes` 会无限地输出 `y`，`head -2` 读到两行就退出了。`PIPESTATUS` 数组保存了管道中每个命令的退出码：`head` 是 0，`yes` 是 **141 = 128 + 13**，被 13 号信号 **SIGPIPE** 终止了。用 strace 看 `yes` 的最后时刻：

```
write(1, "y\ny\ny\ny\ny\ny\ny\ny\ny\ny\ny\ny\ny\ny\ny\ny\n"..., 8192) = -1 EPIPE (Broken pipe)
--- SIGPIPE {si_signo=SIGPIPE, si_code=SI_USER, si_pid=6900, si_uid=1001} ---
+++ killed by SIGPIPE +++
```

向一个没有读端的管道写数据，`write` 返回 `EPIPE` 错误，同时进程收到 SIGPIPE 信号，默认的处理方式是终止进程。这正是我们想要的：`head` 已经不需要数据了，`yes` 就应该停下来，不必一直空转。

忽略 SIGPIPE 时，进程不会被终止，只会看到 `write` 返回错误：

```bash
(trap "" PIPE; yes | head -1; echo "${PIPESTATUS[*]}")
```

```
y
yes: standard output: Broken pipe
1 0
```

网络服务器通常会忽略 SIGPIPE，否则对方断开连接时，服务器向它写数据就会被直接终止（第 4.2 篇详细讲信号）。

## 6. 常见错误

1. **在父进程中直接 `dup2` 再 `exec`**：Shell 自己就被替换掉了。重定向应该在 `fork` 出的子进程中进行。
2. **使用管道后不关闭不用的一端**：读端永远读不到文件结束，程序卡住。
3. **`dup2` 之后不关闭原来的描述符**：多占了一个描述符，管道的情况下还会导致上一条的问题。
4. **混用 stdio 和 `dup2` 时不 `fflush`**：缓冲区中的内容写到了错误的地方。
5. **写成 `2>&1 > file`，以为错误信息也进了文件**：重定向从左到右执行，`2>&1` 是一次性的复制。
6. **以为管道可以存任意多的数据**：容量是 64 KB，写满后 `write` 阻塞。一个进程如果先向管道写大量数据，再去读另一个管道，可能和对方互相等待。
7. **只看最后一个命令的退出码**：`yes | head` 的 `$?` 是 0。要知道管道中每个命令的结果，用 `PIPESTATUS`，或者 `set -o pipefail`（《Shell 脚本》）。

## 7. 小结

- `dup2(oldfd, newfd)` 让 `newfd` 指向 `oldfd` 所指的打开文件表项，两者共享偏移
- 重定向 = `fork` + 在子进程中 `open`、`dup2`、`close` + `exec`；`exec` 保留描述符表，所以任何程序都能被重定向
- `2>&1` 就是 `dup2(1, 2)`，重定向从左到右执行，顺序决定结果
- `pipe` 返回读端和写端，容量 64 KB，满了写阻塞，空了读阻塞
- 管道 = `pipe` + 两次 `fork` + 各自 `dup2` 到 0 或 1；**所有不用的端都要关闭**，否则读端永远读不到文件结束
- 读端全部关闭后再写，写端收到 SIGPIPE（退出码 141）

## 8. 练习

1. 修改 `redirect.c`，实现 `命令 >> 文件`（追加）。
2. 修改 `redirect.c`，实现 `命令 < 文件`，用 `./redirect_in /etc/hostname wc -c` 测试。
3. `ls /etc/hostname /nope 2>&1 | wc -l` 的输出是多少？`ls /etc/hostname /nope | wc -l` 呢？为什么？

<details>
<summary>点击查看答案</summary>

1. 只需要修改 `open` 的标志：把 `O_TRUNC` 换成 `O_APPEND`。

   ```c
   int fd = open(argv[1], O_WRONLY | O_CREAT | O_APPEND, 0644);
   ```

   ```bash
   ./redirect_append log.txt echo first
   ./redirect_append log.txt echo second
   cat log.txt
   ```

   ```
   first
   second
   ```

   `O_APPEND` 让每次 `write` 之前，偏移都自动移到文件末尾。多个进程同时向一个日志文件追加时，用 `O_APPEND` 打开，各自写入的内容不会互相覆盖。

2. 打开方式改为只读，复制到 fd 0：

   ```c
   int fd = open(argv[1], O_RDONLY);
   ...
   dup2(fd, STDIN_FILENO);
   ```

   ```bash
   ./redirect_in /etc/hostname wc -c
   wc -c < /etc/hostname
   ```

   ```
   7
   7
   ```

   和 Shell 的 `<` 结果相同。`wc` 从标准输入读取，不知道数据来自文件，所以输出中没有文件名。

3. ```bash
   ls /etc/hostname /nope 2>&1 | wc -l
   ls /etc/hostname /nope | wc -l
   ```

   ```
   2
   ls: cannot access '/nope': No such file or directory
   1
   ```

   管道只连接标准输出。第二条命令中，错误信息从 fd 2 直接输出到终端，`wc` 只收到 1 行。第一条命令中，Shell 先把 `ls` 的 fd 1 接到管道，再执行 `2>&1`，fd 2 也指向了管道，`wc` 收到 2 行。（管道的设置先于命令中的重定向，所以这里的 `2>&1` 复制的是管道。）bash 还提供了简写 `|&`，相当于 `2>&1 |`。

</details>
