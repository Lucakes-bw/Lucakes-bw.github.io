---
title: 进程：fork、exec、wait
date: 2026-10-24 10:50:00
updated: 2026-10-24 10:50:00
categories:
  - Linux基础
  - 进程与线程
tags:
  - Linux
  - 进程
  - fork
order: 1
description: Linux 上创建进程只有一种方式：fork 复制自己，再用 exec 换成别的程序，父进程用 wait 等它结束。本文实测 fork 后父子进程的变量互不影响、printf 的内容被输出了两次；用进程树演示循环中 fork 产生 2 的 n 次方个进程；复现僵尸进程和孤儿进程，并发现实验容器中已经积累了 12 个僵尸；测得父进程占用 500 MB 内存时 fork 要 1.6 毫秒；最后写一个支持重定向和管道的迷你 Shell。
top_group_list: true
disableNunjucks: true
viz: [lx-proc]
---

> **本文目标**：会用 `fork` 创建子进程，理解它的返回值和「复制」的含义；会用 `exec` 系列函数运行另一个程序；会用 `wait`、`waitpid` 等待子进程并获取它的退出状态；理解僵尸进程和孤儿进程；最后综合前面几篇的内容，写一个迷你 Shell。
>
> **前置知识**：《进程与系统监控》中的进程、PID、状态；《重定向与管道的原理》中的 `dup2` 和 `pipe`。
>
> **实验环境**：Ubuntu 24.04 容器（aarch64），GCC 13.3。

## 1. fork：复制出一个进程

```c
#include <unistd.h>

pid_t fork(void);
```

`fork` 创建一个新的进程（**子进程**），它几乎是调用者（**父进程**）的完整复制：同样的代码、同样的变量值、同样打开的文件，并且**从 fork 返回的地方开始执行**。于是 `fork` 「调用一次，返回两次」：

| 在哪个进程中 | `fork` 的返回值 |
|---|---|
| 父进程 | 子进程的 PID（大于 0） |
| 子进程 | 0 |
| 失败时（父进程中） | -1 |

程序根据返回值区分自己是父进程还是子进程：

```c
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    int x = 100;
    printf("fork 之前：pid = %d\n", getpid());

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return 1;
    }
    if (pid == 0) {                              // 子进程中，fork 返回 0
        x += 1;
        printf("子进程：pid = %d，父进程 = %d，x = %d，&x = %p\n", getpid(), getppid(), x, (void *)&x);
        return 0;
    }
    // 父进程中，fork 返回子进程的 pid
    x -= 1;
    printf("父进程：pid = %d，子进程 = %d，x = %d，&x = %p\n", getpid(), pid, x, (void *)&x);
    wait(NULL);
    return 0;
}
```

```
fork 之前：pid = 7349
父进程：pid = 7349，子进程 = 7350，x = 99，&x = 0xffffd4f6e994
子进程：pid = 7350，父进程 = 7349，x = 101，&x = 0xffffd4f6e994
```

- 「fork 之前」只输出了一次，之后的代码父子进程**各执行一次**；
- 子进程把 `x` 加 1，父进程把 `x` 减 1，结果一个是 101、一个是 99：**两个进程的内存是独立的**，修改互不影响；
- 有意思的是，两个 `x` 的**地址完全相同**。这是因为每个进程有自己的**虚拟地址空间**，同一个虚拟地址在两个进程中对应不同的物理内存；
- 父子进程谁先输出是不确定的，由调度决定。

`wait(NULL)` 让父进程等子进程结束后再退出，第 3 节详细讲。

### 1.1 写时复制

复制整个进程的内存听起来很慢。实际上，`fork` 时内核并不复制物理内存，而是让父子进程**共享**所有的页，并把它们标记为只读；任何一方要写某一页时，才复制这一页（**写时复制**，copy-on-write，上一篇的 `MAP_PRIVATE` 也是这个机制）。但页表本身还是要复制的，所以父进程占用的内存越多，`fork` 越慢。测量 1000 次 `fork` 的平均时间：

