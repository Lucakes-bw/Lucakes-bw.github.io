---
title: 根文件系统：chroot、pivot_root 与 overlayfs
date: 2026-11-12 10:10:00
updated: 2026-11-12 10:10:00
categories:
  - Linux基础
  - 容器原理
tags:
  - Linux
  - 容器
  - 文件系统
order: 2
description: 容器里看到的 / 是镜像中的文件，而不是宿主机的根目录。最早的做法是 chroot，但它只改了「根在哪里」，一个十几行的程序就能逃出来，实测逃逸后看到了外面的 /data；容器用的是 pivot_root：换掉根以后把旧的根卸载掉，同一个程序就逃不出去了。镜像又是怎样做到几十个容器共用一份、各自修改互不影响的？答案是 overlayfs：只读的下层加上可写的上层。实测它的代价：第一次修改一个 500 MB 文件的 1 个字节，要先把整个文件复制到上层，open 用了 300 毫秒；删除下层的文件，上层留下一个「白化」的字符设备。
top_group_list: true
disableNunjucks: true
viz: [lx-ctr]
---

> **本文目标**：理解进程的根目录是什么；会用 `chroot`，知道它为什么不是安全边界；理解 `pivot_root` 和 mount 命名空间怎样彻底换掉根文件系统；理解 overlayfs 的下层、上层、合并视图，以及写时复制（copy-up）和白化文件（whiteout）；知道为什么容器中不要修改镜像里的大文件。
>
> **前置知识**：本章第 1 篇（mount 命名空间）；本专栏的《文件与目录操作》（挂载、目录结构）。
>
> **实验环境**：同第 1 篇。用 `docker export` 把 `ubuntu:24.04` 镜像导出到 `/data/ubuntu`（106 MB）作为容器的根文件系统；`/data` 是 ext4。

## 1. 进程的根目录

每个进程除了当前目录，还有一个**根目录**：路径以 `/` 开头时从这里开始找，`..` 走到这里就停住。平时所有进程的根目录都是同一个 `/`。容器要让进程看到的 `/` 是镜像中的文件，就要改变它的根目录。

## 2. chroot 和它的漏洞

`chroot` 是 Unix 上最早的办法，它把调用者的根目录改成指定的目录：

```
$ chroot /data/ubuntu /bin/sh -c 'ls /'
bin boot dev etc home lib media mnt opt proc root run sbin srv sys tmp usr var
```

看到的是 Ubuntu 镜像的根目录。但 `chroot` **只改了根目录，没有改当前目录**，而且它是一个可以反复调用的系统调用。经典的逃逸方法：

```c
mkdir("/.escape", 0700);
chroot("/.escape");       // 新的根是 /.escape，但当前目录还是原来的 /，在新根的「外面」
for (int i = 0; i < 64; i++)
    if (chdir("..") != 0) break;   // 当前目录不在根之下，.. 不会被拦住，一直走到真正的根
chroot(".");              // 把真正的根设为根
```

`..` 只有在**到达根目录时**才会停住。第二次 `chroot` 以后，根变成了 `/.escape`，而当前目录还在它的上面，所以一直 `cd ..` 就能走出原来的 chroot，走到真正的根。在 chroot 中运行 `escape`：

```
逃逸之前：/ 下有 tmp usr var lib opt home escape mnt media bin run sbin
逃逸之后：/ 下有 tmp usr var lib opt home mnt media bin run sbin boot
主机名：e66c8d76e806
/data 下有：ubuntu
```

逃出来以后，看到了 chroot 外面的 `/data`（里面就是被当作根的 `ubuntu` 目录）和外面的主机名。前提是进程有 `CAP_SYS_CHROOT` 能力（root 默认都有）；去掉这个能力，第二次 `chroot` 就会失败：

```
$ capsh --chroot=/data/ubuntu --drop=cap_sys_chroot --shell=/escape --
逃逸之前：/ 下有 ...
escape.c:26: 检查失败：chroot("/.escape") == 0（Operation not permitted）
```

