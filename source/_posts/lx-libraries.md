---
title: 静态库与动态库
date: 2026-10-24 10:05:00
updated: 2026-10-24 10:05:00
categories:
  - Linux基础
  - 开发工具
tags:
  - Linux
  - gcc
  - 动态库
order: 2
description: 把常用的函数打包成库，供多个程序使用。本文动手制作静态库 .a 和动态库 .so，复现最常见的错误「cannot open shared object file」并给出四种解决方法（LD_LIBRARY_PATH、rpath、ldconfig、静态链接），用 LD_DEBUG 看动态链接器的查找过程，演示升级动态库后程序不用重新编译，以及 soname 版本号的作用。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会制作和使用静态库、动态库；理解链接时和运行时分别在哪里查找库；能解决「找不到 .so」的错误；知道静态链接和动态链接各自的优缺点；了解 soname 和库文件的版本号。
>
> **前置知识**：《GCC 与 ELF 文件》中的符号、重定位和 `ldd`。
>
> **实验环境**：Ubuntu 24.04 容器（aarch64），GCC 13.3。

## 1. 什么是库

**库**就是打包好的、可以被别的程序使用的代码。写 C 程序时一直在用的 `printf`、`malloc`，就来自 C 标准库 glibc。库有两种：

| | 静态库 | 动态库（共享库） |
|---|---|---|
| 文件名 | `libxxx.a` | `libxxx.so` |
| 本质 | 一组 `.o` 文件的归档 | 一个 ELF 共享目标文件 |
| 何时并入程序 | **链接时**，代码被复制进可执行文件 | **运行时**，由动态链接器加载 |
| Windows 上的对应 | `.lib` | `.dll` |

下面把两个函数做成一个库：

```c
// mymath.h
#ifndef MYMATH_H
#define MYMATH_H

int add(int a, int b);
int mul(int a, int b);

#endif
```

```c
// add.c
#include "mymath.h"

int add(int a, int b) { return a + b; }
```

```c
// mul.c
#include "mymath.h"

int mul(int a, int b) { return a * b; }
```

使用它的程序：

```c
// main.c
#include <stdio.h>
#include "mymath.h"

int main(void)
{
    printf("3 + 4 = %d\n", add(3, 4));
    return 0;
}
```

## 2. 静态库

### 2.1 制作

先编译出目标文件，再用 `ar`（archive，归档）把它们打包：

```bash
gcc -c add.c mul.c
ar rcs libmymath.a add.o mul.o
ar t libmymath.a
nm libmymath.a
```

```
add.o
mul.o

add.o:
0000000000000000 T add

mul.o:
0000000000000000 T mul
```

`ar rcs` 中，`r` 表示加入文件（已有的就替换），`c` 表示库不存在时创建，`s` 表示生成符号索引，让链接器能快速查到哪个 `.o` 中定义了哪个符号。`ar t` 列出库中的文件。可以看到，静态库就是把几个 `.o` 装进了一个文件。

### 2.2 使用

```bash
gcc main.c -L. -lmymath -o app_static
./app_static
```

```
3 + 4 = 7
```

- `-L.`：在当前目录中查找库（默认只在系统目录中查找）；
- `-lmymath`：链接名为 `mymath` 的库。链接器会去找 `libmymath.so` 或 `libmymath.a`，**前缀 `lib` 和后缀由链接器自动加上**。

只包含了用到的部分：

```bash
nm app_static | grep -E " T (add|mul)"
```

```
0000000000000788 T add
```

程序中只有 `add`，没有 `mul`。链接器从静态库中**只取出需要的 `.o` 文件**：`main.c` 用到了 `add`，所以取出 `add.o`；没有用到 `mul`，`mul.o` 就不会被放进程序。（取的单位是整个 `.o`，所以如果 `add` 和 `mul` 写在同一个 `.c` 文件中，就会一起被放进去。）

### 2.3 链接顺序

把 `-lmymath` 放到 `main.c` 前面：

```bash
gcc -L. -lmymath main.c -o bad
```

```
/usr/bin/ld: /tmp/cccX28Mc.o: in function `main':
main.c:(.text+0x10): undefined reference to `add'
collect2: error: ld returned 1 exit status
```

