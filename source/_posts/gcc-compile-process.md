---
title: GCC 与 ELF 文件
date: 2026-10-22 10:00:00
updated: 2026-10-24 10:00:00
categories:
  - Linux基础
  - 开发工具
tags:
  - Linux
  - gcc
  - ELF
order: 1
description: Linux 上的目标文件、可执行文件和动态库都是 ELF 格式。本文用 file、readelf、nm、objdump、size 拆开一个 hello 程序：节和段，全局变量放在哪里，4 MB 的数组为什么不占文件空间，符号和重定位怎样把函数调用连起来，以及程序运行时 ELF 文件怎样映射到内存。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：知道 ELF 文件有哪几种；会用 `file`、`readelf`、`nm`、`objdump`、`size` 查看 ELF 文件；理解 `.text`、`.data`、`.bss`、`.rodata` 这几个节分别存放什么；理解符号和重定位，从而理解链接器做了什么；了解程序运行时 ELF 文件怎样映射到内存。
>
> **前置知识**：预处理、编译、汇编、链接四个步骤，以及编译错误和链接错误的区别，在《开发环境与编程工具》中已经详细讲过，本文不再重复，只用一张表回顾。
>
> **实验环境**：Ubuntu 24.04 容器，GCC 13.3。容器运行在 Apple Silicon 的 Mac 上，所以 CPU 架构是 **aarch64**（ARM64），文中的汇编代码是 ARM 指令。在常见的 x86-64 电脑上，指令和一些名字会不同，但本文讲的结构和方法完全一样。

## 1. 回顾：四个步骤

| 步骤 | 命令 | 输出 |
|---|---|---|
| 预处理 | `gcc -E hello.c -o hello.i` | 展开了 `#include` 和宏的源代码 |
| 编译 | `gcc -S hello.i -o hello.s` | 汇编代码 |
| 汇编 | `gcc -c hello.s -o hello.o` | **目标文件**（ELF） |
| 链接 | `gcc hello.o -o hello` | **可执行文件**（ELF） |

本文关注最后两步的产物。示例程序：

```c
// hello.c
#include <stdio.h>

int main(void)
{
    printf("Hello, ELF!\n");
    return 0;
}
```

```bash
gcc -c hello.c -o hello.o
gcc hello.o -o hello
./hello
```

```
Hello, ELF!
```

## 2. ELF 文件的种类

**ELF**（Executable and Linkable Format）是 Linux 上目标文件、可执行文件、动态库和 core 文件共同使用的格式。用 `file` 查看：

```bash
file hello.c hello.o hello /usr/lib/aarch64-linux-gnu/libc.so.6
```

```
hello.c:                              C source, ASCII text
hello.o:                              ELF 64-bit LSB relocatable, ARM aarch64, version 1 (SYSV), not stripped
hello:                                ELF 64-bit LSB pie executable, ARM aarch64, version 1 (SYSV), dynamically linked, interpreter /lib/ld-linux-aarch64.so.1, BuildID[sha1]=fee2bf74a5651a312fa5afa00f7fb18301d30491, for GNU/Linux 3.7.0, not stripped
/usr/lib/aarch64-linux-gnu/libc.so.6: ELF 64-bit LSB shared object, ARM aarch64, version 1 (GNU/Linux), dynamically linked, interpreter /lib/ld-linux-aarch64.so.1, BuildID[sha1]=27027b96e5b8c475fc327aa445bea1c71d37b4e2, for GNU/Linux 3.7.0, stripped
```

| 种类 | 例子 | 说明 |
|---|---|---|
| **relocatable**（可重定位文件） | `hello.o` | 编译出的目标文件，还不能运行，要和其他文件链接 |
| **executable**（可执行文件） | `hello` | 可以运行；`pie` 表示位置无关，每次运行可以加载到不同的地址 |
| **shared object**（共享目标文件） | `libc.so.6` | 动态库，运行时被加载（下一篇详细讲） |
| **core** | `core` | 程序崩溃时的内存快照（第 2.3 篇） |

