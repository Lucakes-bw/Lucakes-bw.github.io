---
title: 命名空间：容器看到的世界
date: 2026-11-12 10:00:00
updated: 2026-11-12 10:00:00
categories:
  - Linux基础
  - 容器原理
tags:
  - Linux
  - 容器
order: 1
description: 容器不是虚拟机，它只是一个被「蒙住了眼睛」的普通进程。蒙眼的工具叫命名空间：同一个内核，不同的进程看到不同的主机名、进程号、挂载点和网卡。本文用 unshare 逐个打开它们，实测创建命名空间的代价：大多数和一次 fork 差不多（40 微秒左右），网络命名空间要 130 微秒。再看容器里的 1 号进程有什么特殊：它不回收孤儿进程，僵尸就会越积越多；它不处理 SIGTERM，信号就被内核直接忽略，所以 docker stop 要等满 10 秒，加上 --init 只要 0.06 秒。
top_group_list: true
disableNunjucks: true
viz: [lx-ctr]
---

> **本文目标**：理解容器就是带有一组命名空间（和 cgroup）的普通进程；知道 Linux 的 8 种命名空间各隔离什么；会用 `unshare` 创建命名空间，用 `/proc/<pid>/ns` 查看；知道创建命名空间的代价；理解 PID 命名空间中 1 号进程的两个特殊之处：回收孤儿进程、信号的默认处理，以及为什么需要 `tini`、`docker run --init`。
>
> **前置知识**：本专栏的《进程：fork、exec、wait》（僵尸进程、孤儿进程）《信号》《进程间通信》。本章的完整代码在 [projects/lx-container](https://github.com/Lucakes-bw/Lucakes-bw.github.io/tree/main/projects/lx-container)。
>
> **实验环境**：Docker 中的 Ubuntu 24.04 容器（以 `--privileged` 运行，这样才能在里面再创建命名空间），内核 6.8，util-linux 2.39；虚拟机 2 核、2 GB 内存。在容器中做容器的实验，正好可以对照「外面」和「里面」。

## 1. 容器只是一个进程

在宿主机上运行一个容器，再在宿主机上用 `ps` 找它，能找到容器中的进程，它们就是宿主机上的普通进程，有自己的进程号，由同一个内核调度。和虚拟机不同，容器**没有自己的内核**。

那为什么在容器里看，它好像是一台独立的机器：进程号从 1 开始、有自己的主机名、自己的网卡和文件系统？因为内核给了它一组**命名空间**（namespace）：同一种全局资源（进程号、主机名、挂载点……），不同命名空间中的进程看到的是不同的一份。再加上第 3 篇的 **cgroup** 限制它能用多少资源，就是容器的全部。

<figure class="diagram">
<svg viewBox="0 0 700 270" role="img" aria-label="容器就是宿主机上的普通进程：两个容器中的进程共享同一个内核，各自带有一组命名空间（决定看到什么）和一个 cgroup（决定能用多少）">
  <defs><marker id="ovw-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="ovw-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="14" width="320" height="170" rx="8" style="fill:none;stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:5 4"/><text x="32.0" y="34.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">容器 A</text><rect x="32" y="46" width="120" height="40" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="92.0" y="62.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">进程（web）</text><text x="92.0" y="77.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">宿主机上 1948 号</text><text x="168.0" y="58.0" text-anchor="start" style="fill:var(--fg);font-size:11px">PID：自己是 1 号</text><text x="168.0" y="76.0" text-anchor="start" style="fill:var(--fg);font-size:11px">UTS：主机名 web</text><text x="168.0" y="94.0" text-anchor="start" style="fill:var(--fg);font-size:11px">mount：镜像的根目录</text><text x="168.0" y="112.0" text-anchor="start" style="fill:var(--fg);font-size:11px">network：自己的 eth0</text><text x="168.0" y="130.0" text-anchor="start" style="fill:var(--fg);font-size:11px">IPC、cgroup、user……</text><rect x="32" y="140" width="296" height="32" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="180.0" y="160.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">cgroup：CPU 1 个、内存 512 MB、进程数 100</text><rect x="360" y="14" width="320" height="170" rx="8" style="fill:none;stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:5 4"/><text x="372.0" y="34.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">容器 B</text><rect x="372" y="46" width="120" height="40" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="432.0" y="62.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">进程（db）</text><text x="432.0" y="77.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">宿主机上 2210 号</text><text x="508.0" y="58.0" text-anchor="start" style="fill:var(--fg);font-size:11px">PID：自己是 1 号</text><text x="508.0" y="76.0" text-anchor="start" style="fill:var(--fg);font-size:11px">UTS：主机名 db</text><text x="508.0" y="94.0" text-anchor="start" style="fill:var(--fg);font-size:11px">mount：镜像的根目录</text><text x="508.0" y="112.0" text-anchor="start" style="fill:var(--fg);font-size:11px">network：自己的 eth0</text><text x="508.0" y="130.0" text-anchor="start" style="fill:var(--fg);font-size:11px">IPC、cgroup、user……</text><rect x="372" y="140" width="296" height="32" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="520.0" y="160.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">cgroup：CPU 2 个、内存 2 GB、进程数 500</text><rect x="20" y="200" width="660" height="34" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="350.0" y="221.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">同一个 Linux 内核（没有虚拟机，没有第二个内核）</text><line x1="180" y1="184" x2="180" y2="198" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ovw-a)"/><line x1="520" y1="184" x2="520" y2="198" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ovw-a)"/><text x="350.0" y="258.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">命名空间：看到什么　　cgroup：能用多少</text>
  </g>