库明明有 `add`，却报找不到。原因是链接器**从左到右**处理命令行上的文件：处理到 `libmymath.a` 时，还没有任何文件需要 `add`，所以一个 `.o` 都没有取出来；之后处理 `main.c` 时，才出现了对 `add` 的需要，但库已经处理过了，不会回头再找。

所以规则是：**库要放在使用它的文件后面**。如果库 A 用到了库 B，就写成 `-lA -lB`。

## 3. 动态库

### 3.1 制作

```bash
gcc -fPIC -shared add.c mul.c -o libmymath.so
file libmymath.so
nm -D libmymath.so | grep -E " T "
```

```
libmymath.so: ELF 64-bit LSB shared object, ARM aarch64, version 1 (SYSV), dynamically linked, BuildID[sha1]=1b69200be1125e1b4f63ec5719ecaada3791f2b5, not stripped
0000000000000568 T add
0000000000000588 T mul
```

- `-shared`：生成动态库；
- `-fPIC`：生成**位置无关代码**（Position-Independent Code）。动态库会被加载到不同进程的不同地址，代码中不能写死任何绝对地址，访问全局变量和调用外部函数都要通过上一篇讲的 GOT 和 PLT。

### 3.2 使用：链接成功，运行失败

链接的写法和静态库完全一样：

```bash
gcc main.c -L. -lmymath -o app_dyn
./app_dyn
echo "exit=$?"
```

```
./app_dyn: error while loading shared libraries: libmymath.so: cannot open shared object file: No such file or directory
exit=127
```

**链接成功了，运行却失败了。**这是使用动态库时最常见的错误。原因是查找库发生在两个不同的时刻：

| 时刻 | 谁来找 | 去哪里找 |
|---|---|---|
| **链接时** | 链接器 `ld` | `-L` 指定的目录，以及系统目录 |
| **运行时** | 动态链接器 `ld-linux` | rpath、`LD_LIBRARY_PATH`、`/etc/ld.so.cache` 中记录的目录、系统默认目录 |

`-L.` 只对链接器有效。可执行文件中只记录了需要的库的**名字**，没有记录路径：

```bash
readelf -d app_dyn | grep NEEDED
ldd app_dyn
```

```
 0x0000000000000001 (NEEDED)             Shared library: [libmymath.so]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
	linux-vdso.so.1 (0x0000fbc6692da000)
	libmymath.so => not found
	libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x0000fbc6690a0000)
	/lib/ld-linux-aarch64.so.1 (0x0000fbc669290000)
```

`ldd` 清楚地显示 `libmymath.so => not found`。设置环境变量 `LD_DEBUG=libs`，动态链接器会打印它查找库的过程：

```bash
LD_DEBUG=libs ./app_dyn
```

```
      5277:	find library=libmymath.so [0]; searching
      5277:	 search cache=/etc/ld.so.cache
      5277:	 search path=/lib/aarch64-linux-gnu:/usr/lib/aarch64-linux-gnu:/lib:/usr/lib		(system search path)
      5277:	  trying file=/lib/aarch64-linux-gnu/libmymath.so
      5277:	  trying file=/usr/lib/aarch64-linux-gnu/libmymath.so
      5277:	  trying file=/lib/libmymath.so
      5277:	  trying file=/usr/lib/libmymath.so
      5277:	
./app_dyn: error while loading shared libraries: libmymath.so: cannot open shared object file: No such file or directory
```

它先查了缓存 `/etc/ld.so.cache`，然后依次尝试了 4 个系统目录，都没有找到，唯独没有去找当前目录。

## 4. 让程序找到动态库

### 4.1 LD_LIBRARY_PATH

```bash
LD_LIBRARY_PATH=. ./app_dyn
```

```
3 + 4 = 7
```

`LD_LIBRARY_PATH` 中的目录会被优先查找，多个目录用冒号分隔。它适合**开发和测试**时临时使用，不适合作为正式的解决办法：它影响这个 Shell 启动的所有程序，而且换个终端就失效了。

### 4.2 rpath：把查找路径写进程序

```bash
gcc main.c -L. -lmymath -Wl,-rpath,'$ORIGIN' -o app_rpath
readelf -d app_rpath | grep RUNPATH
./app_rpath
```