```c
// forkcost.c 的核心
for (int i = 0; i < n; i++) {
    pid_t pid = fork();
    if (pid == 0) _exit(0);
    waitpid(pid, NULL, 0);
}
```

```
父进程占用    0 MB：fork + 退出   41.1 微秒，fork + exec 一个程序   94.0 微秒
父进程占用  100 MB：fork + 退出  364.3 微秒，fork + exec 一个程序  423.3 微秒
父进程占用  500 MB：fork + 退出 1662.1 微秒，fork + exec 一个程序 1723.7 微秒
```

父进程占用 500 MB 时，一次 `fork` 要 1.6 毫秒，是不占内存时的 40 倍，几乎和占用的内存成正比。这就是为什么占用大量内存的程序（如数据库）创建子进程时要格外小心。（`fork + exec` 是创建子进程后立即运行 `/bin/true`。）

### 1.2 printf 被输出了两次

`fork` 复制的不只是变量，还有 **stdio 的缓冲区**：

```c
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    printf("hello ");                            // 没有换行
    if (fork() == 0) {
        printf("from child\n");
        return 0;
    }
    wait(NULL);
    printf("from parent\n");
    return 0;
}
```

```
hello from child
hello from parent
```

`hello ` 只调用了一次 `printf`，却出现了两次。它没有换行，`fork` 时还在缓冲区中，子进程得到了一份缓冲区的副本，父子进程退出时各自把缓冲区写出一次。

更隐蔽的情况：`fork1.c` 中的「fork 之前」有换行，在终端中只输出一次；但如果把输出接到管道：

```bash
./fork1 | cat
```

```
fork 之前：pid = 7316
子进程：pid = 7318，父进程 = 7316，x = 101，&x = 0xffffd379beb4
fork 之前：pid = 7316
父进程：pid = 7316，子进程 = 7318，x = 99，&x = 0xffffd379beb4
```

「fork 之前」也输出了两次。《文件描述符与系统调用》中讲过，`stdout` 接管道或文件时是**全缓冲**的，换行也不会刷新缓冲区。所以**在 fork 之前调用 `fflush(stdout)`**，是个好习惯。子进程结束时用 `_exit` 而不是 `exit`，也可以避免把继承来的缓冲区再写一次（`_exit` 直接结束进程，不刷新 stdio 的缓冲区）。

### 1.3 循环中的 fork

```c
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    for (int i = 0; i < 3; i++) {
        fork();
    }
    printf("pid = %d，父进程 = %d\n", getpid(), getppid());
    while (wait(NULL) > 0) {                     // 等待自己的所有子进程
    }
    return 0;
}
```

```
pid = 7356，父进程 = 7355
pid = 7358，父进程 = 7356
pid = 7359，父进程 = 7356
pid = 7357，父进程 = 7356
pid = 7362，父进程 = 7357
pid = 7360，父进程 = 7358
pid = 7361，父进程 = 7357
pid = 7363，父进程 = 7361
```

循环 3 次，输出了 8 行，也就是有 8 个进程。从 PID 和父进程可以画出一棵树：7356 是最初的进程，它有 3 个子进程（7357、7358、7359），7357 又有 2 个子进程……下面的演示逐步展示了这棵树是怎样长出来的：

<div class="dsviz" data-viz="fork-loop"></div>

每一轮循环中，**所有**已经存在的进程都执行一次 `fork`，进程数翻倍，所以 n 次循环得到 2ⁿ 个进程。如果把循环次数写成一个很大的数，或者在无限循环中 fork，进程数会指数增长，很快耗尽系统的资源（这就是所谓的 fork 炸弹）。`ulimit -u` 可以限制一个用户能创建的进程数。

## 2. exec：换成另一个程序

