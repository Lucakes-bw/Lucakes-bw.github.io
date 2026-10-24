---
title: 进程与系统监控
date: 2026-10-24 09:20:00
updated: 2026-10-24 09:20:00
categories:
  - Linux基础
  - 常用命令
tags:
  - Linux
  - 进程
  - 信号
order: 5
description: 程序运行起来就是进程。本文介绍用 ps、pstree、top 查看进程，用 kill 发送信号，用 &、jobs、fg、bg 管理前后台任务，实测 SIGTERM 和 SIGKILL 的区别：前者让程序有机会清理临时文件，后者直接终止，临时文件留了下来；最后用 df、du、free 和 /proc 查看系统资源。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用 `ps`、`pstree`、`top` 查看进程；理解进程的状态和父子关系；会用 `kill` 发送信号，分清 SIGTERM 和 SIGKILL；会管理前后台任务；会查看磁盘、内存和 CPU 的使用情况；认识 `/proc`。
>
> **前置知识**：《认识 Linux 与 Shell》、《用户、权限与软件包》。
>
> **实验环境**：Ubuntu 24.04 容器。容器中只运行着很少的进程，所以下面的进程列表比一台普通的 Linux 电脑短得多。为了让输出清楚，进程相关的实验写在一个脚本 `proc-demo.sh` 中运行。

## 1. 进程

**程序**是磁盘上的一个文件，**进程**是程序的一次运行。同一个程序可以同时运行多次，每次都是一个独立的进程。每个进程有一个唯一的编号 **PID**，并且记录着它的**父进程**的编号 **PPID**：进程总是由另一个进程创建的，比如在 Shell 中输入一个命令，Shell 就创建了一个子进程来运行它。第 4 章会用 `fork` 和 `exec` 系统调用亲手创建进程。

## 2. 查看进程

脚本中先在后台启动两个进程：`sleep 1000`（什么都不做，只是等待），和 `yes > /dev/null`（不停地输出 `y`，会占满一个 CPU 核）。命令末尾的 **`&`** 表示在后台运行，Shell 不等它结束就继续执行下一条命令。

### 2.1 ps

```bash
sleep 1000 &
yes > /dev/null &
ps
```

```
    PID TTY          TIME CMD
   4926 ?        00:00:00 bash
   4932 ?        00:00:00 sleep
   4933 ?        00:00:00 yes
   4935 ?        00:00:00 ps
```

不带参数的 `ps` 只显示当前终端中的进程。最常用的两种形式是 `ps -ef` 和 `ps aux`，显示系统中的**所有**进程：

```bash
ps -ef
```

```
UID          PID    PPID  C STIME TTY          TIME CMD
lucky       4926       0  0 01:57 ?        00:00:00 bash proc-demo.sh
lucky       4932    4926  0 01:57 ?        00:00:00 sleep 1000
lucky       4933    4926 99 01:57 ?        00:00:00 yes
lucky       4936    4926  0 01:57 ?        00:00:00 ps -ef
```

（这里省略了容器自身的进程。）`sleep` 和 `yes` 的 PPID 都是 4926，也就是运行脚本的 bash。

```bash
ps aux --sort=-%cpu | head -3
```

```
USER         PID %CPU %MEM    VSZ   RSS TTY      STAT START   TIME COMMAND
lucky       4933 99.0  0.0   2268  1168 ?        R    01:57   0:00 yes
root           1  0.0  0.0   2268  1224 ?        Ss   01:57   0:00 sleep infinity
```

`ps aux` 多了 CPU 和内存的使用率：`yes` 占用了 99% 的 CPU。`--sort=-%cpu` 按 CPU 使用率从高到低排序（`-%mem` 按内存）。

| 列 | 含义 |
|---|---|
| `%CPU`、`%MEM` | CPU 和内存的使用率 |
| `VSZ` | 虚拟内存的大小（KB） |
| `RSS` | 实际占用的物理内存（KB） |
| `STAT` | 状态（见下表） |
| `TIME` | 累计使用的 CPU 时间 |

进程的主要**状态**：

| 状态 | 含义 |
|---|---|
| `R` | 正在运行，或者在等待 CPU |
| `S` | 睡眠，在等待某件事（比如等待输入、`sleep`） |
| `D` | 不可中断的睡眠，通常在等待磁盘 IO |
| `T` | 被暂停 |
| `Z` | 僵尸：已经结束，但父进程还没有回收它（第 4.1 篇会复现） |

