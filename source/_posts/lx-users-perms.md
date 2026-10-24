---
title: 用户、权限与软件包
date: 2026-10-24 09:15:00
updated: 2026-10-24 09:15:00
categories:
  - Linux基础
  - 常用命令
tags:
  - Linux
  - 权限
  - apt
order: 4
description: Linux 是多用户系统，每个文件都有所有者和权限。本文讲解用户和组、rwx 权限对文件和目录的不同含义（配有权限位换算的演示）、chmod、chown、umask、sudo，以及 setuid 和 sticky 位，每一条都在 Ubuntu 中实测；最后介绍用 apt 安装和管理软件。
top_group_list: true
disableNunjucks: true
viz: [lx-perm]
---

> **本文目标**：理解用户、组和 root；读懂 `ls -l` 中的权限；分清读、写、执行权限对文件和目录的含义；会用 `chmod`、`chown`、`umask`、`sudo`；认识 setuid 和 sticky 位；会用 `apt` 管理软件包。
>
> **前置知识**：《文件与目录操作》。
>
> **实验环境**：Ubuntu 24.04，普通用户 `lucky`（可以使用 `sudo`），以及系统自带的用户 `nobody`（用来模拟「其他人」）。

## 1. 用户和组

Linux 是**多用户**系统，每个进程都以某个用户的身份运行，每个文件都属于某个用户和某个组。

```bash
whoami
id
grep -E "^(root|lucky):" /etc/passwd
```

```
lucky
uid=1001(lucky) gid=1001(lucky) groups=1001(lucky)
root:x:0:0:root:/root:/bin/bash
lucky:x:1001:1001::/home/lucky:/bin/bash
```

- 每个用户有一个数字编号 **UID**，每个组有一个 **GID**。系统内部用的是数字，名字只是为了方便人看。
- `/etc/passwd` 每行描述一个用户，用 `:` 分隔：用户名、密码占位符（`x`，真正的密码哈希在 `/etc/shadow` 中）、UID、GID、说明、主目录、登录 Shell。
- **root**（UID 为 0）是超级用户，不受任何权限限制。
- 一个用户可以属于多个组，组用来让多个用户共享文件。

## 2. 读懂权限

```
-rw-r--r-- 1 lucky lucky 6 Oct 24 01:52 notes.txt
```

第一个字符是类型，后面 9 个字符分成三组，分别是**所有者**（u）、**所属组**（g）、**其他人**（o）的权限。每组三位依次是 **r**（读）、**w**（写）、**x**（执行），没有的权限显示为 `-`。

系统检查权限的规则是：如果你是文件的所有者，只看第一组；否则如果你在文件的所属组中，只看第二组；否则看第三组。

### 2.1 数字形式

每个权限位对应一个数字：**r = 4，w = 2，x = 1**，每组的三位相加得到一个 0 到 7 的数字，三组就是三个数字。比如 `rwxr-xr--` 是 7（4+2+1）、5（4+1）、4（4），写作 **754**。

在下面的演示中输入数字或符号形式，查看逐位的对应关系：

<div class="dsviz" data-viz="perm-bits"></div>

最常见的几种：

| 数字 | 符号 | 典型用途 |
|---|---|---|
| 644 | `rw-r--r--` | 普通文件：自己可读写，其他人只读 |
| 600 | `rw-------` | 私密文件：只有自己能读写（比如 SSH 私钥） |
| 755 | `rwxr-xr-x` | 程序、脚本、目录：自己可以修改，其他人可以执行或进入 |
| 700 | `rwx------` | 私密目录 |
| 777 | `rwxrwxrwx` | 所有人都能做任何事，**几乎总是一个错误** |

## 3. 权限对文件和目录的含义

### 3.1 对文件

- **r**：可以读取内容（`cat`）；
- **w**：可以修改内容；
- **x**：可以作为程序**执行**。

```bash
echo "secret" > s.txt
chmod 600 s.txt
ls -l s.txt
chmod u-r s.txt
cat s.txt
```

```
-rw------- 1 lucky lucky 7 Oct 24 01:55 s.txt
cat: s.txt: Permission denied
```

去掉了所有者的读权限之后，即使是文件的主人也读不了。

一个脚本没有执行权限时：

```bash
printf '#!/bin/bash\necho hello from script\n' > hi.sh
./hi.sh
echo "exit=$?"
bash hi.sh
chmod +x hi.sh
./hi.sh
```

```
bash: ./hi.sh: Permission denied
exit=126
hello from script
hello from script
```

`./hi.sh` 失败了（退出码 126 表示「找到了，但不能执行」），但 `bash hi.sh` 可以：这时执行的是 `bash`，`hi.sh` 只是 bash 读取的一个文件，只需要读权限。加上执行权限后，就可以直接运行了。第一行的 `#!/bin/bash` 告诉系统用哪个程序来解释这个脚本。

### 3.2 对目录

目录的权限含义不同，这是最容易混淆的地方：

