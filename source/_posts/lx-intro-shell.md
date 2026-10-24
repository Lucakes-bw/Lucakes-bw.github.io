---
title: 认识 Linux 与 Shell
date: 2026-10-24 09:00:00
updated: 2026-10-24 09:00:00
categories:
  - Linux基础
  - 常用命令
tags:
  - Linux
  - Shell
order: 1
description: 学习 Linux 的第一步：知道 Linux 和发行版是什么，分清终端和 Shell，理解 Linux 的目录结构和路径，学会用 man 和 --help 自己查资料。本专栏的所有命令和程序都在真实的 Ubuntu 24.04 中运行，输出都是实际的结果。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：了解 Linux、内核和发行版的关系；分清终端、Shell 和命令；理解 Linux 的目录结构，熟练使用绝对路径和相对路径；会用 `man`、`--help`、`type` 自己查找答案。
>
> **前置知识**：无。
>
> **实验环境**：本专栏的所有命令都在 **Ubuntu 24.04 LTS**（Linux 6.8，ARM64）中运行，以普通用户 `lucky` 的身份执行，主机名是 `devbox`。如果你使用的是 Windows，可以安装 WSL（Windows Subsystem for Linux）；如果使用 macOS，可以用 Docker 或虚拟机运行一个 Ubuntu。macOS 的终端虽然也有很多相同的命令，但很多细节不同，学习时最好使用真正的 Linux。

## 1. Linux 是什么

**Linux** 严格来说是一个操作系统**内核**，由 Linus Torvalds 在 1991 年开始编写。内核负责管理硬件、内存、进程和文件，但只有内核是用不了的，还需要 Shell、编译器、各种命令和软件。

把 Linux 内核和这些软件打包在一起，就是一个 **Linux 发行版**。常见的发行版有：

| 发行版 | 特点 |
|---|---|
| **Ubuntu** | 最流行的桌面和服务器发行版之一，资料最多，适合入门 |
| Debian | Ubuntu 的上游，以稳定著称 |
| Fedora / RHEL / Rocky Linux | Red Hat 系，企业服务器中常见 |
| Arch Linux | 滚动更新，高度可定制，适合喜欢折腾的人 |
| Alpine | 非常小巧，常用作 Docker 容器的基础镜像 |

它们的内核是同一个，常用的命令也几乎一样，主要区别在于软件包管理器（Ubuntu 用 `apt`，Fedora 用 `dnf`）和默认的配置。本专栏使用 Ubuntu。

为什么程序员要学 Linux？绝大多数服务器运行的是 Linux；Android 基于 Linux 内核；云计算、容器（Docker）、嵌入式设备都离不开它。而且 Linux 下的开发工具链（GCC、GDB、Make）就是本博客 C/C++ 专栏中一直在用的工具。

## 2. 终端、Shell 和命令

打开 Linux 的「终端」，会看到一行提示符：

```
lucky@devbox:~$
```

它的含义是：当前用户是 `lucky`，主机名是 `devbox`，当前所在的目录是 `~`（用户的主目录），`$` 表示普通用户（root 用户是 `#`）。

这里涉及三个概念：

- **终端**（terminal）：一个显示文字、接收键盘输入的窗口程序。
- **Shell**：在终端中运行的**命令解释器**。它读取你输入的命令，解析它，然后执行。最常用的 Shell 是 **bash**，此外还有 zsh、fish、dash 等。
- **命令**：要执行的程序，或者 Shell 自己内置的功能。

```bash
echo $SHELL
bash --version | head -1
cat /etc/shells
```

```
/bin/bash
GNU bash, version 5.2.21(1)-release (aarch64-unknown-linux-gnu)
# /etc/shells: valid login shells
/bin/sh
/usr/bin/sh
/bin/bash
/usr/bin/bash
/bin/rbash
/usr/bin/rbash
/usr/bin/dash
```

`$SHELL` 是一个环境变量，记录了当前用户的默认 Shell。`/etc/shells` 列出了系统中所有可用的 Shell。

