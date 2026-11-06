---
title: 容器的安全边界：capabilities、seccomp、AppArmor 与 user 命名空间
date: 2026-11-12 10:40:00
updated: 2026-11-12 10:40:00
categories:
  - Linux基础
  - 容器原理
tags:
  - Linux
  - 容器
  - 安全
order: 5
description: Docker 容器中默认是 root，而且就是宿主机的 root（没有隔离用户）。是什么挡住了它？本文在容器中逐项尝试添加网卡、挂载文件系统、创建命名空间：默认配置下全部失败，挡住它们的是三层机制：capabilities 只留下了 14 个，seccomp 过滤系统调用，AppArmor 限制挂载。实测加上 SYS_ADMIN 也挂载不了，还要同时关掉 AppArmor 才行。再看 user 命名空间怎样让容器中的 root 在外面只是一个普通用户。最后用 --privileged 的容器直接挂载宿主机的磁盘，读到了宿主机的主机名和 Docker 的数据目录：特权容器等于没有边界。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 Linux 的 capabilities，会查看和解读一个进程的能力集；知道 Docker 默认保留哪些能力、`--cap-add`/`--cap-drop` 的作用；理解 seccomp 和 AppArmor 在容器安全中的角色；理解 user 命名空间和 uid 映射；知道 `--privileged` 放开了什么，为什么不要随便使用。
>
> **前置知识**：本章第 1、2 篇；本专栏的《用户、权限与软件包》（root、文件权限）；《strace 与 ltrace》中提到的 seccomp。
>
> **实验环境**：同第 1 篇。用 `docker run --rm` 在不同的选项下启动容器（镜像中装了 `capsh`、`iproute2` 等工具），逐项测试；user 命名空间和特权容器的实验在实验用的特权容器中进行。

## 1. root 被拆成了能力

传统的 Unix 只有两种用户：root 什么都能做，普通用户受权限检查。Linux 把 root 的特权拆成了 40 多个 **capabilities**（能力），比如：

| 能力 | 允许做什么 |
|---|---|
| `CAP_CHOWN` | 修改文件的所有者 |
| `CAP_NET_BIND_SERVICE` | 绑定 1024 以下的端口 |
| `CAP_NET_RAW` | 使用原始套接字（`ping`） |
| `CAP_NET_ADMIN` | 配置网卡、路由、防火墙 |
| `CAP_SYS_ADMIN` | 挂载文件系统、创建命名空间等一大堆管理操作 |
| `CAP_SYS_MODULE` | 加载内核模块 |
| `CAP_SYS_TIME` | 修改系统时钟 |
| `CAP_SYS_PTRACE` | 跟踪任意进程 |

内核检查的是进程**是否有某个能力**，而不是「是不是 root」。一个进程的能力集在 `/proc/<pid>/status` 中，`CapEff` 是当前生效的能力，`capsh --decode` 可以把它翻译成名字。

## 2. Docker 默认的容器

在一个默认的容器中：

```
id: uid=0(root) gid=0(root) groups=0(root)
CapEff:	00000000a80425fb
Seccomp:	2
0x00000000a80425fb=cap_chown,cap_dac_override,cap_fowner,cap_fsetid,cap_kill,cap_setgid,cap_setuid,cap_setpcap,
    cap_net_bind_service,cap_net_raw,cap_sys_chroot,cap_mknod,cap_audit_write,cap_setfcap
```

是 root，但只有 **14 个**能力：修改文件属性、切换用户、绑定低端口、ping、chroot、创建设备文件等。`CAP_SYS_ADMIN`、`CAP_NET_ADMIN`、`CAP_SYS_MODULE`、`CAP_SYS_TIME` 都没有。`Seccomp: 2` 表示进程上装了 seccomp 过滤器。逐项测试：