`chroot` 本来就不是为了安全隔离而设计的（它用来构建软件、修复系统），**不要把它当作安全边界**。

## 3. pivot_root：换掉整个根

容器用的是 `pivot_root`：在新的 mount 命名空间中，把一个目录变成新的根挂载点，原来的根挂到它下面的一个子目录，然后**把原来的根卸载掉**。卸载以后，原来的文件系统在这个命名空间中根本不存在了，`..` 走到哪里都出不去。

```bash
unshare -m sh -c "
    mount --make-rprivate /             # 之后的挂载不要传播回外面
    mount --bind /data/ubuntu /data/ubuntu   # pivot_root 要求新根是一个挂载点
    cd /data/ubuntu
    mkdir -p .old
    pivot_root . .old                   # 新根是当前目录，旧根挂到 /.old
    cd /
    mount -t proc proc /proc
    umount -l /.old && rmdir /.old      # 卸载旧根
    exec /escape"
```

```
旧的根已卸载
逃逸之前：/ 下有 tmp usr var lib opt home escape mnt media bin run sbin
逃逸之后：/ 下有 tmp usr var lib opt home escape mnt media bin run sbin
主机名：
/data 下有：
ls: cannot access '/data': No such file or directory
```

同一个程序，`chroot` 和 `chdir("..")` 都成功了，但「逃逸之后」看到的还是同一个根（主机名为空，因为这是镜像中的 `/etc/hostname`）：外面的文件系统已经被卸载，没有地方可逃。

几个细节：

- `mount --make-rprivate /`：mount 命名空间之间的挂载默认可能是**共享传播**的（systemd 把 `/` 设成了 shared），在里面的挂载、卸载会传播到外面。容器运行时都会先把它改成 private；
- `pivot_root` 要求新根是一个挂载点，所以先把目录绑定挂载到它自己；
- 卸载旧根之前要挂载 `/proc`，否则 `umount` 命令找不到挂载表；第 6 篇的 C 程序直接调用 `umount2(..., MNT_DETACH)` 就没有这个问题。

## 4. overlayfs：镜像的分层

一台机器上运行 50 个基于同一个镜像的容器，不能把镜像复制 50 份；每个容器又要能随意修改自己的文件，互不影响。Docker 的做法是 **overlayfs**：

- **下层**（lowerdir）：镜像的各层，只读，所有容器共用；
- **上层**（upperdir）：每个容器自己的可写层；
- **合并视图**（merged）：容器看到的根目录，上层有的文件用上层的，没有的透过上层看下层。

<div class="dsviz" data-viz="lx-overlay"></div>

实验：下层放一个 500 MB 的文件和两个小文件，挂载：

```bash
mount -t overlay overlay -o lowerdir=ovl/lower,upperdir=ovl/upper,workdir=ovl/work ovl/merged
```

（`workdir` 是 overlayfs 内部使用的临时目录，必须和 upperdir 在同一个文件系统上。）

**读**：读整个 500 MB 的文件，上层仍然只有 4 KB（空目录），读不产生任何复制。

**第一次写**：`touch1` 以读写方式打开文件，写 1 个字节：

```
$ ./build/touch1 ovl/merged/big.bin
open 300.1 ms，write 0.012 ms
$ du -sh ovl/upper
501M	ovl/upper
```

`open` 用了 300 毫秒，上层多了 501 MB。以可写方式打开下层的文件时，overlayfs 要先把**整个文件**复制到上层（**copy-up**），之后的修改都在副本上进行。为了 1 个字节，复制了 500 MB。这台虚拟机的磁盘很快（宿主机有缓存），在普通的云盘上（100 MB/s）要 5 秒以上，期间 `open` 一直阻塞。

**再写一次**：

```
open 0.0 ms，write 0.005 ms
```

文件已经在上层了，和普通的文件一样快。小文件（6 字节）的 copy-up 只要 0.4 毫秒。

**删除**下层的 `old.txt`：

```
$ rm ovl/merged/old.txt
$ ls -l ovl/upper | grep old
c--------- 2 root root 0, 0 Nov  6 02:23 old.txt
$ ls ovl/lower
big.bin  old.txt  small.txt
```