### 2.1 内置命令和外部命令

命令分为两种：

```bash
type cd pwd echo ls type gcc
```

```
cd is a shell builtin
pwd is a shell builtin
echo is a shell builtin
ls is /usr/bin/ls
type is a shell builtin
gcc is /usr/bin/gcc
```

- **内置命令**（builtin）：Shell 自己实现的功能，比如 `cd`、`pwd`、`echo`。
- **外部命令**：磁盘上的一个可执行文件，比如 `ls` 是 `/usr/bin/ls`，`gcc` 是 `/usr/bin/gcc`。输入 `ls` 时，Shell 会在环境变量 `PATH` 列出的目录中依次查找名为 `ls` 的文件：

```bash
echo $PATH
```

```
/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
```

为什么 `cd` 必须是内置命令？因为它要改变 **Shell 自己**的当前目录。如果 `cd` 是一个外部程序，它只能改变那个程序自己的当前目录，程序结束后，Shell 的当前目录还是原来的（第 4 章讲进程时会明白其中的原因）。

### 2.2 命令的格式和退出码

命令的一般格式是：

```
命令  选项  参数
ls    -l    /etc
```

选项通常以 `-` 开头（短选项，如 `-l`，可以合并写成 `-la`），或者以 `--` 开头（长选项，如 `--all`）。

每个命令执行结束后都有一个**退出码**，0 表示成功，非 0 表示失败（和 C 语言中 `main` 的返回值完全是同一个东西，见《C 语言概述与第一个程序》）。用 `$?` 查看上一个命令的退出码：

```bash
cd /nonexistent
echo $?
lss
echo $?
```

```
bash: cd: /nonexistent: No such file or directory
1
bash: lss: command not found
127
```

127 是 Shell 约定的「找不到命令」。

### 2.3 让输入更快的技巧

- **Tab 补全**：输入命令或文件名的前几个字母，按 Tab 自动补全；有多个候选时按两次 Tab 列出所有可能。这是最重要的技巧，既能少打字，又能避免拼写错误。比如输入 `gc` 按两次 Tab，会列出 `gcc`、`gcc-13`、`gcore`、`gcov` 等。
- **↑ ↓**：翻看之前输入过的命令；**Ctrl+R**：搜索历史命令。
- **Ctrl+C**：中断正在运行的命令；**Ctrl+D**：表示输入结束（在空行上按会退出 Shell）；**Ctrl+L**：清屏。

## 3. 目录结构

Linux 中**没有 C 盘、D 盘**，所有的文件都在一棵以 `/`（根目录）开始的目录树中：

```bash
ls /
```

```
bin   boot  dev  etc  home  lib  media  mnt  opt  proc  root  run  sbin  srv  sys  tmp  usr  var  work
```

（为了排版，这里把多行输出合成了一行。`work` 是本专栏实验环境中挂载的工作目录，不是标准的目录。）

| 目录 | 用途 |
|---|---|
| `/home` | 普通用户的主目录，比如 `/home/lucky` |
| `/root` | root 用户的主目录 |
| `/etc` | 系统和软件的配置文件 |
| `/usr` | 系统安装的程序和库：`/usr/bin` 是命令，`/usr/lib` 是库，`/usr/include` 是 C 语言的头文件 |
| `/usr/local` | 自己编译安装的软件 |
| `/var` | 经常变化的数据，比如日志 `/var/log` |
| `/tmp` | 临时文件，重启后可能被清空 |
| `/dev` | 设备文件，比如硬盘、终端 |
| `/proc`、`/sys` | 内核提供的虚拟文件系统，用来查看进程和系统的信息（第 1.5 篇） |
| `/boot` | 启动相关的文件，比如内核 |
| `/opt` | 一些第三方软件 |

在较新的发行版中，`/bin`、`/lib`、`/sbin` 只是指向 `/usr` 中对应目录的**链接**：

```bash
ls -l / | grep -- "->"
```