`file` 还告诉我们：`hello` 是**动态链接**的，它的 **interpreter**（解释器）是 `/lib/ld-linux-aarch64.so.1`。这是**动态链接器**，运行 `hello` 时，内核先加载它，由它去加载 `libc.so.6`，再跳到 `hello` 的代码。`not stripped` 表示文件中还保留着符号表（第 4 节）。

> `file` 不看文件的扩展名，而是看文件开头的内容。Linux 中的可执行文件不需要 `.exe` 这样的扩展名。

### 2.1 ELF 头

每个 ELF 文件都以 4 个字节 `7f 45 4c 46` 开头，后三个字节就是字符 `E`、`L`、`F`：

```bash
head -c 16 hello | od -A d -t x1 -c
```

```
0000000  7f  45  4c  46  02  01  01  00  00  00  00  00  00  00  00  00
        177   E   L   F 002 001 001  \0  \0  \0  \0  \0  \0  \0  \0  \0
```

这样的固定开头叫作**魔数**（magic number），`file` 就是靠它识别文件类型的。`readelf -h` 解析出完整的 ELF 头：

```bash
readelf -h hello
```

```
ELF Header:
  Magic:   7f 45 4c 46 02 01 01 00 00 00 00 00 00 00 00 00 
  Class:                             ELF64
  Data:                              2's complement, little endian
  Version:                           1 (current)
  OS/ABI:                            UNIX - System V
  ABI Version:                       0
  Type:                              DYN (Position-Independent Executable file)
  Machine:                           AArch64
  Version:                           0x1
  Entry point address:               0x640
  Start of program headers:          64 (bytes into file)
  Start of section headers:          68520 (bytes into file)
  Flags:                             0x0
  Size of this header:               64 (bytes)
  Size of program headers:           56 (bytes)
  Number of program headers:         9
  Size of section headers:           64 (bytes)
  Number of section headers:         28
  Section header string table index: 27
```

- `Class`、`Data`：64 位，小端序（魔数后面的 `02 01` 就是它们）；
- `Machine`：CPU 架构。aarch64 上编译的程序不能直接在 x86-64 上运行，反过来也一样；
- `Entry point address`：程序的**入口地址**。注意它不是 `main`，而是 C 运行库中的 `_start`，`_start` 做完初始化才调用 `main`；
- 后面几行说明了两张表的位置：**节头表**（section headers，28 个节）和**程序头表**（program headers，9 个段）。

## 3. 节：代码和数据放在哪里

ELF 文件的内容被分成很多**节**（section），每个节存放一类内容。用一个有各种全局变量的程序来看：

```c
// sections.c
#include <stdio.h>

int counter = 42;              // 有初始值的全局变量
int buffer[1000000];           // 没有初始值的全局变量，约 4 MB
static int hidden = 7;         // 只在本文件内可见
const char *greeting = "hello";

static void helper(void) { hidden++; }

int main(void)
{
    helper();
    printf("%s %d %d %d\n", greeting, counter, hidden, buffer[0]);
    return 0;
}
```

```bash
gcc -c sections.c -o sections.o
readelf -S -W sections.o | grep -E "Name|\.text|\.data|\.bss|\.rodata"
```

```
  [Nr] Name              Type            Address          Off    Size   ES Flg Lk Inf Al
  [ 1] .text             PROGBITS        0000000000000000 000040 00007c 00  AX  0   0  4
  [ 2] .rela.text        RELA            0000000000000000 0003d0 000168 18   I 12   1  8
  [ 3] .data             PROGBITS        0000000000000000 0000bc 000008 00  WA  0   0  4
  [ 4] .bss              NOBITS          0000000000000000 0000c8 3d0900 00  WA  0   0  8
  [ 5] .rodata           PROGBITS        0000000000000000 0000c8 000015 00   A  0   0  8
  [ 6] .data.rel.local   PROGBITS        0000000000000000 0000e0 000008 00  WA  0   0  8
  [ 7] .rela.data.rel.local RELA            0000000000000000 000538 000018 18   I 12   6  8
```