`fork` 出的子进程运行的还是同一个程序。要运行别的程序，用 **exec** 系列函数：它把当前进程的代码和数据**替换**成另一个程序，从新程序的 `main` 开始执行。PID 不变，打开的文件描述符也不变（上一篇的重定向就利用了这一点）。

```c
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    printf("exec 之前：pid = %d\n", getpid());
    fflush(stdout);
    execlp("ps", "ps", "-o", "pid,cmd", NULL);   // 参数列表以 NULL 结尾
    perror("execlp");                            // 只有 exec 失败才会执行到这里
    return 1;
}
```

```
exec 之前：pid = 7406
    PID CMD
   7405 sh -c ./exec1
   7406 ps -o pid,cmd
```

`ps` 显示的 7406 号进程，就是刚才的 `exec1`：PID 没有变，程序已经换成了 `ps`。**exec 成功后不会返回**，后面的 `perror` 只有在失败时（比如找不到程序）才会执行。

exec 有 6 个版本，名字中的字母表示参数的形式：

| 函数 | 参数形式 | 查找 `PATH` |
|---|---|---|
| `execl(path, arg0, arg1, ..., NULL)` | **l**ist：逐个列出 | 否，要给出完整路径 |
| `execlp(file, arg0, ..., NULL)` | 同上 | **p**：是 |
| `execv(path, argv)` | **v**ector：数组 | 否 |
| `execvp(file, argv)` | 同上 | 是 |
| `execle`、`execve` | 再加一个环境变量数组（**e**） | 否 |

其中 `execve` 是真正的系统调用，其余都是在它基础上包装的库函数。注意 `arg0` 是程序自己的名字（就是 `main` 中的 `argv[0]`），参数列表和数组都以 `NULL` 结尾。

**fork + exec** 就是 Linux 上运行一个新程序的标准方法：Shell 每执行一个外部命令，都是先 `fork` 出一个子进程，再在子进程中 `exec`。

## 3. wait：等待子进程结束

```c
#include <sys/wait.h>

pid_t wait(int *status);                              // 等待任意一个子进程
pid_t waitpid(pid_t pid, int *status, int options);   // 等待指定的子进程
```

它们阻塞父进程，直到有子进程结束，返回那个子进程的 PID，并把它的结束状态存入 `status`。`status` 是一个编码过的整数，要用宏来解读：

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static void report(pid_t pid, int status)
{
    if (WIFEXITED(status)) {
        printf("子进程 %d 正常结束，退出码 %d\n", pid, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        printf("子进程 %d 被信号 %d 终止%s\n", pid, WTERMSIG(status), WCOREDUMP(status) ? "，生成了 core" : "");
    }
}

int main(void)
{
    pid_t a = fork();
    if (a == 0) exit(3);                         // 正常结束，退出码 3

    pid_t b = fork();
    if (b == 0) {
        int *p = NULL;
        return *p;                               // 段错误
    }

    pid_t c = fork();
    if (c == 0) {
        pause();                                 // 一直等待，直到收到信号
        return 0;
    }
    kill(c, SIGTERM);

    int status;
    pid_t pid;
    while ((pid = wait(&status)) > 0) {          // 任意一个子进程结束，wait 就返回
        report(pid, status);
    }
    return 0;
}
```

```
子进程 7408 正常结束，退出码 3
子进程 7409 被信号 11 终止
子进程 7410 被信号 15 终止
```

| 宏 | 含义 |
|---|---|
| `WIFEXITED(s)` | 是否正常结束（`return` 或 `exit`） |
| `WEXITSTATUS(s)` | 退出码（0 到 255） |
| `WIFSIGNALED(s)` | 是否被信号终止 |
| `WTERMSIG(s)` | 信号的编号：11 是 SIGSEGV（段错误），15 是 SIGTERM |
| `WCOREDUMP(s)` | 是否生成了 core 文件 |

Shell 中 `$?` 显示的「128 + 信号编号」（《进程与系统监控》），就是 Shell 根据 `WIFSIGNALED` 和 `WTERMSIG` 算出来的。

`waitpid` 的第三个参数可以是 `WNOHANG`：没有子进程结束时立即返回 0，而不是等待。

## 4. 僵尸进程和孤儿进程

### 4.1 僵尸进程

子进程结束后，内核释放了它的内存和打开的文件，但必须**保留它的退出状态**，等父进程用 `wait` 来取。在被 `wait` 之前，这个已经结束的进程就是**僵尸进程**（zombie）：

```c
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        printf("子进程 %d 结束\n", getpid());
        exit(0);
    }
    sleep(1);
    char cmd[64];
    snprintf(cmd, sizeof cmd, "ps -o pid,ppid,stat,cmd -p %d,%d", getpid(), pid);
    printf("--- 父进程还没有 wait\n");
    fflush(stdout);
    if (system(cmd) == -1) perror("system");

    waitpid(pid, NULL, 0);
    printf("--- 父进程 wait 之后\n");
    fflush(stdout);
    if (system(cmd) == -1) perror("system");
    return 0;
}
```

```
子进程 7412 结束
--- 父进程还没有 wait
    PID    PPID STAT CMD
   7411    7370 S    ./zombie
   7412    7411 Z    [zombie] <defunct>
