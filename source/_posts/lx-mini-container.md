---
title: 实战：两百行 C 写一个容器
date: 2026-11-12 10:50:00
updated: 2026-11-12 10:50:00
categories:
  - Linux基础
  - 容器原理
tags:
  - Linux
  - 容器
order: 6
description: 把本章的内容组合起来：clone 出带有 5 种新命名空间的子进程，放进限制了内存、进程数和 CPU 的 cgroup，用 overlayfs 叠在 Ubuntu 的根文件系统上，pivot_root 换根，挂载 /proc、/sys 和一个最小的 /dev，去掉危险的能力，最后作为容器的 1 号进程启动命令、回收子进程。一共 200 多行 C。实测：主机名、进程号、网卡、cgroup 都被隔离；能力和 Docker 默认的完全相同；200 MB 的内存占用被 OOM 杀掉；第 18 次 fork 失败；CPU 限制为 50% 后，同样的循环从 1.08 秒变成 2.20 秒；chroot 逃逸失败；从启动到退出约 10 毫秒。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：把命名空间、cgroup、pivot_root、overlayfs、capabilities、1 号进程这些零件组装成一个能用的容器运行时；理解每一步的顺序为什么是这样；知道真正的容器运行时（runc）还多做了哪些事。
>
> **前置知识**：本章第 1～5 篇。
>
> **实验环境**：同第 1 篇。根文件系统是第 2 篇导出的 `/data/ubuntu`。完整的代码是 [minictr.c](https://github.com/Lucakes-bw/Lucakes-bw.github.io/tree/main/projects/lx-container/minictr.c)。

## 1. 要做的事

```
minictr [-h 主机名] [-m 内存MB] [-p 最多进程数] [-c CPU百分比] 根文件系统目录 命令 [参数...]
```

<figure class="diagram">
<svg viewBox="0 0 700 330" role="img" aria-label="minictr 的执行顺序：父进程创建 cgroup、clone 出带新命名空间的子进程、放进 cgroup 后通知它；子进程创建 cgroup 命名空间、设置主机名、挂载 overlayfs 并 pivot_root、去掉能力，最后 fork 出命令并作为 1 号进程回收子进程">
  <defs><marker id="stp-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="stp-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="140.0" y="20.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">父进程（宿主机）</text><text x="520.0" y="20.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">子进程（容器的 1 号进程）</text><rect x="20" y="32" width="240" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="140.0" y="54.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">创建 cgroup，写入限制</text><rect x="20" y="78" width="240" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="140.0" y="100.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">准备 overlayfs 的目录</text><rect x="20" y="124" width="240" height="36" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="140.0" y="146.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">clone（PID、mount、UTS、IPC、net）</text><rect x="20" y="170" width="240" height="36" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="140.0" y="192.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">子进程放进 cgroup.procs</text><rect x="20" y="216" width="240" height="36" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="140.0" y="238.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">写管道：通知继续</text><rect x="20" y="262" width="240" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="140.0" y="284.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">waitpid，清理</text><rect x="400" y="32" width="280" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="540.0" y="54.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">read 管道：等待</text><rect x="400" y="78" width="280" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="540.0" y="100.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">unshare(CLONE_NEWCGROUP)、sethostname</text><rect x="400" y="124" width="280" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="540.0" y="146.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">overlayfs、/dev、pivot_root、卸载旧根</text><rect x="400" y="170" width="280" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="540.0" y="192.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">挂载 /proc、/sys、cgroup2、/tmp</text><rect x="400" y="216" width="280" height="36" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="540.0" y="238.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">去掉能力（只留 14 个）</text><rect x="400" y="262" width="280" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="540.0" y="284.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">fork 命令；wait 所有子进程</text><line x1="262" y1="142" x2="398" y2="50" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#stp-a)"/><text x="330.0" y="86.0" text-anchor="middle" style="fill:var(--link);font-size:11px">clone</text><line x1="262" y1="234" x2="398" y2="60" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#stp-a)"/><text x="330.0" y="176.0" text-anchor="middle" style="fill:var(--link);font-size:11px">管道：可以继续了</text><line x1="398" y1="280" x2="262" y2="280" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#stp-a)"/><text x="330.0" y="273.0" text-anchor="middle" style="fill:var(--link);font-size:11px">命令退出</text><text x="350.0" y="318.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">顺序要点：先进 cgroup 再运行命令；先挂载（需要 SYS_ADMIN）再去掉能力</text>
  </g>
</svg>
</figure>

父进程（在宿主机上）：

1. 创建 cgroup，写入 `memory.max`、`pids.max`、`cpu.max`；
2. 准备 overlayfs 的 upper、work、merged 目录；
3. `clone` 出子进程，带上新的 PID、mount、UTS、IPC、network 命名空间；
4. 把子进程放进 cgroup，**然后**通知它继续；
5. 等子进程退出，清理 cgroup 和 overlayfs 的目录。

子进程（容器中的 1 号进程）：

1. 等父进程的通知；
2. 新的 cgroup 命名空间；设置主机名；
3. 挂载 overlayfs，准备 `/dev`，`pivot_root`，卸载旧根，挂载 `/proc`、`/sys`、`/sys/fs/cgroup`、`/tmp`；
4. 从能力的边界集中去掉危险的能力；
5. fork 出命令，自己留下来做 init：回收子进程、转发信号。

## 2. 父进程：cgroup 和 clone

```c
// 1. cgroup
snprintf(cg, sizeof cg, "/sys/fs/cgroup/minictr-%d", getpid());
CHECK(mkdir(cg, 0755) == 0);
if (memMB) {
    snprintf(v, sizeof v, "%ld", memMB << 20);
    cgWrite(cg, "memory.max", v);
    cgWrite(cg, "memory.swap.max", "0");
}
if (pids) { ... cgWrite(cg, "pids.max", v); }
if (cpu) {  // 每 100 毫秒最多用 cpu × 1 毫秒，即 cpu% 个 CPU
    snprintf(v, sizeof v, "%ld 100000", cpu * 1000);
    cgWrite(cg, "cpu.max", v);
}

// 3. 带新命名空间的子进程
int fds[2];
CHECK(pipe(fds) == 0);
c.syncFd = fds[0];
int flags = CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWNET | SIGCHLD;
pid_t child = clone(containerMain, stack + sizeof stack, flags, &c);
close(fds[0]);
snprintf(v, sizeof v, "%d", child);
cgWrite(cg, "cgroup.procs", v);   // 在子进程做任何事之前放进 cgroup
CHECK(write(fds[1], "g", 1) == 1);
```

**为什么要用管道同步**：`clone` 返回时子进程已经在运行了。如果它在被放进 cgroup 之前就 fork 出命令，命令就逃过了所有的限制。所以子进程先阻塞在 `read` 上，父进程把它放进 cgroup 以后再写一个字节放行。runc 也有同样的同步步骤。

**为什么 cgroup 命名空间在子进程中创建**：cgroup 命名空间的「根」是创建它的那一刻进程所在的 cgroup。如果在 `clone` 时就创建，那时子进程还在父进程的 cgroup 中，容器会看到上一层；等它被移进 `minictr-xxx` 以后再 `unshare(CLONE_NEWCGROUP)`，容器看到的 cgroup 根就是自己的那一个。

## 3. 子进程：根文件系统

```c
// 新的 mount 命名空间中的挂载，不要传播回宿主机
CHECK(mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == 0);
// overlayfs：根文件系统作为只读的下层，容器中的修改都写进 upper
snprintf(opts, sizeof opts, "lowerdir=%s,upperdir=%s,workdir=%s", lower, upper, work);
CHECK(mount("overlay", merged, "overlay", 0, opts) == 0);
// 最小的 /dev：tmpfs 上放几个绑定挂载的设备文件
CHECK(mount("tmpfs", dev, "tmpfs", MS_NOSUID, "mode=755") == 0);
for (...) bindDev(merged, devs[i]);     // null、zero、random、urandom、tty

// pivot_root：merged 成为新的根，原来的根挂到 /.oldroot，然后卸载掉
mkdir(old, 0700);
CHECK(syscall(SYS_pivot_root, merged, old) == 0);
CHECK(chdir("/") == 0);
CHECK(umount2("/.oldroot", MNT_DETACH) == 0);
rmdir("/.oldroot");

CHECK(mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) == 0);
CHECK(mount("sysfs", "/sys", "sysfs", MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) == 0);
CHECK(mount("cgroup2", "/sys/fs/cgroup", "cgroup2", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) == 0);
CHECK(mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, NULL) == 0);
```

几个要点：

- **`/dev` 不能直接用宿主机的**，那里有所有的磁盘设备（第 5 篇中特权容器就是这样挂载了宿主机的磁盘）。只放几个无害的设备，用绑定挂载从宿主机借过来；
- 绑定挂载必须在 `pivot_root` **之前**做，之后宿主机的 `/dev` 就看不到了；
- `/proc` 要在新的 PID 命名空间中重新挂载，才能只看到容器中的进程；
- `/sys` 只读挂载，在新的网络命名空间中挂载的 sysfs 只显示这个命名空间的网卡；
- overlayfs 的 upper 放在根文件系统旁边的目录中（`/data/ubuntu.minictr/<pid>`）。最初放在 `/var/lib/minictr`，挂载失败（`Invalid argument`），因为实验的环境本身就是 Docker 容器，`/var/lib` 在 overlayfs 上，而 upperdir 不能是另一个 overlayfs。

## 4. 子进程：能力和 init

```c
// 容器中保留的 capabilities 之外，全部从边界集中去掉（和 Docker 默认保留的那 14 个相同）
static const int keep[] = {CAP_CHOWN, CAP_DAC_OVERRIDE, CAP_FOWNER, CAP_FSETID, CAP_KILL,
                           CAP_SETGID, CAP_SETUID, CAP_SETPCAP, CAP_NET_BIND_SERVICE, CAP_NET_RAW,
                           CAP_SYS_CHROOT, CAP_MKNOD, CAP_AUDIT_WRITE, CAP_SETFCAP};

for (int c = 0; c <= CAP_LAST_CAP; c++)
    if (!inKeep(c)) CHECK(prctl(PR_CAPBSET_DROP, c, 0, 0, 0) == 0);   // inKeep：c 是否在 keep 中（简写）
```

**边界集**（bounding set）是一个进程以后能拥有的能力的上限，只能减少不能增加，而且会被子进程继承。root 执行一个程序时，新程序的能力 = 边界集中的能力。所以去掉边界集中的能力以后，之后 `exec` 的任何程序（即使是 root、即使是 setuid 程序）都拿不回来。顺序很重要：挂载、`pivot_root` 需要 `CAP_SYS_ADMIN`，必须在去掉能力之前完成。

最后，第 1 篇的教训：子进程是容器的 1 号进程，不能直接 `exec` 命令，而是 fork 出命令，自己留下来回收所有的子进程，并把 `SIGTERM`、`SIGINT` 转发给命令：

```c
signal(SIGTERM, forward);
signal(SIGINT, forward);
cmdPid = fork();
if (cmdPid == 0) {
    signal(SIGTERM, SIG_DFL);   // 命令自己恢复默认的信号处理
    signal(SIGINT, SIG_DFL);
    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
    execvp(c->argv[0], c->argv);
    _exit(127);
}
while ((p = wait(&status)) > 0 || errno == EINTR)
    if (p == cmdPid) cmdStatus = status;
```

## 5. 运行起来

```bash
./build/minictr -h demo -m 64 -p 20 -c 50 /data/ubuntu /bin/bash -c "..."
```

在容器中依次执行一些命令：

```
$ hostname
demo
$ head -1 /etc/os-release
PRETTY_NAME="Ubuntu 24.04.5 LTS"
$ ps -ef
UID          PID    PPID  C STIME TTY          TIME CMD
root           1       0  0 02:31 ?        00:00:00 ./build/minictr -h demo -m 64 ...
root           2       1  0 02:31 ?        00:00:00 /bin/bash -c hostname; ...
root           5       2  0 02:31 ?        00:00:00 ps -ef
$ cat /proc/self/cgroup
0::/
$ cat /sys/fs/cgroup/memory.max /sys/fs/cgroup/pids.max /sys/fs/cgroup/cpu.max
67108864
20
50000 100000
$ grep CapEff /proc/self/status
CapEff:	00000000a80425fb
$ mount -t tmpfs none /mnt
mount: /mnt: permission denied.
$ echo hi > /root/hello; ls /root
hello
```

- 主机名、Ubuntu 的根文件系统、进程号从 1 开始、只看到容器中的 3 个进程；
- cgroup 命名空间让它看到自己是 cgroup 的根（`0::/`），限制写在根目录的控制文件中；
- 能力 `a80425fb` 和第 5 篇中 Docker 默认的容器**完全相同**；没有 `CAP_SYS_ADMIN`，挂载失败；
- 网卡（读 `/proc/net/dev`）只有一个 `lo`：新的网络命名空间，还没有接网线。要上网，按第 4 篇的方法建一对 veth 放进来即可（Ubuntu 的基础镜像里没有 `ip` 命令，这里没有加）。

容器退出后，宿主机上的 `/data/ubuntu/root` 仍然是空的：`hello` 写进了 overlayfs 的 upper，随着清理一起删掉了；cgroup 目录也删掉了。

**资源限制**：

```
### 内存 64 MB：head -c 200M /dev/zero | tail > /dev/null
         4 Killed                  | tail > /dev/null
tail 的退出码 137
oom 2
oom_kill 1

### 进程数 20：/forkbomb 100
第 18 次 fork 失败：Resource temporarily unavailable
成功创建了 17 个子进程

### CPU：同一个 100 万次的 shell 循环
-c 0（不限制）: real 1.079 user 1.079
-c 50:         real 2.199 user 1.106
```

- `tail` 要找最后 10 行，而 200 MB 的零字节中没有换行，它只能全部存在内存中，超过 64 MB 时被 OOM killer 杀掉（137 = 128 + 9）；
- 17 个子进程，加上 `forkbomb`、`bash` 和 1 号进程，正好 20 个；第一次用 bash 的后台任务测试时，bash 在 fork 失败几次后自己退出了（退出码 254），所以改用第 3 篇的 `forkbomb`；
- CPU 限制为 50%：消耗的 CPU 时间一样（1.1 秒），实际的时间翻倍。

**逃逸**：把第 2 篇的 `escape` 放进根文件系统运行：

```
逃逸之前：/ 下有 tmp usr var lib opt home escape mnt media bin run sbin
逃逸之后：/ 下有 tmp usr var lib opt home escape mnt media bin run sbin
主机名：
ls: cannot access '/data': No such file or directory
```

容器保留了 `CAP_SYS_CHROOT`（和 Docker 一样），所以 `chroot` 调用都成功了，但旧根已经卸载，无处可逃。

**启动速度**：运行 `/bin/true` 5 次，从启动到退出分别是 10、11、11、8、9 毫秒（包括创建 cgroup、挂载、清理）。作为对照，在 Mac 上执行 `docker run --rm ubuntu:24.04 true` 是 0.10～0.14 秒，其中大部分时间花在 Docker 守护进程、镜像管理以及 Mac 和虚拟机之间的通信上。

## 6. 和真正的容器运行时相比

Docker 的底层是 containerd 加 runc。runc 做的事情和 `minictr` 基本是同一套，只是更完整：

- **seccomp**：加载 Docker 的默认配置，禁止 40 多个危险的系统调用（`minictr` 没有做）；
- **AppArmor / SELinux**：加载 `docker-default` 配置；
- **只读和屏蔽的路径**：`/proc/sys`、`/proc/kcore`、`/sys/firmware` 等挂成只读或用空文件覆盖，防止容器读写内核的敏感接口；
- **网络**：由 Docker 或 CNI 插件创建 veth、连接网桥、分配 IP、配置 NAT；
- **user 命名空间**（可选）、`no_new_privs`、资源限制的更多项（IO、hugetlb）；
- **镜像**：把多层镜像解压、按顺序叠成 overlayfs 的多个下层；
- 遵循 OCI 运行时规范：一个 `config.json` 描述以上所有的配置。

`minictr` 足以说明：**容器不是什么魔法，是内核的几个特性组合在一起**。理解了每一个零件，遇到问题时就知道该去哪里找：进程号和信号的问题看 PID 命名空间和 1 号进程，文件的问题看 overlayfs 和挂载，资源的问题看 cgroup，权限的问题看能力、seccomp 和 AppArmor，网络的问题看 veth、网桥和 NAT。

## 7. 常见错误

1. **先 clone 再放进 cgroup，却不同步**。子进程可能在被限制之前就启动了命令。
2. **在去掉能力之后才挂载文件系统**。挂载需要 `CAP_SYS_ADMIN`，顺序要反过来。
3. **把宿主机的 `/dev` 整个挂进容器**。等于给了容器所有的磁盘设备。
4. **只 `pivot_root` 不卸载旧根**。宿主机的文件仍然可以通过 `/.oldroot` 访问。
5. **容器的 1 号进程直接 `exec` 应用**。僵尸进程和信号的问题（第 1 篇）。

## 8. 小结

- 一个容器运行时的核心：cgroup 限制资源、clone 创建命名空间、overlayfs + pivot_root 换根、挂载 `/proc` 等、去掉能力、1 号进程做 init；200 多行 C 就能实现；
- 父子进程用管道同步，保证命令启动之前已经在 cgroup 中；cgroup 命名空间要在进入 cgroup 之后再创建；
- 实测：隔离了主机名、进程、网卡和 cgroup 视图，能力和 Docker 默认相同；内存超限被 OOM 杀掉；第 18 次 fork 失败；CPU 50% 时同样的计算用时翻倍；逃逸失败；启动到退出约 10 毫秒；
- runc 在此基础上还有 seccomp、AppArmor、屏蔽的路径、网络、OCI 规范等。

## 9. 练习

1. 给 `minictr` 加上网络：在宿主机上创建一对 veth，一端放进容器的网络命名空间，接到第 4 篇的网桥上。这一步应该由父进程还是子进程来做？在什么时候做？
2. 如果把 `dropCaps()` 移到 `setupRoot()` 之前，会发生什么？
3. `minictr` 的容器中，`/proc/sys/kernel/hostname` 可以写吗？`/proc/sysrq-trigger` 呢？这说明了什么问题？

<details>
<summary>点击查看答案</summary>

1. 由父进程来做：创建 veth、把一端移进容器的网络命名空间（`ip link set eth0 netns <子进程的进程号>`）、把另一端接到网桥上，都需要在宿主机的网络命名空间中操作，而子进程已经在新的网络命名空间中了。时机是 `clone` 之后、通知子进程继续之前（和放进 cgroup 一起）；容器内部的配置（给 eth0 设地址、启用、加默认路由）可以由子进程在启动命令之前完成，或者由父进程用 `ip -n` / `nsenter` 进入它的命名空间来做。

2. `mount`、`pivot_root`、`umount2`、`sethostname` 都需要 `CAP_SYS_ADMIN`，去掉以后这些调用全部失败，`CHECK` 报错退出。所以必须先完成所有需要特权的准备工作，最后再去掉能力，然后执行用户的命令。

3. `/proc/sys/kernel/hostname` 是按 UTS 命名空间隔离的，写它只改容器自己的主机名。但 `/proc` 和 `/sys` 下有很多接口是全局的，比如 `/proc/sysrq-trigger`（向内核发送「立即重启」等命令）、`/proc/sys` 下的大部分内核参数。`minictr` 没有 user 命名空间，容器中的 root 就是宿主机的 root，这些文件能不能写，只取决于各个接口自己的权限检查，有的检查能力，有的只看文件权限。runc 不依赖这些检查，而是直接把 `/proc/sys`、`/proc/sysrq-trigger` 等挂成只读，或者用空文件、空目录覆盖掉。`minictr` 没有做这一步，这是它和真正的运行时之间的安全差距之一。（为了不让实验的虚拟机重启，本文没有去实际测试写这个文件。）

</details>