在一台普通的 Linux 电脑上，绝大多数进程大部分时间都处于 `S` 状态。

### 2.2 pstree

```bash
pstree -p $$
```

```
bash(4926)-+-pstree(4941)
           |-sleep(4932)
           `-yes(4933)
```

`pstree` 把父子关系画成一棵树，`-p` 显示 PID。`$$` 是当前 Shell 自己的 PID。在普通的系统上运行 `pstree`，可以看到所有的进程最终都来自 PID 为 1 的 `systemd`。

### 2.3 top

`top` 实时地显示系统的整体状态和最占资源的进程，按 `q` 退出。下面用 `top -b -n 1` 输出一次快照：

```
top - 01:57:43 up 9 min,  0 user,  load average: 0.00, 0.01, 0.00
Tasks:   6 total,   2 running,   4 sleeping,   0 stopped,   0 zombie
%Cpu(s): 10.0 us, 40.0 sy,  0.0 ni, 50.0 id,  0.0 wa,  0.0 hi,  0.0 si,  0.0 st 
MiB Mem :   1959.6 total,    421.7 free,    247.7 used,   1379.2 buff/cache     
MiB Swap:      0.0 total,      0.0 free,      0.0 used.   1711.9 avail Mem 

    PID USER      PR  NI    VIRT    RES    SHR S  %CPU  %MEM     TIME+ COMMAND
   4897 lucky     20   0    2268   1156   1064 R 100.0   0.1   0:01.71 yes
```

- 第一行：当前时间、开机时长、**平均负载**（过去 1、5、15 分钟，等待运行的进程数的平均值；长期超过 CPU 的核数说明系统过载）；
- `%Cpu(s)`：`us` 是用户程序，`sy` 是内核，`id` 是空闲，`wa` 是等待 IO；这台虚拟机有 2 个核，`yes` 占满了其中一个，所以空闲是 50%；
- 内存一行中，`buff/cache` 是内核用来缓存文件的内存，需要时可以释放，所以真正可用的内存看 `avail Mem`。

（平均负载显示为 0.00，是因为快照是在 `yes` 启动一秒后拍的，平均值还来不及升高。）

在 `top` 中按 `P` 按 CPU 排序，`M` 按内存排序，`k` 输入 PID 终止进程。`htop` 是一个更直观、可以用鼠标操作的替代品（`sudo apt install htop`）。

## 3. 信号与 kill

**信号**是发送给进程的一种通知。`kill` 命令的名字有点误导，它的作用其实是**发送信号**：

```bash
kill -l | head -3
```

```
 1) SIGHUP	 2) SIGINT	 3) SIGQUIT	 4) SIGILL	 5) SIGTRAP
 6) SIGABRT	 7) SIGBUS	 8) SIGFPE	 9) SIGKILL	10) SIGUSR1
11) SIGSEGV	12) SIGUSR2	13) SIGPIPE	14) SIGALRM	15) SIGTERM
```

最常用的几个：

| 信号 | 编号 | 含义 |
|---|---|---|
| SIGTERM | 15 | 请求终止，`kill` 的默认信号；**程序可以捕获它，做完清理再退出** |
| SIGKILL | 9 | 强制终止，**不能被捕获或忽略** |
| SIGINT | 2 | 中断，按 Ctrl+C 时发送 |
| SIGSTOP / SIGCONT | 19 / 18 | 暂停 / 继续 |
| SIGSEGV | 11 | 段错误，访问了非法的内存（《指针入门》中的崩溃就是它） |

### 3.1 SIGTERM 和 SIGKILL 的区别

写一个脚本，它创建一个临时文件，然后一直运行；用 `trap` 设置收到 SIGTERM 时的处理：删除临时文件再退出。

```bash
#!/bin/bash
trap "echo 收到 SIGTERM，清理临时文件后退出; rm -f /tmp/work.tmp; exit 0" TERM
touch /tmp/work.tmp
while true; do sleep 0.1; done
```

分别用 `kill`（SIGTERM）和 `kill -9`（SIGKILL）终止它：

```bash
bash graceful.sh & P=$!
kill $P
wait $P; echo "exit=$?"
ls /tmp/work.tmp