| 操作 | 默认 | `--cap-drop ALL` | `--cap-add NET_ADMIN` | `--cap-add SYS_ADMIN` | `SYS_ADMIN` + `apparmor=unconfined` |
|---|---|---|---|---|---|
| `ip link add d0 type dummy` | 失败 | 失败 | **成功** | 失败 | — |
| `mount -t tmpfs none /mnt` | 失败 | 失败 | 失败 | 失败 | **成功** |
| `unshare -m true` | 失败 | 失败 | 失败 | 失败 | **成功** |
| `unshare -U true` | 失败 | 失败 | 失败 | **成功** | — |
| `date -s 2030-01-01` | 失败 | — | — | — | — |
| `chown nobody /tmp` | 成功 | **失败** | 成功 | 成功 | — |
| `ping 127.0.0.1` | 成功 | **失败** | 成功 | 成功 | — |

（— 表示没有测。）

- **添加网卡**需要 `CAP_NET_ADMIN`，`--cap-add NET_ADMIN` 以后就可以了；
- **`--cap-drop ALL`** 去掉了所有能力，连 `chown` 和 `ping` 都不行了：这时的 root 和普通用户差不多。很多服务其实一个能力都不需要，最安全的做法是 `--cap-drop ALL`，再按需加回来；
- **创建 user 命名空间**（`unshare -U`）在默认配置下失败，加上 `SYS_ADMIN` 就成功了，`--security-opt seccomp=unconfined`（不加能力）也能成功。挡住它的是 **seccomp**：Docker 默认的 seccomp 配置禁止了 `unshare`、`clone` 的创建命名空间的标志，除非容器有 `CAP_SYS_ADMIN`；
- **挂载**就更严了：加上 `SYS_ADMIN`、甚至同时关掉 seccomp，`mount` 和 `unshare -m` 仍然失败，错误信息也变了（`cannot mount none read-only`、`Permission denied`）。挡住它的是第三层：**AppArmor**。

## 3. 三层防护

在容器中看自己的 AppArmor 配置：

```
$ cat /proc/self/attr/current
docker-default (enforce)
```

Docker 默认给每个容器加载 `docker-default` 这个 AppArmor 配置，它禁止挂载、禁止写 `/proc` 和 `/sys` 中的敏感文件等。加上 `--security-opt apparmor=unconfined` 和 `--cap-add SYS_ADMIN` 以后，`mount` 和 `unshare -m` 才成功。

所以一个默认的 Docker 容器有三层互相独立的防护：

<figure class="diagram">
<svg viewBox="0 0 700 220" role="img" aria-label="默认的 Docker 容器有三层互相独立的防护，一个操作要依次通过 capabilities、seccomp、AppArmor 才能到达内核；实测 mount 要同时加上 SYS_ADMIN 并关掉 AppArmor 才能成功">
  <defs><marker id="lay-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="lay-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="70" width="120" height="50" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="80.0" y="91.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">容器中的 root</text><text x="80.0" y="106.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">uid 0</text><rect x="180" y="60" width="130" height="70" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="245.0" y="91.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">capabilities</text><text x="245.0" y="106.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">只留 14 个</text><line x1="142" y1="95" x2="178" y2="95" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lay-a)"/><rect x="330" y="60" width="130" height="70" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="395.0" y="91.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">seccomp</text><text x="395.0" y="106.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">禁止 40 多个系统调用</text><line x1="312" y1="95" x2="328" y2="95" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lay-a)"/><rect x="480" y="60" width="130" height="70" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="545.0" y="91.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">AppArmor</text><text x="545.0" y="106.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">docker-default</text><line x1="462" y1="95" x2="478" y2="95" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lay-a)"/><rect x="630" y="60" width="60" height="70" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="660.0" y="99.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">内核</text><line x1="612" y1="95" x2="628" y2="95" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lay-a)"/><text x="245.0" y="160.0" text-anchor="middle" style="fill:var(--danger);font-size:11px">ip link add：缺 NET_ADMIN</text><text x="395.0" y="160.0" text-anchor="middle" style="fill:var(--danger);font-size:11px">unshare -U：被过滤</text><text x="545.0" y="160.0" text-anchor="middle" style="fill:var(--danger);font-size:11px">mount：被拒绝</text><text x="20.0" y="200.0" text-anchor="start" style="fill:var(--muted);font-size:11px">--privileged：三层全部去掉，还能访问宿主机的所有设备</text>
  </g>