</svg>
</figure>

Linux 有 8 种命名空间：

| 命名空间 | 隔离什么 | `unshare` 的选项 | 引入的内核版本 |
|---|---|---|---|
| mount | 挂载点（看到的文件系统） | `--mount` | 2.4.19 |
| UTS | 主机名、域名 | `--uts` | 2.6.19 |
| IPC | System V IPC、POSIX 消息队列 | `--ipc` | 2.6.19 |
| PID | 进程号 | `--pid` | 2.6.24 |
| network | 网卡、路由表、防火墙规则、端口 | `--net` | 2.6.29 |
| user | 用户号、组号、capabilities | `--user` | 3.8 |
| cgroup | 看到的 cgroup 层级 | `--cgroup` | 4.6 |
| time | 开机时间、单调时钟的偏移 | `--time` | 5.6 |

每个进程属于每种命名空间中的一个，`/proc/<pid>/ns/` 下的符号链接说明是哪一个：

```
$ ls -l /proc/self/ns
cgroup -> cgroup:[4026532521]
ipc -> ipc:[4026532519]
mnt -> mnt:[4026532517]
net -> net:[4026532522]
pid -> pid:[4026532520]
time -> time:[4026532581]
user -> user:[4026531837]
uts -> uts:[4026532518]
...
```

方括号中的数字是命名空间的编号，两个进程的编号相同，就在同一个命名空间中。这里的 `user:[4026531837]` 是系统最初的那个 user 命名空间，和宿主机相同：Docker 默认**不**隔离用户，容器中的 root 就是宿主机的 root（第 5 篇）。

## 2. 用 unshare 逐个打开

`unshare` 命令（和同名的系统调用）让一个进程离开当前的命名空间，进入新建的命名空间，然后运行指定的命令。

**UTS：主机名**

```
$ hostname
e66c8d76e806
$ unshare --uts sh -c "hostname inside-uts; hostname; readlink /proc/self/ns/uts"
inside-uts
uts:[4026532948]
$ hostname
e66c8d76e806
```

在新的 UTS 命名空间中改了主机名，外面不受影响。容器的主机名就是这样设置的。

**PID：进程号**

```
$ unshare --pid --fork --mount-proc sh -c 'echo $$; ps -e | wc -l'
1
4
```

`sh` 在新的 PID 命名空间中是 **1 号进程**，`ps` 只看到 3 个进程（加上标题行是 4 行）。`--fork` 是因为 `unshare` 系统调用不会改变调用者自己的 PID 命名空间，只影响之后创建的子进程；`--mount-proc` 同时创建新的 mount 命名空间，重新挂载 `/proc`，否则 `ps` 读的还是外面的 `/proc`，看到的是所有进程。

进程号在每一层命名空间中各有一个：容器中的 1 号进程在宿主机上可能是 1948 号。`/proc/<pid>/status` 中的 `NSpid` 一行会列出它在每一层中的进程号。

**mount：挂载点**

```
$ unshare --mount sh -c 'mount -t tmpfs none /srv/demo; touch /srv/demo/x; echo 里面：$(ls /srv/demo)'
里面：x
$ ls /srv/demo | wc -l
0
```

新的 mount 命名空间一开始是外面的一份副本，之后的挂载和卸载互不影响。第 2 篇会在这个基础上给容器换一个根文件系统。network 命名空间在第 4 篇，user 命名空间在第 5 篇。

## 3. 创建命名空间的代价

`clone` 系统调用可以在创建子进程的同时创建新的命名空间。`nscost` 对每一种命名空间各创建 200 个子进程（子进程立即退出），测平均耗时：