bash graceful.sh & P=$!
kill -9 $P
wait $P; echo "exit=$?"
ls /tmp/work.tmp
```

```
收到 SIGTERM，清理临时文件后退出
exit=0
ls: cannot access '/tmp/work.tmp': No such file or directory
exit=137
/tmp/work.tmp
```

（`$!` 是最后一个后台进程的 PID，`wait` 等待它结束并取得它的退出码。）

- 收到 SIGTERM，脚本执行了清理，临时文件被删除了；
- 收到 SIGKILL，进程被内核直接终止，**没有任何机会做清理**，临时文件留了下来。退出码 137 = 128 + 9，表示被 9 号信号杀死。

所以**应该先用 `kill`（SIGTERM），等一会儿还不退出，才用 `kill -9`**。数据库这样的程序被 `kill -9` 时，可能来不及把数据写回磁盘。

### 3.2 按名字发送信号

```bash
sleep 300 &
pgrep -af "sleep 300"
pkill -f "sleep 300"
```

```
4952 sleep 300
```

`pgrep` 按名字查找进程的 PID（`-a` 同时显示命令行，`-f` 匹配完整的命令行），`pkill` 按名字发送信号。用 `pkill` 时要小心匹配得太宽：一开始我用 `pgrep sleep`，还匹配到了容器自己的 `sleep infinity`（PID 1）。

## 4. 前台和后台

在终端中直接运行的命令在**前台**，Shell 要等它结束才能输入下一条命令。管理前后台任务（叫作**作业**，job）的方法：

| 操作 | 作用 |
|---|---|
| `命令 &` | 在后台启动 |
| Ctrl+Z | 暂停前台的作业（发送 SIGTSTP） |
| `jobs` | 列出当前 Shell 的作业 |
| `bg %n` | 让暂停的作业 n 在后台继续运行 |
| `fg %n` | 把作业 n 调到前台 |
| `kill %n` | 向作业 n 发送信号 |

```bash
jobs -l
kill %2
jobs -l
kill -STOP %1
jobs -l
ps -o pid,stat,cmd -p $(jobs -p %1)
bg %1
ps -o pid,stat,cmd -p $(jobs -p %1)
```

```
[1]-  4932 Running                 sleep 1000 &
[2]+  4933 Running                 yes > /dev/null &
[2]+  Terminated              yes > /dev/null
[1]+  4932 Running                 sleep 1000 &
[1]+  4932 Stopped (signal)        sleep 1000
    PID STAT CMD
   4932 T    sleep 1000
    PID STAT CMD
   4932 S    sleep 1000
```

`%1`、`%2` 是作业编号（不是 PID）。暂停后进程状态变成了 `T`，`bg` 之后恢复为 `S`。（脚本中用 `kill -STOP` 代替了在终端中按 Ctrl+Z，效果相同。）

**关闭终端时，它的后台作业通常也会被终止**（收到 SIGHUP 信号）。要让程序在退出登录后继续运行，可以用 `nohup 命令 &`，或者使用 `tmux`、`screen` 这样的终端复用工具；正式的服务应该交给 `systemd` 管理。

## 5. 系统资源

### 5.1 磁盘：df 和 du

```bash
df -h / /dev/shm
du -sh ~/logs /usr/include
du -h --max-depth=1 /usr | sort -hr | head -4
```

```
Filesystem      Size  Used Avail Use% Mounted on
overlay          30G  921M   27G   4% /
shm              64M     0   64M   0% /dev/shm
200K	/home/lucky/logs
29M	/usr/include
600M	/usr
300M	/usr/lib
103M	/usr/libexec
98M	/usr/bin
```

- `df` 显示每个**文件系统**的总空间和剩余空间；`-h` 用易读的单位。
- `du` 统计**目录**占用的空间；`-s` 只显示总计，`--max-depth=1` 显示每个子目录。
- `du -h --max-depth=1 | sort -hr | head` 是查找「什么占用了磁盘」的常用组合（`sort -h` 能正确比较 `98M` 和 `1.2G` 这样的大小）。

### 5.2 内存：free

```bash
free -h
```

```
               total        used        free      shared  buff/cache   available
Mem:           1.9Gi       247Mi       420Mi       752Ki       1.3Gi       1.7Gi
Swap:             0B          0B          0B
```

`free` 只有 420 MiB，但 `available` 有 1.7 GiB。Linux 会把空闲的内存用来缓存文件（`buff/cache`），让读取更快，需要时再释放给程序。所以**判断内存够不够，看 `available`，而不是 `free`**。

### 5.3 CPU 和负载

```bash
uptime
nproc
```

```
 01:57:55 up 9 min,  0 user,  load average: 0.00, 0.01, 0.00