| 节 | 存放的内容 | 本例中 | 标志 |
|---|---|---|---|
| `.text` | 机器码 | `main`、`helper` 的代码，0x7c = 124 字节 | `AX`：加载到内存，可执行 |
| `.data` | 有初始值的全局变量和 `static` 变量 | `counter`、`hidden`，共 8 字节 | `WA`：可写 |
| `.bss` | 没有初始值（或初始值为 0）的全局变量 | `buffer`，0x3d0900 = 4000000 字节 | `WA`，类型 **NOBITS** |
| `.rodata` | 只读数据：字符串常量等 | `"hello"`、格式字符串，0x15 = 21 字节 | `A`：只读 |
| `.rela.text` | `.text` 的重定位信息 | 见第 5 节 | |

`greeting` 是一个指针变量，它的值是字符串的地址，要到链接或加载时才能确定，所以 GCC 把它放在了 `.data.rel.local` 中，可以把它看作 `.data` 的一部分。它指向的字符串 `"hello"` 在 `.rodata` 中。这正是《指针进阶》中说「字符串常量不能修改」的原因：`.rodata` 加载到内存后是**只读**的，修改它会导致段错误。

函数中的**局部变量**不在任何一个节中，它们在程序运行时才在**栈**上分配；`malloc` 分配的内存在**堆**上。

### 3.1 4 MB 的数组不占文件空间

`.bss` 的类型是 **NOBITS**：它在文件中**不占空间**，只记录了需要多大。用 `size` 查看各部分的大小：

```bash
gcc sections.o -o sections
size sections.o sections
ls -l sections
```

```
   text	   data	    bss	    dec	    hex	filename
    217	     16	4000000	4000233	 3d09e9	sections.o
   1772	    656	4000008	4002436	 3d1284	sections
-rwxr-xr-x 1 lucky lucky 70568 Oct 24 02:04 sections
```

`bss` 有 4000008 字节，可执行文件却只有 70568 字节。全局变量没有初始值时，C 语言规定它的值是 0。既然全是 0，就没必要把 4 MB 的 0 存在文件中，程序加载时由操作系统分配一块清零的内存就行了。如果写成 `int buffer[1000000] = {1};`，这 4 MB 就会进入 `.data`，文件也会变大 4 MB（练习 2）。

（`size` 的 `text` 一列不只是 `.text`，还包括 `.rodata` 等所有只读的部分。）

## 4. 符号：nm

**符号**是函数和全局变量的名字。链接器的工作，就是把每个使用符号的地方和这个符号的定义连起来。`nm` 列出文件中的符号：

```bash
nm sections.o
```

```
0000000000000000 B buffer
0000000000000000 D counter
0000000000000000 D greeting
0000000000000000 t helper
0000000000000004 d hidden
0000000000000024 T main
                 U printf
```

中间的字母是符号的类型：

| 字母 | 含义 |
|---|---|
| `T` / `t` | 在 `.text` 中，是函数 |
| `D` / `d` | 在 `.data` 中 |
| `B` / `b` | 在 `.bss` 中 |
| `R` / `r` | 在 `.rodata` 中 |
| `U` | **undefined**：这里使用了，但定义在别的文件中 |

**大写表示全局符号，小写表示局部符号。** `helper` 和 `hidden` 被声明为 `static`，所以是小写的 `t` 和 `d`：它们只在本文件内可见，其他文件中即使有同名的函数也不会冲突（《预处理与多文件编程》中讲过 `static` 的这个作用）。

`printf` 是 `U`：`sections.o` 调用了它，但没有定义它。链接时链接器要在其他目标文件或库中找到 `printf` 的定义，**找不到就报 `undefined reference to 'printf'`**。而如果两个目标文件中都有全局的 `T main`，就报 `multiple definition of 'main'`。用 `nm` 查看各个文件中的符号，是排查链接错误最直接的方法。

## 5. 重定位：链接器填上地址