```c
pid_t pid = clone(child, stack + sizeof stack, kinds[k].flags | SIGCHLD, NULL);
waitpid(pid, NULL, 0);
```

```
无（普通的 fork）    每个     39.8 微秒
UTS（主机名）        每个     36.0 微秒
IPC                  每个     40.1 微秒
PID                  每个     35.3 微秒
mount                每个     42.3 微秒
user                 每个     32.7 微秒
cgroup               每个     32.8 微秒
network              每个    130.2 微秒
全部                 每个    127.1 微秒
```

除了网络，带上一个新的命名空间和普通的 `fork` 几乎没有区别（几微秒的差别在测量的波动之内）。网络命名空间要 130 微秒，因为它要初始化一整套协议栈：回环网卡、路由表、各种 sysctl 和统计计数器。而且它的销毁是异步的，由内核的工作队列在后台完成，没有算在这里面。

所以「容器很轻」是真的：给一个进程准备好全部的命名空间，只要 0.1 毫秒左右。第 6 篇自己写的容器从启动到退出约 10 毫秒，大部分时间花在挂载文件系统和执行程序上。

## 4. 容器中的 1 号进程

在普通的 Linux 系统中，1 号进程是 `systemd`（或 `init`），它有两个特殊的职责，容器中的 1 号进程也继承了它们，但容器中的 1 号进程通常只是你的应用程序，它不知道自己有这些职责。

<div class="dsviz" data-viz="lx-pid1"></div>

### 4.1 回收孤儿进程

《进程：fork、exec、wait》中讲过：父进程先退出时，子进程成为孤儿，被过继给 1 号进程；子进程退出后变成僵尸，要由父进程 `wait` 才能彻底消失。在 PID 命名空间中，孤儿被过继给**这个命名空间的 1 号进程**。

`pid1` 程序模拟两种 1 号进程：`lazy` 只等待自己启动的那个命令（很多应用作为 1 号进程时就是这样），`reap` 像 `tini` 一样回收所有退出的子进程。让命令产生 3 个孤儿：

```bash
unshare --pid --fork --mount-proc ./build/pid1 lazy \
    sh -c 'for i in 1 2 3; do (sleep 0.2 &); done; sleep 1; ps -o pid,ppid,stat,comm'
```

`(sleep 0.2 &)` 在子 shell 中把 `sleep` 放到后台，子 shell 立即退出，`sleep` 就成了孤儿，父进程变成 1 号进程：

```
lazy：
    PID    PPID STAT COMMAND
      1       0 S    pid1
      2       1 S    sh
      4       1 Z    sleep
      6       1 Z    sleep
      8       1 Z    sleep
     10       2 R    ps

reap：
    PID    PPID STAT COMMAND
      1       0 S    pid1
      2       1 S    sh
     10       2 R    ps
pid1：回收了孤儿进程 4
pid1：回收了孤儿进程 6
pid1：回收了孤儿进程 8
```

`lazy` 时，3 个 `sleep` 退出后成了僵尸（`Z`），父进程是 1 号进程，而它从不 `wait` 它们。一个长期运行、不断启动子进程（比如执行 shell 脚本、调用外部命令）的容器，僵尸会越积越多，最后占满进程号的上限（第 3 篇的 `pids.max`），再也无法创建新进程。

### 4.2 信号：没有处理函数就被忽略

内核对 1 号进程有一个保护：**发给 1 号进程的信号，如果它没有为这个信号设置处理函数，就直接忽略**，即使这个信号的默认动作是终止进程。这是为了防止 `init` 被意外杀死，整个系统跟着崩溃。在 PID 命名空间中，这条规则同样适用于命名空间的 1 号进程：命名空间内部发出的信号都受这个保护；从外面（上层命名空间）发来的信号，只有 `SIGKILL` 和 `SIGSTOP` 例外。

实测：在新的 PID 命名空间中运行 `sleep 101`（它是 1 号进程，没有设置任何信号处理函数），从外面发 `SIGTERM`：

```
新命名空间中 1 号进程（sleep 101）在外面的进程号 1948
   1948 S    sleep                 ← 发了 SIGTERM，1 秒后它还活着
SIGKILL 后已退出
```

在命名空间内部，连 `SIGKILL` 也杀不死它：

```
$ unshare --pid --fork --mount-proc sh -c "kill -KILL 1; echo 还活着"
还活着
```

（`sh -c` 就是这个命名空间的 1 号进程，它给自己发 `SIGKILL`，被忽略了。）

这就是 `docker stop` 经常要等 10 秒的原因：它先给容器的 1 号进程发 `SIGTERM`，等 10 秒，还没退出再发 `SIGKILL`。在 Mac 上用 Docker 测试：