2
```

`nproc` 显示 CPU 的核数。平均负载要和核数比较：2 核的机器，负载 2.0 表示正好满负荷。

## 6. /proc：内核的窗口

`/proc` 不是真正存储在磁盘上的目录，而是内核实时生成的**虚拟文件系统**。每个进程在其中有一个以 PID 命名的目录，`/proc/self` 指向读取它的那个进程自己：

```bash
grep -E "^(Name|State|Pid|PPid|Uid|VmRSS|Threads)" /proc/self/status
```

```
Name:	grep
State:	R (running)
Pid:	4972
PPid:	4957
Uid:	1001	1001	1001	1001
VmRSS:	    1540 kB
Threads:	1
```

这里读取 `/proc/self` 的是 `grep` 自己，所以显示的是 `grep` 进程的信息：正在运行，占用 1540 KB 内存，只有一个线程。`ps`、`top` 显示的信息，其实就是从 `/proc` 中读出来的。

```bash
ls -l /proc/$$/fd
```

```
total 0
lr-x------ 1 lucky lucky 64 Oct 24 01:57 0 -> /dev/null
l-wx------ 1 lucky lucky 64 Oct 24 01:57 1 -> pipe:[18777]
l-wx------ 1 lucky lucky 64 Oct 24 01:57 2 -> pipe:[18778]
```

`/proc/PID/fd` 列出了进程打开的所有文件。0、1、2 就是上一篇讲的标准输入、标准输出、标准错误：这个 Shell 是通过 `docker exec` 运行的，标准输入连接到 `/dev/null`，输出连接到管道。第 3 章讲文件描述符时，会经常查看这个目录。

其他常用的文件：`/proc/cpuinfo`（CPU 信息）、`/proc/meminfo`（内存的详细信息，`free` 就读取它）、`/proc/loadavg`（平均负载）。

## 7. 常见错误

1. **一上来就 `kill -9`**：程序没有机会清理，可能留下临时文件、锁文件，甚至损坏数据。先用 `kill`。
2. **`pkill` 的匹配太宽**：误杀了其他进程。先用 `pgrep -a` 确认会匹配到哪些。
3. **看到 `free` 很小就以为内存不足**：要看 `available`。
4. **关闭终端后后台程序也结束了**：用 `nohup`、`tmux` 或 `systemd`。
5. **混淆作业编号和 PID**：`kill %1` 和 `kill 1` 完全不同，后者是向 PID 为 1 的进程发送信号。

## 8. 小结

- 进程是程序的一次运行，有 PID 和父进程的 PPID；`ps -ef`、`ps aux`、`pstree`、`top` 查看进程，状态有 R、S、D、T、Z
- `kill` 发送信号：SIGTERM（15，默认）可以被捕获，程序能做清理；SIGKILL（9）不能被捕获；实测 SIGTERM 时脚本删除了临时文件，SIGKILL 时临时文件留了下来，退出码 137 = 128 + 9
- `&` 后台运行，`jobs`、`fg`、`bg`、Ctrl+Z 管理作业
- `df` 看文件系统、`du` 看目录、`free` 看内存（看 available）、`uptime` 看负载
- `/proc` 是内核提供的虚拟文件系统，`/proc/PID/status`、`/proc/PID/fd` 显示进程的信息

## 9. 练习

1. 找出当前占用内存最多的进程。
2. 一个进程被 SIGTERM 终止（没有捕获这个信号），它的退出码是多少？被 Ctrl+C 终止呢？
3. 磁盘快满了，怎样找出主目录中占用空间最大的几个子目录？

<details>
<summary>点击查看答案</summary>

1. `ps aux --sort=-%mem | head -2`，或者在 `top` 中按 `M`。

2. 被信号终止的进程，退出码是 128 加上信号的编号。实测：

   ```bash
   sleep 100 & P=$!; kill $P; wait $P; echo "TERM exit=$?"
   sleep 100 & P=$!; kill -INT $P; wait $P; echo "INT exit=$?"
   ```

   ```
   TERM exit=143
   INT exit=130
   ```

   143 = 128 + 15，130 = 128 + 2。看到退出码大于 128，就可以推断出进程是被哪个信号终止的。

3. `du -h --max-depth=1 ~ | sort -hr | head`。找到大的目录后，可以进入它，重复这个命令，逐层找到占用空间的文件。

</details>