下层是只读的，文件还在。overlayfs 在上层创建了一个设备号为 0, 0 的字符设备，叫**白化文件**（whiteout），表示「这个名字被删除了」，合并视图中就看不到它。所以在容器中删除镜像里的文件，**不会**减少镜像占用的空间；在 Dockerfile 中，前一层加入、后一层删除的文件，仍然占着镜像的大小。

**这些知识的实际意义**：

- 容器中不要修改镜像中的大文件（数据库文件、大的日志），它们应该放在**卷**（volume，绑定挂载的宿主机目录，不经过 overlayfs）中；
- 容器的可写层随容器删除而消失，要保留的数据也要放在卷中；
- upperdir 不能在另一个 overlayfs 上：第 6 篇的程序最初把 upperdir 放在 Docker 容器的 `/var/lib` 下（它本身就是 overlayfs），挂载失败，错误是 `Invalid argument`。

## 5. 常见错误

1. **把 `chroot` 当作安全隔离**。有 `CAP_SYS_CHROOT` 的进程可以逃出去。
2. **不先把挂载传播改成 private**。容器中的挂载、卸载可能影响宿主机。
3. **在容器中修改镜像里的大文件**。第一次写会把整个文件复制到可写层。
4. **以为在容器中删除文件能节省镜像空间**。删除只是在上层加一个白化文件。
5. **把数据写在容器的可写层中**。容器删除后就没了，而且 overlayfs 的写入比卷慢。

## 6. 小结

- 进程的根目录决定了 `/` 从哪里开始；`chroot` 只改根目录，有 `CAP_SYS_CHROOT` 就能逃出去，实测逃逸后看到了外面的 `/data`；
- 容器在新的 mount 命名空间中用 `pivot_root` 换根，再卸载旧根，同一个逃逸程序就出不去了；
- overlayfs 用只读的下层（镜像）加可写的上层（容器）组成合并视图；
- 写下层的文件会先把整个文件复制到上层：500 MB 的文件写 1 个字节，`open` 用了 300 毫秒，上层多了 501 MB；
- 删除下层的文件在上层留下白化文件，镜像不会变小。

## 7. 练习

1. 一个容器中运行着 MySQL，数据目录在镜像中（没有使用卷）。容器第一次启动时，为什么特别慢？还有什么问题？
2. 下面的 Dockerfile 构建出的镜像比预期大 1 GB，为什么？怎么改？
   ```dockerfile
   RUN curl -o /tmp/big.tar.gz https://example.com/big.tar.gz
   RUN tar xzf /tmp/big.tar.gz -C /opt
   RUN rm /tmp/big.tar.gz
   ```
3. 为什么说 `pivot_root` 之后一定要卸载旧的根？只做 `pivot_root` 不卸载，会怎样？

<details>
<summary>点击查看答案</summary>

1. 数据文件在镜像的只读层中，MySQL 启动时以读写方式打开它们，overlayfs 要把每个文件整个复制到可写层，文件越大越慢。其他问题：之后的写入都要经过 overlayfs；数据在容器的可写层中，删除容器就丢失了；同一个镜像启动多个容器，每个都要复制一份。应该把数据目录放在卷中。

2. 每个 `RUN` 生成一层。第一层包含了下载的 1 GB 压缩包；第三层的 `rm` 只是在上层加了一个白化文件，第一层中的文件仍然在镜像里。改法是在同一个 `RUN` 中下载、解压、删除：`RUN curl -o /tmp/big.tar.gz ... && tar xzf /tmp/big.tar.gz -C /opt && rm /tmp/big.tar.gz`，这样压缩包从来没有被记录在任何一层中（或者使用多阶段构建）。

3. `pivot_root` 只是把旧的根挂到了新根下的一个目录（比如 `/.old`），旧的文件系统仍然可以通过这个目录访问，容器中的进程可以直接 `ls /.old` 看到宿主机的所有文件，相当于没有隔离。卸载旧根以后，它在这个 mount 命名空间中才真正消失。

</details>