编译 `hello.c` 时，编译器并不知道 `printf` 在哪里，也不知道字符串最终会放在什么地址。用 `objdump -d` 反汇编目标文件，`-r` 同时显示重定位信息：

```bash
objdump -d -r hello.o
```

```
0000000000000000 <main>:
   0:	a9bf7bfd 	stp	x29, x30, [sp, #-16]!
   4:	910003fd 	mov	x29, sp
   8:	90000000 	adrp	x0, 0 <main>
			8: R_AARCH64_ADR_PREL_PG_HI21	.rodata
   c:	91000000 	add	x0, x0, #0x0
			c: R_AARCH64_ADD_ABS_LO12_NC	.rodata
  10:	94000000 	bl	0 <puts>
			10: R_AARCH64_CALL26	puts
  14:	52800000 	mov	w0, #0x0                   	// #0
  18:	a8c17bfd 	ldp	x29, x30, [sp], #16
  1c:	d65f03c0 	ret
```

先注意一个有趣的地方：代码调用的是 `puts`，不是 `printf`。`printf("Hello, ELF!\n")` 的格式字符串中没有 `%`，又以换行结尾，和 `puts("Hello, ELF!")` 的效果完全相同，GCC 就把它换成了更简单的 `puts`。即使不开优化，GCC 也会做这种替换；加上 `-fno-builtin` 编译，`nm hello.o` 中就会是 `U printf`。

偏移 0x10 处的 `bl`（branch with link，即调用函数）指令是 `94000000`，后面的地址部分全是 0：**这个位置还没有填上真正的地址**。下面的 `R_AARCH64_CALL26 puts` 是一条**重定位记录**，意思是：「链接时，把 `puts` 的地址填到偏移 0x10 处这条指令中」。偏移 0x8 和 0xc 处的两条记录，则是要填上 `.rodata` 中字符串的地址。这些记录存放在 `.rela.text` 节中：

```bash
readelf -r hello.o
```

```
Relocation section '.rela.text' at offset 0x248 contains 3 entries:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000008  000500000113 R_AARCH64_ADR_PRE 0000000000000000 .rodata + 0
00000000000c  000500000115 R_AARCH64_ADD_ABS 0000000000000000 .rodata + 0
000000000010  000d0000011b R_AARCH64_CALL26  0000000000000000 puts + 0
```

链接之后再看：

```bash
objdump -d hello | grep -A8 "<main>:"
```

```
0000000000000758 <main>:
 758:	a9bf7bfd 	stp	x29, x30, [sp, #-16]!
 75c:	910003fd 	mov	x29, sp
 760:	90000000 	adrp	x0, 0 <__abi_tag-0x278>
 764:	911e6000 	add	x0, x0, #0x798
 768:	97ffffb2 	bl	630 <puts@plt>
 76c:	52800000 	mov	w0, #0x0                   	// #0
 770:	a8c17bfd 	ldp	x29, x30, [sp], #16
 774:	d65f03c0 	ret
```

- `main` 有了确定的位置 0x758；
- `add` 指令中填上了 0x798，这是字符串在程序中的位置；
- `bl` 指令变成了 `97ffffb2`，跳转到 0x630 处的 `puts@plt`。

`puts` 在 `libc.so.6` 中，`libc.so.6` 运行时才加载，加载到哪个地址事先也不知道。所以链接器没有直接填 `puts` 的地址，而是让它跳到一小段叫作 **PLT**（Procedure Linkage Table）的代码：

```
0000000000000630 <puts@plt>:
 630:	f00000f0 	adrp	x16, 1f000 <__FRAME_END__+0x1e770>
 634:	f947e611 	ldr	x17, [x16, #4040]
 638:	913f2210 	add	x16, x16, #0xfc8
 63c:	d61f0220 	br	x17
```

这段代码从一张叫作 **GOT**（Global Offset Table）的表中取出一个地址（`ldr x17, ...`），再跳过去（`br x17`）。程序运行时，动态链接器把 `puts` 的真实地址写进 GOT。这样代码本身不需要修改，只要改 GOT 中的一个表项。可执行文件需要哪些库，记录在 `.dynamic` 节中：