```
$ time docker stop $(docker run -d ubuntu:24.04 sleep 1000)
real	10.11

$ time docker stop $(docker run -d --init ubuntu:24.04 sleep 1000)
real	0.06
```

`sleep` 没有处理 `SIGTERM`，作为 1 号进程，信号被忽略，等满 10 秒后被 `SIGKILL`。加上 `--init`，Docker 用一个很小的 init 程序（`tini`）作为 1 号进程，它把 `SIGTERM` 转发给 `sleep`，而 `sleep` 不是 1 号进程，按默认动作立即退出。

`pid1 reap` 也做了同样的事：收到 `SIGTERM` 就转发给命令。从外面发 `SIGTERM`，命令以 143（128 + 15）退出，1 号进程随之退出。

**结论**：容器的 1 号进程要么自己处理 `SIGTERM`、回收子进程，要么交给 `tini`、`dumb-init`（`docker run --init`、Kubernetes 中的共享 PID 命名空间等）。Dockerfile 中 `CMD` 用 shell 形式（`CMD python app.py`）时，1 号进程是 `/bin/sh -c`，它不会把信号转发给 Python，改用 exec 形式（`CMD ["python", "app.py"]`）才能让应用自己成为 1 号进程。

## 5. 常见错误

1. **把容器当成轻量的虚拟机**。容器共享宿主机的内核：内核的漏洞、内核参数、内核模块，对所有的容器都有影响。
2. **`unshare --pid` 不加 `--fork`**。调用者自己不会进入新的 PID 命名空间，直接运行的命令也不会。
3. **新的 PID 命名空间不重新挂载 `/proc`**。`ps` 看到的仍然是外面所有的进程。
4. **应用作为 1 号进程却不处理 `SIGTERM`**。`docker stop`、Kubernetes 删除 Pod 都要等到超时才强制结束，期间也没有机会做清理。
5. **1 号进程不回收孤儿进程**。僵尸越积越多。

## 6. 小结

- 容器是带有一组命名空间和 cgroup 的普通进程，共享宿主机的内核；
- 8 种命名空间分别隔离挂载点、主机名、IPC、进程号、网络、用户、cgroup 视图和时钟；`/proc/<pid>/ns/` 显示一个进程属于哪些命名空间；
- 创建命名空间很便宜：大多数和 fork 一样约 40 微秒，网络命名空间 130 微秒；
- 命名空间的 1 号进程要回收孤儿进程，否则僵尸会积累；没有处理函数的信号会被忽略，`docker stop` 因此等满 10 秒，`--init` 只要 0.06 秒。

## 7. 练习

1. 在一个容器中运行 `ps aux`，发现很多 `<defunct>` 进程。它们是什么？怎么解决？
2. 一个 Dockerfile 的最后一行是 `CMD ./start.sh`，`start.sh` 中最后一行是 `java -jar app.jar`。`docker stop` 时，Java 程序能收到 `SIGTERM` 吗？怎样修改？
3. 为什么 `unshare --pid` 需要配合 `--fork`，而 `unshare --uts` 不需要？

<details>
<summary>点击查看答案</summary>

1. `<defunct>` 就是僵尸进程：已经退出、但父进程还没有 `wait` 的进程。它们的父进程多半是容器的 1 号进程（孤儿被过继给了它），而 1 号进程不回收它们。解决：让一个会回收子进程的 init 作为 1 号进程（`docker run --init`，或者在镜像中安装 `tini` 并作为 `ENTRYPOINT`），或者让应用自己处理 `SIGCHLD` 并 `waitpid(-1, ...)`。

2. 收不到。shell 形式的 `CMD` 让 `/bin/sh -c "./start.sh"` 成为 1 号进程，`start.sh` 又是一个 shell，`java` 是它的子进程。`SIGTERM` 发给 1 号进程（`sh`），`sh` 不会转发，而且作为 1 号进程、没有处理函数，信号被忽略，10 秒后整个容器被 `SIGKILL`。修改：脚本最后一行改为 `exec java -jar app.jar`，让 Java 进程替换 shell；`CMD` 改为 exec 形式 `CMD ["./start.sh"]`；或者使用 `--init`。

3. 一个进程的进程号在创建时就确定了，不能改变，所以 `unshare(CLONE_NEWPID)` 不会把调用者放进新的 PID 命名空间，只决定它**之后创建的子进程**在哪个 PID 命名空间中，第一个子进程成为那里的 1 号进程，所以需要 `--fork`。而主机名等是可以随时切换的属性，`unshare(CLONE_NEWUTS)` 立即对调用者自己生效。

</details>