```
lrwxrwxrwx   1 root root    7 Apr 22  2024 bin -> usr/bin
lrwxrwxrwx   1 root root    7 Apr 22  2024 lib -> usr/lib
lrwxrwxrwx   1 root root    8 Apr 22  2024 sbin -> usr/sbin
```

Linux 的一个核心理念是**一切皆文件**：普通的文件是文件，目录是文件，硬盘、终端、甚至进程的信息都以文件的形式出现。第 3 章讲文件 IO 时会深入理解这一点。

另外要注意：Linux 的文件名**区分大小写**，`File.txt` 和 `file.txt` 是两个不同的文件；以 `.` 开头的文件是**隐藏文件**，`ls` 默认不显示，要用 `ls -a`。

## 4. 路径

### 4.1 绝对路径和相对路径

- **绝对路径**：从根目录 `/` 开始，比如 `/home/lucky/projects`。在任何位置使用都指向同一个地方。
- **相对路径**：从**当前目录**开始，比如 `projects/blog`。

几个特殊的符号：

| 符号 | 含义 |
|---|---|
| `.` | 当前目录 |
| `..` | 上一级目录 |
| `~` | 当前用户的主目录（`$HOME`） |
| `-` | 在 `cd -` 中表示上一次所在的目录 |

```bash
pwd
mkdir -p projects/blog/src
cd projects/blog/src
pwd
cd ../..
pwd
cd ~
pwd
cd /etc
cd -
cd /usr/local/../bin
pwd
```

```
/home/lucky
/home/lucky/projects/blog/src
/home/lucky/projects
/home/lucky
/home/lucky
/usr/bin
```

- `pwd`（print working directory）显示当前目录；`cd` 切换目录，不带参数时回到主目录。
- `mkdir -p` 可以一次创建多级目录，目录已经存在也不报错。
- `cd -` 回到上一次所在的目录（`/home/lucky`），并显示它。
- `/usr/local/../bin` 中的 `..` 回到了 `/usr`，所以结果是 `/usr/bin`。

`tree` 命令可以把目录结构画出来（Ubuntu 中需要先安装：`sudo apt install tree`）：

```bash
tree projects
```

```
projects
`-- blog
    `-- src

3 directories, 0 files
```

### 4.2 主目录中的隐藏文件

```bash
ls -a ~
```

```
.
..
.bash_logout
.bashrc
.profile
projects
```

`.bashrc` 是 bash 的配置文件，每次打开一个新的 Shell 都会执行它，可以在里面设置别名、环境变量等。注意 `.` 和 `..` 也出现在了列表中。

## 5. 自己查找答案

Linux 的命令非常多，选项更是数不清，没有人能全部记住。重要的是**会查**。

### 5.1 --help

大多数命令都支持 `--help`，显示简短的用法说明：

```bash
ls --help | head -5
```

```
Usage: ls [OPTION]... [FILE]...
List information about the FILEs (the current directory by default).
Sort entries alphabetically if none of -cftuvSUX nor --sort is specified.

Mandatory arguments to long options are mandatory for short options too.
```

（`| head -5` 只显示前 5 行，管道会在第 1.3 篇介绍。）在用法说明中，`[ ]` 表示可选，`...` 表示可以有多个。

### 5.2 man

`man`（manual）显示详细的手册页：

```bash
man ls
```

```
LS(1)                            User Commands                           LS(1)

NAME
       ls - list directory contents

SYNOPSIS
       ls [OPTION]... [FILE]...

DESCRIPTION
       List  information  about  the FILEs (the current directory by default).
       Sort entries alphabetically if none of -cftuvSUX nor --sort  is  speci-
       fied.
```

在 `man` 中，用空格向下翻页，`b` 向上翻页，`/关键字` 搜索，`n` 跳到下一个匹配，`q` 退出。

手册页分为很多**节**，最常用的是：

| 节 | 内容 |
|---|---|
| 1 | 用户命令（`ls`、`gcc`） |
| 2 | 系统调用（`open`、`fork`，第 3、4 章会大量使用） |
| 3 | C 库函数（`printf`、`malloc`） |
| 5 | 文件格式（`/etc/passwd` 的格式） |