```bash
readelf -d hello | head -4
```

```
Dynamic section at offset 0xfda0 contains 27 entries:
  Tag        Type                         Name/Value
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
```

`nm hello` 中的 `U puts@GLIBC_2.17` 说明，`puts` 在可执行文件中仍然是未定义的，要从 glibc 中找。`ldd` 列出程序运行时实际会加载的库：

```bash
ldd hello
```

```
	linux-vdso.so.1 (0x0000e9831e78d000)
	libc.so.6 => /lib/aarch64-linux-gnu/libc.so.6 (0x0000e9831e560000)
	/lib/ld-linux-aarch64.so.1 (0x0000e9831e750000)
```

动态库怎样制作、怎样被找到，是下一篇的内容。

## 6. 段：程序怎样被加载

节是给链接器看的，分得很细；操作系统加载程序时并不关心这么多的节，它只关心哪些内容要以什么权限放进内存。所以 ELF 中还有另一种划分：**段**（segment），由**程序头表**描述。一个段包含若干个权限相同的节：

```bash
readelf -l -W hello
```

```
Program Headers:
  Type           Offset   VirtAddr           PhysAddr           FileSiz  MemSiz   Flg Align
  PHDR           0x000040 0x0000000000000040 0x0000000000000040 0x0001f8 0x0001f8 R   0x8
  INTERP         0x000238 0x0000000000000238 0x0000000000000238 0x00001b 0x00001b R   0x1
      [Requesting program interpreter: /lib/ld-linux-aarch64.so.1]
  LOAD           0x000000 0x0000000000000000 0x0000000000000000 0x000894 0x000894 R E 0x10000
  LOAD           0x00fd90 0x000000000001fd90 0x000000000001fd90 0x000280 0x000288 RW  0x10000
  DYNAMIC        0x00fda0 0x000000000001fda0 0x000000000001fda0 0x0001f0 0x0001f0 RW  0x8
  ...

 Section to Segment mapping:
  Segment Sections...
   ...
   02     .interp .note.gnu.build-id ... .plt .text .fini .rodata .eh_frame_hdr .eh_frame 
   03     .init_array .fini_array .dynamic .got .data .bss 
```

真正被加载到内存中的是两个 **LOAD** 段：

- 第一个（编号 02）的权限是 `R E`（可读、可执行），包含 `.text`、`.rodata` 等**只读**的内容；
- 第二个（编号 03）的权限是 `RW`（可读写），包含 `.data`、`.bss`、`.got` 等**可写**的内容。它的 `MemSiz`（0x288）比 `FileSiz`（0x280）大，多出来的 8 字节就是 `.bss`：文件中没有，加载时补上。

`INTERP` 段记录了动态链接器的路径，`file` 输出中的 interpreter 就是从这里读出来的。

### 6.1 为什么 hello 有 70 KB

`hello` 中真正的内容只有 2 KB 多（`size` 显示 text 1617、data 640、bss 8），文件却有 70312 字节。原因在 `Align` 一列：两个 LOAD 段都按 0x10000（**64 KB**）对齐，第二个段从文件偏移 0xfd90 开始，中间几乎都是填充。aarch64 上的 Linux 可能使用 64 KB 的内存页，所以链接器默认按 64 KB 对齐。改成按 4 KB 对齐：

```bash
gcc -Wl,-z,max-page-size=4096 hello.c -o hello_4k
ls -l hello_4k
```

```
-rwxr-xr-x 1 lucky lucky 8872 Oct 24 02:04 hello_4k
```

只有 8872 字节。（`-Wl,选项` 把选项传给链接器。）x86-64 上通常按 4 KB 对齐，没有这么多填充。

### 6.2 运行时的内存映射

《进程与系统监控》中介绍过 `/proc`，其中的 `/proc/PID/maps` 列出了进程的内存映射。写一个打印几个地址后等待 30 秒的程序：