--- 父进程 wait 之后
    PID    PPID STAT CMD
   7411    7370 S    ./zombie
```

子进程 7412 已经结束了，却还在进程列表中，状态是 **Z**，名字后面标着 `<defunct>`（已失效）。父进程 `waitpid` 之后，它才彻底消失。

僵尸不占内存，也不能被 `kill`（它已经死了），但它占用一个进程号。一个长期运行的服务器如果不断创建子进程却从不 `wait`，僵尸就会越积越多，最终耗尽进程号，无法创建新的进程。

### 4.2 孤儿进程

反过来，父进程先结束了，子进程还在运行，这个子进程就成了**孤儿进程**：

```c
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        printf("子进程：父进程是 %d\n", getppid());
        sleep(1);                                // 等父进程先结束
        printf("子进程：父进程变成了 %d\n", getppid());
        return 0;
    }
    printf("父进程 %d 结束\n", getpid());
    return 0;
}
```

```
父进程 7433 结束
子进程：父进程是 7433
子进程：父进程变成了 1
```

父进程结束后，子进程的父进程变成了 **1 号进程**：内核会把孤儿「过继」给 1 号进程（在普通的 Linux 系统上是 `systemd`）。1 号进程的职责之一就是 `wait` 所有过继来的子进程，所以孤儿结束后不会留下僵尸。孤儿进程本身没有任何问题，在后台运行的守护进程，通常就是有意制造的孤儿。

下面的演示展示了两种情况：

<div class="dsviz" data-viz="proc-life"></div>

### 4.3 实验容器中的僵尸

看一下实验容器中的 1 号进程：

```bash
ps -eo pid,ppid,stat,cmd | head -5
```

```
    PID    PPID STAT CMD
      1       0 Ss   sleep infinity
   4996       1 Z    [sleep] <defunct>
   5019       1 Z    [sleep] <defunct>
   5590       1 Z    [hang] <defunct>
```

容器的 1 号进程是启动容器时指定的 `sleep infinity`，它只会睡眠，**从来不调用 `wait`**。所以本专栏的实验中，凡是父进程先退出、被过继给 1 号进程的子进程，结束后都成了僵尸：

```bash
ps -eo stat,ppid,cmd | awk '$1 ~ /^Z/' | sort | uniq -c | sort -rn
```

```
      3 Z          1 [ls] <defunct>
      2 Z          1 [wc] <defunct>
      2 Z          1 [sleep] <defunct>
      2 Z          1 [orphan] <defunct>
      2 Z          1 [hang] <defunct>
      1 Z          1 [mypipe_bug] <defunct>