</svg>
</figure>

| 机制 | 管什么 | Docker 的默认 |
|---|---|---|
| capabilities | 进程有哪些特权 | 只保留 14 个 |
| seccomp | 能用哪些系统调用、用什么参数 | 禁止约 40 多个危险的系统调用（`mount`、`reboot`、`kexec_load`、`unshare` 等，部分在有对应能力时放开） |
| AppArmor（或 SELinux） | 能访问哪些文件、能做哪些操作 | `docker-default`：禁止挂载等 |

任何一层挡住了，操作就失败。这也说明为什么排查「容器中某个操作没有权限」时，要逐层检查：先看能力（`CapEff`），再看 seccomp（`Seccomp` 和 `dmesg` 中的审计日志），再看 AppArmor（`dmesg` 中的 `apparmor="DENIED"`）。

## 4. user 命名空间：容器中的 root 不是 root

前面这些都是在「容器中的 root 就是宿主机的 root」这个前提下，尽量收回它的特权。第 1 篇看到，Docker 默认的容器和宿主机在同一个 user 命名空间中。user 命名空间从根本上改变了这一点：

- 每个 user 命名空间有自己的一套用户号，通过 **uid 映射**（`/proc/<pid>/uid_map`）对应到外面的用户号；
- 在 user 命名空间中，进程可以是 uid 0，拥有全部能力，但这些能力**只对这个命名空间拥有的资源有效**（它自己创建的网络命名空间、mount 命名空间等）；
- 对外面的文件，它的身份是映射后的那个普通用户。

实验：普通用户 alice（uid 1001）创建一个 user 命名空间，由 root 把命名空间中的 0 映射到外面的 1001（这正是 `newuidmap` 这类工具做的事）：

```
uid=0(root) gid=0(root) groups=0(root)          ← 在命名空间中是 root
         0       1001          1                ← uid_map：里面的 0 对应外面的 1001，共 1 个
-rw-rw-r-- 1 0 0 0 Nov  6 02:30 made-in-ns      ← 在里面看，自己创建的文件属于 root
-rw-r----- 1 nobody nogroup 529 Nov  6 02:28 /etc/shadow
head: cannot open '/etc/shadow' for reading: Permission denied
```

在外面看同一个文件：

```
-rw-rw-r-- 1 1001 1001 0 Nov  6 02:30 made-in-ns
```

里面是 root，创建的文件在外面属于 1001；外面 root 的文件（`/etc/shadow`）在里面显示为 `nobody`（没有映射的用户），读不了。即使容器被攻破、攻击者以「root」的身份逃出了容器，在宿主机上也只是 uid 1001。Docker 的 `--userns-remap`、Podman 的 rootless 模式、Kubernetes 的 user namespace 支持都基于这个机制。

一个插曲：最初让 alice 自己写 uid 映射，失败了：

```
unshare: write failed /proc/self/uid_map: Operation not permitted
```

Ubuntu 24.04 默认开启了 `kernel.apparmor_restrict_unprivileged_userns = 1`：普通用户创建的 user 命名空间会被放进一个受限的 AppArmor 配置（上面的输出中，命名空间中的进程的配置是 `unprivileged_userns (enforce)`），拿不到命名空间中的能力。原因是近几年很多内核漏洞都要先创建一个 user 命名空间、在里面获得「root」才能利用，发行版因此收紧了普通用户的这个能力。需要的程序（Podman、浏览器的沙箱）要有专门的 AppArmor 配置。

## 5. --privileged：没有边界

实验中一直使用的 `--privileged` 容器，放开了上面的所有限制：全部能力、没有 seccomp、没有 AppArmor，还能访问宿主机的所有设备。在里面：