```
 0x000000000000001d (RUNPATH)            Library runpath: [$ORIGIN]
3 + 4 = 7
```

`-Wl,-rpath,目录` 让链接器把一个查找目录写进可执行文件的 `.dynamic` 节。`$ORIGIN` 是一个特殊的写法，表示**可执行文件自己所在的目录**（要用单引号，防止 Shell 把 `$ORIGIN` 当成变量展开）。把程序和库一起复制到别的目录，仍然能运行：

```bash
mkdir -p /tmp/elsewhere && cp app_rpath libmymath.so /tmp/elsewhere/
cd /tmp/elsewhere && ./app_rpath
```

```
3 + 4 = 7
```

很多软件把程序和它自带的库放在一起发布，用的就是这个方法。

### 4.3 安装到系统目录，并运行 ldconfig

正式安装的库，通常放在 `/usr/local/lib` 中：

```bash
sudo cp libmymath.so /usr/local/lib/
./app_dyn
```

```
./app_dyn: error while loading shared libraries: libmymath.so: cannot open shared object file: No such file or directory
```

复制过去了，还是找不到。前面 `LD_DEBUG` 的输出显示，动态链接器先查缓存 `/etc/ld.so.cache`，而 `/usr/local/lib` 不在它直接尝试的 4 个默认目录中。新安装的库要运行 `ldconfig` 更新缓存：

```bash
sudo ldconfig
./app_dyn
ldconfig -p | grep mymath
```

```
3 + 4 = 7
	libmymath.so (libc6,AArch64) => /usr/local/lib/libmymath.so
```

`ldconfig` 扫描 `/etc/ld.so.conf.d/` 中列出的目录，把找到的库记录到缓存中。`/usr/local/lib` 就列在其中：

```bash
cat /etc/ld.so.conf.d/*.conf
```

```
# Multiarch support
/usr/local/lib/aarch64-linux-gnu
/lib/aarch64-linux-gnu
/usr/lib/aarch64-linux-gnu
# libc default configuration
/usr/local/lib
```

库在其他目录中，可以在 `/etc/ld.so.conf.d/` 中新建一个 `.conf` 文件写上目录，再运行 `ldconfig`。用 `make install` 安装软件后，如果出现找不到 `.so` 的错误，多半就是忘了运行 `sudo ldconfig`。

### 4.4 改用静态链接

如果同一个目录中既有 `libmymath.a` 又有 `libmymath.so`，`-lmymath` 会**优先选择动态库**：

```bash
ls libmymath.*
gcc main.c -L. -lmymath -o t1
ldd t1 | grep mymath
```

```
libmymath.a
libmymath.so
	libmymath.so => not found
```

要使用静态库，可以写出完整的文件名 `-l:libmymath.a`，或者用 `-static` 让所有的库都静态链接：

```bash
gcc main.c -L. -l:libmymath.a -o t2
ldd t2 | grep -c mymath
```

```
0
```

`t2` 不再依赖 `libmymath.so`。

| 方法 | 适用场合 |
|---|---|
| `LD_LIBRARY_PATH` | 开发、测试时临时使用 |
| rpath（`$ORIGIN`） | 程序和库一起发布 |
| 安装到系统目录 + `ldconfig` | 系统中多个程序共用的库 |
| 静态链接 | 想要一个不依赖任何库的程序 |

## 5. 动态库的好处：升级库，不用重新编译程序

把 `add` 改成一个会打印日志的新版本，重新编译出动态库并安装（`app_dyn` 和 `app_static` 都**不重新编译**）：

```c
// add_v2.c
#include <stdio.h>
#include "mymath.h"

int add(int a, int b)
{
    fprintf(stderr, "[libmymath v2] add(%d, %d)\n", a, b);
    return a + b;
}
```

```bash
gcc -fPIC -shared add_v2.c mul.c -o libmymath.so
sudo cp libmymath.so /usr/local/lib/ && sudo ldconfig
./app_dyn
./app_static
```

```
[libmymath v2] add(3, 4)
3 + 4 = 7
3 + 4 = 7
```

`app_dyn` 每次运行时才加载库，所以自动用上了新版本；`app_static` 中的是链接时复制进去的旧代码，不会改变。这就是动态库的一大好处：**库修复了一个安全漏洞，只要更新库文件，所有使用它的程序都得到了修复**。如果是静态链接的，每个程序都要重新编译、重新发布。