```

一共 12 个，包括《GDB 与 core dump》中卡住的 `hang`、上一篇卡住的 `mypipe_bug` 和它的子进程，以及刚才的 `orphan`。这是 Docker 中的一个常见问题，解决方法是让一个会 `wait` 的小程序作为容器的 1 号进程，比如 `docker run --init`（使用 tini）。

## 5. 一个迷你 Shell

综合 `fork`、`exec`、`wait`，以及上一篇的 `dup2`、`pipe`，就可以写一个 Shell 了。它支持内置命令 `cd`、`exit`，重定向 `<`、`>`，以及一个 `|`：

```c
// mysh.c：一个迷你 Shell，支持 cd、exit、> 文件、< 文件 和一个 |
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ARGS 64

struct cmd {
    char *argv[MAX_ARGS];
    char *in;                                    // < 文件，没有时为 NULL
    char *out;                                   // > 文件，没有时为 NULL
};

// 把一段命令按空白拆开，识别 < 和 >
static int parse(char *s, struct cmd *c)
{
    int n = 0;
    c->in = c->out = NULL;
    for (char *tok = strtok(s, " \t\n"); tok; tok = strtok(NULL, " \t\n")) {
        if (strcmp(tok, "<") == 0 || strcmp(tok, ">") == 0) {
            char *file = strtok(NULL, " \t\n");
            if (file == NULL) {
                fprintf(stderr, "mysh: %s 后面缺少文件名\n", tok);
                return -1;
            }
            if (tok[0] == '<') c->in = file; else c->out = file;
        } else if (n < MAX_ARGS - 1) {
            c->argv[n++] = tok;
        }
    }
    c->argv[n] = NULL;
    return n;
}