- **r**：可以**列出**目录中的文件名（`ls`）；
- **w**：可以在目录中**创建、删除、重命名**文件；
- **x**：可以**进入**目录（`cd`），以及**访问**其中的文件。

```bash
mkdir box
echo in > box/inside.txt
chmod 644 box                 # 有 r，没有 x
ls box
cat box/inside.txt
cd box
```

```
inside.txt
cat: box/inside.txt: Permission denied
bash: cd: box: Permission denied
```

有 r 没有 x：能看到文件名，但不能读取其中的文件，也不能进入。反过来：

```bash
chmod 300 box                 # 有 w、x，没有 r
ls box
cat box/inside.txt
```

```
ls: cannot open directory 'box': Permission denied
in
```

有 x 没有 r：不能列出文件名，但**如果知道文件名**，就可以直接访问它。

```bash
chmod 555 box                 # 没有 w
touch box/new
rm box/inside.txt
```

```
touch: cannot touch 'box/new': Permission denied
rm: cannot remove 'box/inside.txt': Permission denied
```

没有 w：不能在目录中创建或删除文件，即使这些文件是自己的。

### 3.3 删除文件需要的是目录的权限

由此可以得出一个让很多人意外的结论：**能不能删除一个文件，取决于它所在目录的写权限，而不是文件本身的权限**：

```bash
sudo chown root:root s.txt
ls -l s.txt
rm -f s.txt
echo "exit=$?"
```

```
-rw-r----- 1 root root 7 Oct 24 01:55 s.txt
exit=0
```

`s.txt` 属于 root，`lucky` 连读都不能读，却可以删除它，因为 `lucky` 对所在的目录有写权限。删除文件只是修改目录（删掉一个目录项，见《文件与目录操作》第 6 节），并不修改文件本身。

## 4. 修改权限和所有者

### 4.1 chmod

```bash
chmod 754 s.txt               # 数字形式
chmod u+r,g+r,o+r s.txt       # 符号形式：给 u、g、o 加上 r
chmod a-x,o-r s.txt           # a 表示所有人（all）
chmod +x hi.sh                # 省略 u/g/o 时，等于 a（受 umask 影响）
chmod -R 755 dir              # -R：递归修改目录中的所有文件
```

符号形式的格式是「谁（u、g、o、a）+ 操作（`+` 加、`-` 去掉、`=` 设为）+ 权限（r、w、x）」，适合在原有权限的基础上做修改；数字形式适合一次设置所有的权限。

### 4.2 chown

```bash
chown root s.txt
sudo chown root:root s.txt
```

```
chown: changing ownership of 's.txt': Operation not permitted
```

**普通用户不能把文件的所有者改成别人**（否则就可以把文件「送给」别人，逃避磁盘配额等限制），需要 root 权限。`chown 用户:组 文件` 同时修改所有者和组。

### 4.3 umask：新文件的默认权限

```bash
umask
touch f
mkdir d
ls -ld f d
```

```
0022
drwxr-xr-x 2 lucky lucky 4096 Oct 24 01:55 d
-rw-r--r-- 1 lucky lucky    0 Oct 24 01:55 f
```

新建文件的权限是 666 去掉 umask，新建目录是 777 去掉 umask。umask 为 022，表示去掉组和其他人的写权限，所以文件是 644，目录是 755。（普通文件默认不会有执行权限，即使 umask 是 0。）

## 5. sudo 和 root

日常应该以普通用户的身份工作，需要管理员权限时，在命令前加 **`sudo`**：

```bash
cat /etc/shadow
sudo head -1 /etc/shadow | cut -c1-20
```

```
cat: /etc/shadow: Permission denied
root:*:20726:0:99999
```

`sudo` 以 root 的身份执行一条命令（通常要输入**自己的**密码，这个实验环境中配置了免密码）。哪些用户可以使用 `sudo`，在 `/etc/sudoers` 中配置。

为什么不直接用 root 登录？因为 root 不受任何限制，一个错误的命令（比如 `rm -rf` 写错了路径）就可能毁掉整个系统。**最小权限原则**：只在必要的时候、为必要的命令使用 root 权限。

## 6. 特殊权限位

```bash
ls -l /usr/bin/passwd
ls -ld /tmp
```

```
-rwsr-xr-x 1 root root 72056 May 30  2024 /usr/bin/passwd
drwxrwxrwt 1 root root 4096 Oct 24 01:52 /tmp
```

- **setuid**（所有者的 x 位置显示为 `s`）：执行这个程序时，进程以**文件所有者**的身份运行。普通用户修改自己的密码需要写 `/etc/shadow`，而这个文件只有 root 能写，所以 `passwd` 被设置为 setuid root。setuid 的程序如果有漏洞，就可能被利用来获得 root 权限，所以这样的程序非常少，而且要格外小心。
- **sticky 位**（其他人的 x 位置显示为 `t`）：用在目录上，表示**只有文件的所有者才能删除或重命名自己的文件**。`/tmp` 所有人都可以写，如果没有 sticky 位，任何人都能删除别人的临时文件。

