# Linux 容器原理：实验程序

《Linux 基础》专栏「容器原理」一章的实验程序，C11。文章中的输出都来自这些程序：Docker 中的 Ubuntu 24.04 容器（以 `--privileged --cgroupns=private` 运行），内核 6.8，GCC 13.3，util-linux 2.39；虚拟机 2 核、2 GB 内存。根文件系统用 `docker export` 从 `ubuntu:24.04` 导出到 `/data/ubuntu`。

```bash
make
sudo ./build/minictr -h demo -m 64 -p 20 /data/ubuntu /bin/bash
```

大部分程序需要 root；cgroup 的实验需要 cgroup v2，并且要先在当前的 cgroup 中启用控制器（见第 3 篇）。

| 程序 | 文章 | 内容 |
|---|---|---|
| `nscost 次数` | 命名空间 | 创建各种命名空间的耗时 |
| `pid1 lazy\|reap 命令...` | 命名空间 | 1 号进程是否回收孤儿进程、转发信号 |
| `escape` | 根文件系统 | 从 chroot 中逃逸 |
| `touch1 文件` | 根文件系统 | overlayfs 的 copy-up 耗时 |
| `cgcpu 线程数 请求的CPU毫秒数 秒数` | cgroup | CPU 配额对延迟的影响 |
| `forkbomb 最多个数` | cgroup | pids.max |
| `pingpong server\|client` | 容器网络 | 不同路径的 TCP 往返时间 |
| `minictr` | 实战 | 两百多行的迷你容器 |

`scripts/bridge.sh up|down`：第 4 篇的手工容器网络（网桥、veth、MASQUERADE、DNAT）。