// 在子进程中执行：先处理重定向，再 exec
static void run_child(struct cmd *c)
{
    if (c->in) {
        int fd = open(c->in, O_RDONLY);
        if (fd == -1) { perror(c->in); _exit(1); }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }
    if (c->out) {
        int fd = open(c->out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) { perror(c->out); _exit(1); }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    execvp(c->argv[0], c->argv);
    fprintf(stderr, "mysh: %s: 找不到命令\n", c->argv[0]);
    _exit(127);
}

int main(void)
{
    char line[1024];
    int last = 0;                                // 上一条命令的退出码
    for (;;) {
        printf("mysh> ");
        fflush(stdout);
        if (fgets(line, sizeof line, stdin) == NULL) {   // Ctrl+D
            printf("\n");
            break;
        }

        char *bar = strchr(line, '|');
        struct cmd left, right;
        if (bar) *bar = '\0';
        int n = parse(line, &left);
        if (n <= 0) continue;
        if (bar && parse(bar + 1, &right) <= 0) {
            fprintf(stderr, "mysh: | 后面缺少命令\n");
            continue;
        }

        // 内置命令：必须在 Shell 自己的进程中执行
        if (strcmp(left.argv[0], "exit") == 0) break;
        if (strcmp(left.argv[0], "cd") == 0) {
            const char *dir = left.argv[1] ? left.argv[1] : getenv("HOME");
            if (chdir(dir) == -1) perror(dir);
            continue;
        }

        if (!bar) {
            pid_t pid = fork();
            if (pid == 0) run_child(&left);
            int status;
            waitpid(pid, &status, 0);
            last = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        } else {
            int p[2];
            if (pipe(p) == -1) {
                perror("pipe");
                continue;
            }
            pid_t a = fork();
            if (a == 0) {
                dup2(p[1], STDOUT_FILENO);
                close(p[0]); close(p[1]);
                run_child(&left);
            }
            pid_t b = fork();
            if (b == 0) {
                dup2(p[0], STDIN_FILENO);
                close(p[0]); close(p[1]);
                run_child(&right);
            }
            close(p[0]); close(p[1]);
            int status;
            waitpid(a, NULL, 0);
            waitpid(b, &status, 0);
            last = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        }
        if (last != 0) printf("[退出码 %d]\n", last);
    }
    return last;
}
```

```
mysh> echo hello mysh
hello mysh
mysh> ls /etc/hostname /nope
ls: cannot access '/nope': No such file or directory
/etc/hostname
[退出码 2]
mysh> cd /etc
mysh> pwd
/etc
mysh> wc -l < passwd
20
mysh> ls /usr/bin | wc -l
543
mysh> grep lucky passwd > /tmp/me.txt
mysh> cat /tmp/me.txt
lucky:x:1001:1001::/home/lucky:/bin/bash
mysh> nosuchcmd
mysh: nosuchcmd: 找不到命令
[退出码 127]
mysh> exit
```

（`/usr/bin` 中的文件比《文件描述符与系统调用》中的 536 个多了 7 个，是《内存映射 mmap》的实验中安装了 `fincore` 等工具。）

为什么 `cd` 必须是**内置命令**？如果像其他命令一样 `fork` 出子进程去执行 `chdir`，改变的只是子进程的当前目录，子进程一结束就没了，Shell 自己的目录不会变。所以 `cd`、`exit`、`export` 这类要改变 Shell 自身状态的命令，只能在 Shell 的进程中执行。`type cd` 会显示 `cd is a shell builtin`。

这个 Shell 还很简陋：不支持引号、变量、多个管道、后台运行 `&`，按 Ctrl+C 会把 Shell 自己也终止（下一篇的信号会解决这个问题）。但真正的 Shell 的核心，就是这个「读取 → fork → 在子进程中重定向并 exec → wait」的循环。

## 6. 常见错误

1. **不检查 `fork` 的返回值**：`fork` 失败返回 -1，把 -1 当作子进程的 PID 去 `kill(-1, ...)` 会向所有能发送信号的进程发信号。
2. **子进程执行完自己的任务后没有退出**：继续执行父进程的代码，比如又进入了 fork 的循环。子进程的分支最后一定要 `exit` 或 `_exit`。
3. **fork 之前不 `fflush`**：缓冲区中的内容被输出两次。
4. **exec 的参数列表不以 `NULL` 结尾**，或者忘了 `argv[0]`。
5. **以为 exec 之后的代码会执行**：exec 成功就不会返回。
6. **不 `wait` 子进程**：产生僵尸。
7. **在子进程中用 `exit` 而导致重复刷新缓冲区**：fork 出的子进程如果不 exec，结束时用 `_exit`。

## 7. 小结

- `fork` 复制出子进程，父进程中返回子进程的 PID，子进程中返回 0；父子进程的内存独立，通过写时复制实现，但页表要复制，父进程占用的内存越多 fork 越慢（500 MB 时 1.6 毫秒）
- `fork` 也复制了 stdio 的缓冲区，fork 之前要 `fflush`
- n 次循环 fork 产生 2ⁿ 个进程
- `exec` 把当前进程换成另一个程序，PID 和文件描述符不变，成功后不返回；`execvp` 最常用
- `wait`、`waitpid` 等待子进程，用 `WIFEXITED`、`WEXITSTATUS`、`WIFSIGNALED`、`WTERMSIG` 解读状态
- 子进程结束而父进程没有 wait，就成了僵尸（Z）；父进程先结束，子进程被过继给 1 号进程
- Shell = 循环「读取命令 → fork → 子进程中重定向并 exec → 父进程 wait」；`cd` 必须是内置命令

## 8. 练习

1. 下面的程序输出几行 `hi`？

   ```c
   int main(void)
   {
       fork();
       fork() && fork();
       printf("hi\n");
       return 0;
   }
   ```

2. 给 `mysh` 增加后台运行：命令以 `&` 结尾时，不等待子进程结束，立即显示下一个提示符。这样会产生什么问题？怎样解决？
3. 写一个程序，创建 5 个子进程，每个子进程睡眠随机的 1 到 3 秒后以自己的编号作为退出码结束；父进程按子进程**结束的顺序**输出它们的编号。

<details>
<summary>点击查看答案</summary>

1. **6 行**。实测（在末尾加上 `while (wait(NULL) > 0) {}`，让所有进程都等自己的子进程）：

   ```bash
   ./hi | wc -l
   ```

   ```
   6
   ```

   第一个 `fork()` 之后有 2 个进程。每个进程执行 `fork() && fork()`：第一个 `fork` 在父进程中返回非 0，`&&` 继续执行第二个 `fork`；在子进程中返回 0，`&&` 短路，不再执行第二个 `fork`。所以每个进程变成 3 个，共 2 × 3 = 6 个进程，各输出一行。（GCC 会警告 `value computed is not used`，因为 `&&` 的结果没有被使用。）

2. 命令以 `&` 结尾时，去掉 `&`，`fork` 之后不调用 `waitpid`，打印子进程的 PID 就继续下一轮循环。问题是：**后台命令结束后没有人 wait 它，它成了僵尸**。前台命令的 `waitpid(pid, ...)` 只等指定的那个子进程，不会顺便回收它。实测运行 `sleep 1 &`，再前台运行 `sleep 2`，然后看进程：

   ```
   mysh> sleep 1 &
   [19606]
   mysh> echo 前台命令照常执行
   前台命令照常执行
   mysh> sleep 2
   mysh> ps -o pid,stat,cmd
       PID STAT CMD
     19604 Ss+  sh -c ./mysh_bg
     19605 S+   ./mysh_bg
     19606 Z+   [sleep] <defunct>
     19613 R+   ps -o pid,stat,cmd
   ```

   解决方法：每次显示提示符之前，用 `WNOHANG` 回收所有已经结束的子进程：

   ```c
   pid_t done;
   while ((done = waitpid(-1, NULL, WNOHANG)) > 0) {   // 回收已经结束的后台进程
       printf("[%d] 完成\n", done);
   }
   ```

   `waitpid` 的第一个参数为 -1，表示任意一个子进程。改进后同样的操作：

   ```
   mysh> sleep 2
   [19620] 完成
   mysh> ps -o pid,stat,cmd
       PID STAT CMD
     19618 Ss+  sh -c ./mysh_reap
     19619 S+   ./mysh_reap
     19627 R+   ps -o pid,stat,cmd
   ```

   僵尸没有了。不过只有在用户按下回车时才会检查；bash 用 SIGCHLD 信号在子进程结束时立即得到通知，下一篇会讲。

3. 父进程用 `wait` 等待任意一个子进程，哪个先结束就先返回哪个：

   ```c
   #include <stdio.h>
   #include <stdlib.h>
   #include <sys/wait.h>
   #include <time.h>
   #include <unistd.h>

   int main(void)
   {
       for (int i = 1; i <= 5; i++) {
           pid_t pid = fork();
           if (pid == 0) {
               srand(time(NULL) ^ getpid());       // 每个子进程用不同的种子
               int sec = rand() % 3 + 1;
               printf("子进程 %d 将睡眠 %d 秒\n", i, sec);
               fflush(stdout);
               sleep(sec);
               _exit(i);
           }
       }
       int status;
       while (wait(&status) > 0) {
           if (WIFEXITED(status)) {
               printf("子进程 %d 结束\n", WEXITSTATUS(status));
           }
       }
       return 0;
   }
   ```

   ```
   子进程 1 将睡眠 1 秒
   子进程 3 将睡眠 2 秒
   子进程 2 将睡眠 3 秒
   子进程 4 将睡眠 1 秒
   子进程 5 将睡眠 1 秒
   子进程 1 结束
   子进程 4 结束
   子进程 5 结束
   子进程 3 结束
   子进程 2 结束
   ```

   睡眠 1 秒的 1、4、5 先结束，然后是 3，最后是 2。子进程中如果不重新设置随机数种子，5 个子进程继承的是同一个随机数状态，会得到相同的结果。所有子进程都结束后，`wait` 返回 -1（`errno` 为 `ECHILD`），循环结束。

</details>