```
$ lsblk
NAME     SIZE MOUNTPOINTS
vda       20G
|-vda1    19G
...
$ mount /dev/vda1 /mnt/host
$ cat /mnt/host/etc/hostname
colima
$ ls /mnt/host/var/lib/docker
buildkit  containers  engine-id  image  network ...
```

直接挂载了宿主机（运行 Docker 的虚拟机）的根磁盘，读到了它的主机名，看到了 Docker 所有容器和镜像的数据。可以修改宿主机的任何文件、加载内核模块、读写内存设备。**特权容器和宿主机之间没有安全边界**，只应该在完全信任的场景（比如本章这样的实验、系统管理工具）中使用。

需要某个特定能力时，用 `--cap-add` 只加那一个；需要某个设备，用 `--device` 只给那一个。

## 6. 常见错误

1. **以为容器中的 root 受到了「容器」的保护**。默认没有 user 命名空间，它就是宿主机的 root，只是能力被收回了大半。
2. **遇到权限问题就加 `--privileged`**。应该找出缺的是哪个能力，或者被哪一层挡住了。
3. **只看 capabilities**。加了 `SYS_ADMIN` 仍然不能挂载，是 AppArmor 挡住的。
4. **以 root 运行不需要特权的服务**。在镜像中创建普通用户，`USER` 指令切换过去，再加上 `--cap-drop ALL`。
5. **把 Docker 的套接字（`/var/run/docker.sock`）挂进容器**。能访问它就能启动特权容器，等于拥有宿主机的 root。

## 7. 小结

- Linux 把 root 的特权拆成了 40 多个能力；Docker 默认的容器是 root，但只保留 14 个能力；
- 默认的容器有三层防护：capabilities、seccomp、AppArmor，任何一层都能挡住一个操作；实测加上 `SYS_ADMIN` 仍然不能挂载，还要关掉 AppArmor；
- `--cap-drop ALL` 让 root 连 `chown`、`ping` 都做不了；
- user 命名空间让容器中的 root 在外面只是普通用户：里面创建的文件在外面属于 uid 1001，外面 root 的文件在里面读不了；
- `--privileged` 的容器可以直接挂载宿主机的磁盘，没有安全边界。

## 8. 练习

1. 一个 Web 服务要监听 80 端口，以普通用户运行。最少需要什么能力？
2. 在容器中运行 `tcpdump` 报错 `Operation not permitted`，应该怎样给权限？为什么不应该用 `--privileged`？
3. 为什么说「把 `/var/run/docker.sock` 挂进容器」相当于给了这个容器宿主机的 root 权限？

<details>
<summary>点击查看答案</summary>

1. 只需要 `CAP_NET_BIND_SERVICE`（绑定 1024 以下的端口）。可以 `--cap-drop ALL --cap-add NET_BIND_SERVICE`，再用 `setcap cap_net_bind_service=+ep` 给程序文件加上这个能力，或者让服务监听 8080 等高端口、由端口映射转到 80。（新的 Linux 也可以用 `net.ipv4.ip_unprivileged_port_start` 降低「特权端口」的范围。）

2. `tcpdump` 要创建原始的抓包套接字，需要 `CAP_NET_RAW`（Docker 默认保留了它，但以普通用户运行时没有）和 `CAP_NET_ADMIN`（设置混杂模式等）。用 `--cap-add NET_ADMIN --cap-add NET_RAW` 就够了。`--privileged` 会放开全部能力、seccomp、AppArmor 和所有设备，容器可以挂载宿主机的磁盘，风险远远超过需要。

3. Docker 守护进程以 root 运行，能访问它的套接字就能让它做任何事，比如启动一个 `--privileged`、挂载了宿主机根目录（`-v /:/host`）的新容器，然后在里面修改宿主机的任何文件。所以 docker.sock 的访问权限等同于宿主机的 root 权限。

</details>