两种方式的比较：

| | 静态链接 | 动态链接 |
|---|---|---|
| 部署 | 一个文件，复制过去就能运行 | 目标机器上要有对应版本的库 |
| 升级库 | 每个程序都要重新链接 | 替换库文件即可 |
| 磁盘和内存 | 每个程序各有一份库代码 | 多个进程共用同一份库的代码（只读的代码段在物理内存中只有一份） |
| 启动 | 稍快，不用加载库 | 要由动态链接器加载和重定位 |

Linux 发行版中的程序绝大多数是动态链接的，库由软件包管理器统一升级。

## 6. soname 和版本号

如果库的新版本改变了函数的参数，旧程序还使用它，就会出错。所以动态库通常带有版本号，看看系统中的库：

```bash
ls -l /lib/aarch64-linux-gnu/libstdc++.so*
```

```
lrwxrwxrwx 1 root root      19 Dec 18  2025 /lib/aarch64-linux-gnu/libstdc++.so.6 -> libstdc++.so.6.0.33
-rw-r--r-- 1 root root 2633224 Dec 18  2025 /lib/aarch64-linux-gnu/libstdc++.so.6.0.33
```

真正的文件是 `libstdc++.so.6.0.33`，`libstdc++.so.6` 是指向它的软链接。仿照这种方式制作 `libmymath`：

```bash
gcc -fPIC -shared -Wl,-soname,libmymath.so.1 add.c mul.c -o libmymath.so.1.0.0
readelf -d libmymath.so.1.0.0 | grep SONAME
ln -s libmymath.so.1.0.0 libmymath.so.1
ln -s libmymath.so.1 libmymath.so
ls -l libmymath*
```

```
 0x000000000000000e (SONAME)             Library soname: [libmymath.so.1]
lrwxrwxrwx 1 lucky lucky    14 Oct 24 02:07 libmymath.so -> libmymath.so.1
lrwxrwxrwx 1 lucky lucky    18 Oct 24 02:07 libmymath.so.1 -> libmymath.so.1.0.0
-rwxr-xr-x 1 lucky lucky 69344 Oct 24 02:07 libmymath.so.1.0.0
```

```bash
gcc main.c -L. -lmymath -Wl,-rpath,'$ORIGIN' -o app
readelf -d app | grep NEEDED
```

```
 0x0000000000000001 (NEEDED)             Shared library: [libmymath.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
```

三个名字各有用途：

| 名字 | 例子 | 用途 |
|---|---|---|
| 真实文件名 | `libmymath.so.1.0.0` | 包含完整的版本号 |
| **soname** | `libmymath.so.1` | 只包含主版本号；写在库中，链接时被记录进程序的 `NEEDED`，**运行时按它查找** |
| 链接名 | `libmymath.so` | 只在链接时给 `-lmymath` 使用；Ubuntu 中由 `-dev` 软件包提供 |

注意 `app` 的 `NEEDED` 是 `libmymath.so.1`，而不是链接时用的 `libmymath.so`。约定是：

- 修复 bug、增加新函数，不影响已有的程序：主版本号不变，比如升级到 `libmymath.so.1.1.0`，把 `libmymath.so.1` 指向它，所有程序都用上新版本；
- 删除函数、改变参数等不兼容的修改：主版本号加 1，soname 变成 `libmymath.so.2`。旧程序仍然找 `.so.1`，新程序找 `.so.2`，两个版本可以同时安装。

## 7. 常见错误