实测 sticky 位的作用：在一个所有人都可写的目录 `/srv/share` 中，用户 `nobody` 删除 `lucky` 的文件：

```bash
chmod 777 /srv/share
sudo -u nobody rm /srv/share/pub.txt          # 没有 sticky 位：删除成功
echo again > /srv/share/pub.txt
chmod 1777 /srv/share
ls -ld /srv/share
sudo -u nobody rm /srv/share/pub.txt          # 有 sticky 位
```

```
drwxrwxrwt 2 lucky lucky 4096 Oct 24 01:55 /srv/share
rm: cannot remove '/srv/share/pub.txt': Operation not permitted
```

（`sudo -u nobody` 以用户 `nobody` 的身份执行命令。数字形式中，sticky 位是最前面多出的一位 `1`。）

## 7. 软件包管理：apt

Ubuntu 用 **apt** 从软件源下载和安装软件，它会自动处理依赖关系：

```bash
sudo apt update                 # 更新软件源的索引（安装前先执行）
apt show cowsay                 # 查看软件包的信息
sudo apt install cowsay         # 安装
apt list --installed            # 列出已安装的软件包
sudo apt remove cowsay          # 卸载
sudo apt upgrade                # 升级所有已安装的软件包
```

```bash
apt-cache show cowsay | grep -E "^(Version|Installed-Size|Depends)"
```

```
Version: 3.03+dfsg2-8
Installed-Size: 91
Depends: libtext-charwidth-perl, perl:any
```

安装之后运行它：

```bash
/usr/games/cowsay "Hello Linux"
```

```
 _____________
< Hello Linux >
 -------------
        \   ^__^
         \  (oo)\_______
            (__)\       )\/\
                ||----w |
                ||     ||
```

（`cowsay` 被安装在 `/usr/games` 中，有些环境的 `PATH` 中没有这个目录，所以这里写了完整的路径。）

`dpkg -L 包名` 列出一个软件包安装了哪些文件；`dpkg -S 文件` 查找一个文件属于哪个软件包。开发时常用的软件包有 `build-essential`（gcc、make 等）、`gdb`、`git`。

## 8. 常见错误

1. **遇到 Permission denied 就 `chmod 777`**：所有人都能修改甚至替换你的文件，是严重的安全问题。应该搞清楚是谁需要什么权限。
2. **以为目录的 r 权限就能访问其中的文件**：访问需要 x。
3. **以为文件只读就不会被删除**：删除取决于目录的 w。
4. **习惯性地使用 root 或者到处加 `sudo`**：一个错误就可能破坏系统；`sudo` 创建的文件属于 root，之后普通用户无法修改。
5. **`chmod -R` 时给所有文件都加上了 x**：普通文件不需要执行权限。可以用 `chmod -R u=rwX,go=rX dir`，大写的 `X` 只对目录和已经可执行的文件加 x。
6. **安装软件前没有 `apt update`**：软件源的索引过期，找不到包或下载失败。

## 9. 小结

- 每个文件有所有者、所属组和 9 个权限位；r = 4，w = 2，x = 1，常用 644、600、755、700
- 对文件，rwx 是读、写、执行；对目录，r 是列出、w 是创建删除、x 是进入和访问；删除文件看的是目录的 w
- `chmod` 修改权限，`chown`（需要 root）修改所有者，`umask` 决定新文件的默认权限
- `sudo` 以 root 身份执行单条命令，遵循最小权限原则
- setuid 让程序以文件所有者的身份运行，sticky 位让共享目录中只有所有者能删除自己的文件
- `apt update`、`apt install`、`apt remove` 管理软件包

## 10. 练习

1. `rwxr-x---` 对应的数字是多少？`chmod 640` 之后，符号形式是什么？
2. 用户 A 有一个目录，权限是 `rwxrwxrwx`，用户 B 能删除其中属于 A 的文件吗？如果目录的权限是 `rwxrwxrwt` 呢？
3. 如果 umask 是 027，新建的文件和目录的权限分别是什么？

<details>
<summary>点击查看答案</summary>

1. `rwxr-x---` 是 750；640 是 `rw-r-----`。可以在本文的演示中验证。

2. 权限是 777 时可以：B 对目录有写权限，能删除其中的任何文件，不管文件属于谁。加上 sticky 位（1777，显示为 `rwxrwxrwt`）之后不能，只有文件的所有者、目录的所有者和 root 能删除。第 6 节实测了这两种情况。

3. 文件是 666 去掉 027，得到 640（`rw-r-----`）；目录是 777 去掉 027，得到 750（`rwxr-x---`）。实测：

   ```
   drwxr-x--- 2 lucky lucky 4096 Oct 24 01:55 nd
   -rw-r----- 1 lucky lucky    0 Oct 24 01:55 nf
   ```

   这样的设置让其他人完全不能访问自己新建的文件，组内的成员只能读。

</details>