同一个名字可能出现在多个节中：

```bash
man -f printf
```

```
printf (1)           - format and print data
printf (1posix)      - write formatted output
printf (3)           - formatted output conversion
```

`printf` 既是一个命令（第 1 节），也是 C 库函数（第 3 节）。`man printf` 默认显示第 1 节，要看 C 函数，写 `man 3 printf`：

```
printf(3)             Library Functions Manual             printf(3)

NAME
       printf,  fprintf,  dprintf,  sprintf,  snprintf, vprintf, vf-
       printf, vdprintf, vsprintf, vsnprintf - formatted output con-
       version
```

手册页是**最权威、和你的系统版本完全一致**的文档，写 C 程序时查函数的参数和返回值，第一个应该看的就是它。

> Docker 的 Ubuntu 镜像为了减小体积，默认删除了手册页，`man` 会提示「This system has been minimized」。在这样的环境中，可以运行 `unminimize` 恢复。正常安装的 Ubuntu 没有这个问题。

### 5.3 内置命令的帮助

内置命令没有单独的手册页，用 `help` 查看：

```bash
help cd | head -2
```

```
cd: cd [-L|[-P [-e]] [-@]] [dir]
    Change the shell working directory.
```

## 6. 常见错误

1. **混淆大小写**：`Ls`、`CD` 都是「找不到命令」。
2. **路径中有空格不加引号**：`cd My Documents` 会被当作两个参数。要写成 `cd "My Documents"` 或 `cd My\ Documents`。
3. **分不清当前目录**：执行命令前先 `pwd` 看一看，尤其是在执行删除等危险操作之前。
4. **以为 `/home` 是主目录**：`/home` 是所有用户主目录的上一级，`~` 才是自己的主目录。
5. **遇到问题直接到网上搜**：先看 `--help` 和 `man`，它们和你的系统版本是一致的，网上的答案可能针对的是另一个发行版或版本。

## 7. 小结

- Linux 是内核，发行版是内核加上各种软件；本专栏使用 Ubuntu 24.04
- 终端是窗口，Shell（通常是 bash）是解释命令的程序；命令分为内置命令和 `PATH` 中的外部命令
- 退出码 0 表示成功；`$?` 查看上一个命令的退出码，127 表示找不到命令
- 所有文件在一棵以 `/` 开始的目录树中；`/home`、`/etc`、`/usr`、`/var`、`/tmp` 是最常用的目录
- 绝对路径从 `/` 开始，相对路径从当前目录开始；`.`、`..`、`~`、`cd -`
- 用 `--help`、`man`（`man 3 printf` 查 C 函数）、`help` 查找答案；Tab 补全是最重要的技巧

## 8. 练习

1. `cd`、`pwd`、`echo`、`ls`、`type` 中，哪些是内置命令？怎样验证？
2. 当前目录是 `/home/lucky/projects/blog/src`，执行 `cd ../..` 之后在哪个目录？用绝对路径怎样直接到达那里？
3. 想查看 C 语言 `open` 函数的手册页，应该用什么命令？它在第几节？

<details>
<summary>点击查看答案</summary>

1. 用 `type` 验证：

   ```
   cd is a shell builtin
   pwd is a shell builtin
   echo is a shell builtin
   ls is /usr/bin/ls
   type is a shell builtin
   ```

   只有 `ls` 是外部命令。（`echo` 和 `pwd` 其实也有外部命令的版本 `/usr/bin/echo`、`/usr/bin/pwd`，但 Shell 优先使用内置的版本。）

2. 在 `/home/lucky/projects`：第一个 `..` 回到 `blog`，第二个 `..` 回到 `projects`。用绝对路径是 `cd /home/lucky/projects`，或者 `cd ~/projects`。

3. `man 2 open`。`open` 是系统调用，在第 2 节（`man -f open` 显示 `open (2) - open and possibly create a file`）。如果直接写 `man open`，在有些系统上可能会先找到别的同名条目。

</details>