```c
// wait.c
#include <stdio.h>
#include <unistd.h>

int counter = 42;
int buffer[1000000];

int main(void)
{
    printf("pid=%d main=%p counter=%p buffer=%p\n", getpid(), (void *)main, (void *)&counter, (void *)buffer);
    fflush(stdout);
    sleep(30);
    return 0;
}
```

```bash
gcc wait.c -o wait
./wait &
cat /proc/$!/maps
```

```
pid=5097 main=0xbe03b7a90898 counter=0xbe03b7ab0010 buffer=0xbe03b7ab0020
be03b7a90000-be03b7a91000 r-xp 00000000 00:2c 1070420                    /home/lucky/ch2/elf/wait
be03b7aaf000-be03b7ab0000 r--p 0000f000 00:2c 1070420                    /home/lucky/ch2/elf/wait
be03b7ab0000-be03b7ab1000 rw-p 00010000 00:2c 1070420                    /home/lucky/ch2/elf/wait
be03b7ab1000-be03b7e81000 rw-p 00000000 00:00 0 
be03e95db000-be03e95fc000 rw-p 00000000 00:00 0                          [heap]
fe2a26120000-fe2a262ba000 r-xp 00000000 00:2c 1052956                    /usr/lib/aarch64-linux-gnu/libc.so.6
...
fe2a262e0000-fe2a26307000 r-xp 00000000 00:2c 1052936                    /usr/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1
...
ffffd059f000-ffffd05c0000 rw-p 00000000 00:00 0                          [stack]
```

<figure class="diagram">
<svg viewBox="0 0 680 250" role="img" aria-label="ELF 文件到进程内存的映射：只读的 LOAD 段映射为 r-xp，可写的 LOAD 段一部分在加载后改为只读（r--p），一部分是 rw-p 的 .data，.bss 的剩余部分是一块没有对应文件的匿名内存">
  <defs>
    <marker id="elf-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="20" y="22" style="fill:var(--muted)">ELF 文件中的 LOAD 段</text>
    <text x="400" y="22" style="fill:var(--muted)">进程的内存（/proc/PID/maps）</text>
    <rect x="20" y="36" width="230" height="52" rx="5" style="fill:var(--panel-2);stroke:var(--border-2)"/>
    <text x="32" y="58" style="fill:var(--fg)">LOAD  R E</text>
    <text x="32" y="78" style="fill:var(--muted);font-size:12px">.text .rodata .plt …</text>
    <rect x="20" y="110" width="230" height="78" rx="5" style="fill:var(--panel-2);stroke:var(--border-2)"/>
    <text x="32" y="132" style="fill:var(--fg)">LOAD  RW</text>
    <text x="32" y="152" style="fill:var(--muted);font-size:12px">.got .dynamic …</text>
    <text x="32" y="170" style="fill:var(--muted);font-size:12px">.data  .bss（只记大小）</text>
    <rect x="400" y="36" width="260" height="34" rx="5" style="fill:var(--code-bg);stroke:var(--accent)"/>
    <text x="412" y="58" style="fill:var(--accent)">r-xp  代码  main</text>
    <rect x="400" y="84" width="260" height="34" rx="5" style="fill:var(--code-bg);stroke:var(--border-2)"/>
    <text x="412" y="106" style="fill:var(--fg)">r--p  GOT 等，加载后改为只读</text>
    <rect x="400" y="132" width="260" height="34" rx="5" style="fill:var(--code-bg);stroke:var(--warn)"/>
    <text x="412" y="154" style="fill:var(--warn)">rw-p  counter，buffer 开头</text>
    <rect x="400" y="180" width="260" height="52" rx="5" style="fill:var(--panel);stroke:var(--warn);stroke-dasharray:4 3"/>
    <text x="412" y="202" style="fill:var(--warn)">rw-p  匿名内存，约 3.8 MB</text>
    <text x="412" y="222" style="fill:var(--muted);font-size:12px">buffer 的其余部分，全是 0</text>
    <line x1="250" y1="62" x2="398" y2="53" style="stroke:var(--fg-2)" marker-end="url(#elf-arrow)"/>
    <line x1="250" y1="140" x2="398" y2="101" style="stroke:var(--fg-2)" marker-end="url(#elf-arrow)"/>
    <line x1="250" y1="155" x2="398" y2="149" style="stroke:var(--fg-2)" marker-end="url(#elf-arrow)"/>
    <line x1="250" y1="170" x2="398" y2="200" style="stroke:var(--warn);stroke-dasharray:4 3" marker-end="url(#elf-arrow)"/>
  </g>