1. **`cannot open shared object file`**：运行时找不到动态库。先用 `ldd` 看哪个是 `not found`，再按第 4 节选择解决方法。
2. **`undefined reference`，但库中明明有这个函数**：检查 `-l` 是否放在了使用它的文件后面；用 `nm` 检查库中的符号名是否一致。
3. **复制库到 `/usr/local/lib` 后忘了 `sudo ldconfig`。**
4. **制作动态库时忘了 `-fPIC`**：本文的 `add.c` 不加 `-fPIC` 也能做成动态库，因为它没有访问任何全局变量。但只要访问了全局变量，比如函数中执行 `++counter`，链接时就会报错：

   ```
   /usr/bin/ld: g.o: relocation R_AARCH64_ADR_PREL_PG_HI21 against symbol `counter' which may bind externally can not be used when making a shared object; recompile with -fPIC
   ```

   报错的最后一句就是解决方法。制作动态库时总是加上 `-fPIC`。
5. **以为有 `.a` 就会静态链接**：同一个目录中有 `.so` 时，`-l` 优先选择 `.so`。
6. **用 `LD_LIBRARY_PATH` 作为正式的解决办法**：换个环境就失效。正式发布时用 rpath 或安装到系统目录。
7. **C++ 写的库给 C 程序用，链接时找不到函数**：C++ 会改编函数名，用 `g++` 编译同样的 `add`，`nm` 中看到的是 `T _Z3addii`，头文件中要用 `extern "C"` 声明（《预处理与多文件编程》）。

## 8. 小结

- 静态库 `.a` 是 `.o` 文件的归档，用 `ar rcs` 制作，链接时只取出用到的 `.o`，复制进程序
- 动态库 `.so` 用 `gcc -fPIC -shared` 制作，运行时由动态链接器加载；程序中只记录了库的名字（`NEEDED`）
- 使用库：`-L目录 -l名字`，库要放在使用它的文件后面；同时有 `.a` 和 `.so` 时优先 `.so`
- 运行时找不到 `.so`：`LD_LIBRARY_PATH` 临时解决，rpath（`$ORIGIN`）随程序发布，安装到系统目录后运行 `ldconfig`，或者静态链接
- `ldd` 查看依赖，`LD_DEBUG=libs` 查看查找过程
- 升级动态库，使用它的程序不用重新编译；soname 带主版本号，不兼容的修改要换主版本号

## 9. 练习

1. 一个程序用到了 `libA`，而 `libA` 中的函数又调用了 `libB` 中的函数，两个都是静态库。链接命令应该怎样写？
2. 用户反馈程序在他的电脑上运行时报 `libfoo.so.3: cannot open shared object file`，但是他的电脑上装着 `libfoo.so.4`。能不能建一个软链接 `libfoo.so.3 -> libfoo.so.4` 解决？
3. 怎样看出系统中的 `ls` 命令依赖哪些动态库？

<details>
<summary>点击查看答案</summary>

1. `gcc main.c -L. -lA -lB`。链接器从左到右处理：处理 `main.c` 时需要 `libA` 中的函数；处理 `-lA` 时取出这些函数，又产生了对 `libB` 的需要；最后处理 `-lB` 时满足这些需要。实测写成 `-lB -lA`：

   ```
   /usr/bin/ld: ./libA.a(a.o): in function `a_val':
   a.c:(.text+0x8): undefined reference to `b_val'
   ```

   报错指出，是 `libA.a` 中的 `a.o` 找不到 `b_val`。

2. 不应该这样做。soname 的主版本号从 3 变成 4，按照约定，表示两个版本**不兼容**：函数可能被删除了，或者参数、结构体的布局变了。强行链接过去，程序可能在运行到某个函数时才崩溃，或者悄悄地算错。正确的做法是安装 `libfoo.so.3`（两个主版本可以同时安装），或者把程序用 `libfoo.so.4` 重新编译。

3. `ldd /usr/bin/ls`。实测：

   ```
   	linux-vdso.so.1 (0x0000eacbae1d7000)
   	libselinux.so.1 => /lib/aarch64-linux-gnu/libselinux.so.1 (0x0000eacbae0f0000)
   	libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x0000eacbadf30000)
   	/lib/ld-linux-aarch64.so.1 (0x0000eacbae190000)
   	libpcre2-8.so.0 => /lib/aarch64-linux-gnu/libpcre2-8.so.0 (0x0000eacbade80000)
   ```

   `ls` 除了 glibc，还依赖 SELinux 的库（`ls -Z` 显示安全上下文时要用），`libselinux` 又依赖正则表达式库 `libpcre2`。另一个方法是 `readelf -d /usr/bin/ls | grep NEEDED`，它只列出直接依赖（`libselinux.so.1`、`libc.so.6`、`ld-linux-aarch64.so.1`），没有 `libpcre2`；`ldd` 会把间接依赖也列出来。

</details>