</svg>
</figure>

- `main` 的地址落在第一行 `r-xp`（可读、可执行）的映射中，这是代码；
- 第二行 `r--p` 是可写段中的 GOT 等部分：动态链接器填好地址后，把它改成了只读，防止被恶意修改（`readelf -l` 中的 `GNU_RELRO` 段就是指这部分）；
- `counter` 和 `buffer` 的开头在第三行 `rw-p` 中；
- 第四行是一块**没有对应文件**的匿名内存，大小 0x3d0000，约 3.8 MB，就是 `buffer` 的其余部分。这就是 `.bss` 不占文件空间的实现方式：内核直接分配一块清零的内存；
- 再往后是堆（`[heap]`）、`libc.so.6`、动态链接器和栈（`[stack]`）。

地址都是 `0xbe03...` 这样的大数，并且每次运行都不同，这是**地址空间布局随机化**（ASLR）的效果，前面看到的 `pie`（位置无关的可执行文件）就是为了支持它。

## 7. 调试信息、strip 和静态链接

```bash
gcc -g hello.c -o hello_g
cp hello_g hello_s; strip hello_s
gcc -static hello.c -o hello_static
ls -l hello hello_g hello_s hello_static
```

```
-rwxr-xr-x 1 lucky lucky  70312 Oct 24 02:04 hello
-rwxr-xr-x 1 lucky lucky  71488 Oct 24 02:04 hello_g
-rwxr-xr-x 1 lucky lucky  67496 Oct 24 02:04 hello_s
-rwxr-xr-x 1 lucky lucky 639808 Oct 24 02:04 hello_static
```

- `-g` 加入了**调试信息**：`readelf -S hello_g` 中多了 6 个 `.debug_*` 节，记录了机器码和源代码行号、变量名的对应关系。GDB 靠它显示源代码（第 2.3 篇）。调试信息不会被加载到内存，不影响运行速度。
- `strip` 删除了符号表和调试信息，文件变小，但调试时就看不到函数名了。发布的程序常常是 strip 过的，前面 `file` 显示 `libc.so.6` 是 `stripped`。
- `-static` 是**静态链接**：把用到的库代码直接复制进可执行文件。文件大了约 9 倍，但不再依赖任何动态库，`ldd hello_static` 显示 `not a dynamic executable`。两种方式的取舍在下一篇讨论。

## 8. 工具汇总

| 工具 | 常用写法 | 用途 |
|---|---|---|
| `file` | `file prog` | 判断文件类型、架构、是否动态链接 |
| `readelf` | `-h` ELF 头，`-S` 节，`-l` 段，`-s` 符号，`-r` 重定位，`-d` 依赖 | 查看 ELF 的各个部分 |
| `objdump` | `-d` 反汇编，`-r` 显示重定位，`-S` 混合源代码（需要 `-g`） | 看机器码 |
| `nm` | `nm prog`，`nm -C` 还原 C++ 名字，`nm -D` 动态符号 | 查看符号，排查链接错误 |
| `size` | `size prog` | 各部分的大小 |
| `strip` | `strip prog` | 删除符号和调试信息 |
| `ldd` | `ldd prog` | 运行时依赖的动态库 |

这些工具都在 `binutils` 软件包中（`ldd` 属于 glibc）。macOS 上的可执行文件是另一种格式 Mach-O，对应的工具是 `otool` 和 `nm`。

## 9. 常见错误

1. **在另一种架构上运行程序**：在 x86-64 上编译的程序拿到 ARM 服务器上运行，会报 `cannot execute binary file: Exec format error`。用 `file` 看一下 `Machine`。
2. **以为全局数组越大，程序文件越大**：没有初始值的全局数组在 `.bss` 中，不占文件空间；有初始值的才占。
3. **修改字符串常量**：`char *s = "hi"; s[0] = 'H';` 会段错误，因为字符串在只读的 `.rodata` 中。要修改，写成 `char s[] = "hi";`。
4. **对着 strip 过的程序调试**：看不到函数名和行号。调试时用 `-g` 编译，并且不要 strip。
5. **链接错误时只盯着源代码**：用 `nm` 看看符号到底在哪个文件中定义、名字是否一致（C++ 的名字会被改编，`nm -C` 还原后对比）。

## 10. 小结

- ELF 是 Linux 上目标文件（relocatable）、可执行文件、动态库（shared object）、core 文件的统一格式，以 `7f 45 4c 46` 开头
- 节：`.text` 代码，`.data` 有初始值的全局变量，`.bss` 无初始值的全局变量（不占文件空间），`.rodata` 只读数据；局部变量在栈上，`malloc` 的内存在堆上
- 符号：`nm` 中 `T`、`D`、`B` 是定义，`U` 是未定义，小写是 `static` 的局部符号；链接错误就是符号找不到（undefined reference）或重复（multiple definition）
- 重定位：目标文件中调用外部函数的地址是 0，链接器根据重定位记录填上地址；调用动态库中的函数通过 PLT 和 GOT
- 段：加载器按段把文件映射到内存，代码段可读可执行，数据段可读可写，`.bss` 由匿名内存补上；`/proc/PID/maps` 可以看到结果
- `-g` 加调试信息，`strip` 去掉符号，`-static` 静态链接

## 11. 练习

1. 下面的变量分别在哪个节中？用 `nm` 验证。
   ```c
   int a;
   int b = 1;
   static int c = 0;
   const int d = 5;

   int use(void) { return c; }   // 使用 c，免得编译器警告它没有被使用
   ```
2. 把 `sections.c` 中的 `int buffer[1000000];` 改成 `int buffer[1000000] = {1};`，可执行文件会变成多大？
3. 有两个文件 `a.c` 和 `b.c`，`a.c` 中调用了 `void work(void)`，但链接时报 `undefined reference to 'work'`。怎样用 `nm` 找原因？

<details>
<summary>点击查看答案</summary>

1. 实测：

   ```
   0000000000000000 B a
   0000000000000000 D b
   0000000000000004 b c
   0000000000000000 R d
   0000000000000000 T use
   ```

   `a` 没有初始值，在 `.bss`；`b` 在 `.data`；`c` 的初始值是 0，和没有初始值一样放进 `.bss`，并且是 `static`，所以是小写的 `b`；`d` 是 `const` 的全局变量，在只读的 `.rodata` 中。

2. `buffer` 进入了 `.data`，4000000 字节都要存进文件：

   ```
      text	   data	    bss	    dec	    hex	filename
      1772	4000664	      8	4002444	 3d128c	sections
   -rwxr-xr-x 1 lucky lucky 4070552 Oct 24 02:06 sections
   ```

   `data` 从 656 变成了 4000664，`bss` 从 4000008 变成了 8；文件从 70568 字节变成了 4070552 字节，增加了约 4 MB。

3. 分别查看两个目标文件：

   ```bash
   nm a.o | grep work
   nm b.o | grep work
   ```

   `a.o` 中应该是 `U work`。如果 `b.o` 中**没有** `work`，说明没有定义它，或者名字拼错了；如果是小写的 `t work`，说明它被声明成了 `static`，其他文件看不到。实测把 `b.c` 写成 `static void work(void) {}`：

   ```
                    U work
   0000000000000000 t work
   ```

   链接报 ``a.c:(.text+0x8): undefined reference to `work'``。如果符号都对，再检查链接命令中是否漏了 `b.o`。

</details>
