---
title: 开发环境与编程工具
date: 2026-10-23 13:40:00
updated: 2026-10-23 13:40:00
categories:
  - C/C++
  - C++基础
tags:
  - C++基础
  - 工具
  - 调试
order: 0
description: 写代码之前，先把工具准备好。本文用实际的输出讲清楚源代码变成可执行文件的四个步骤，介绍各平台编译器的安装、常用编译选项、编辑器和 IDE 的选择，演示用 lldb/gdb 调试和定位崩溃，用 Make 和 CMake 构建多文件项目，以及 AddressSanitizer 等检查工具。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解预处理、编译、汇编、链接四个步骤，能分清编译错误和链接错误；在自己的电脑上装好编译器；会用命令行编译单个文件和多个文件；选好一个编辑器或 IDE；会用调试器设置断点、单步执行、查看变量、定位崩溃；会写简单的 Makefile 和 CMakeLists.txt；知道用 AddressSanitizer 检查内存错误。
>
> **前置知识**：无。这是 C++ 基础章节的第一篇，后面所有文章都会用到这里介绍的工具。
>
> **演示环境**：macOS + Apple clang 21，调试器是 lldb。Linux 和 Windows 上的对应做法会在文中给出。

## 1. 从源代码到可执行文件

C++ 是**编译型语言**：我们写的 `.cpp` 文件不能直接运行，要先由**编译器**翻译成机器能执行的程序。这个过程看起来只有一条命令：

```bash
g++ -std=c++17 hello.cpp -o hello
```

实际上它分为**四个步骤**。用下面这个程序，把每一步的产物都保存下来看一看：

```cpp
#include <iostream>

#define GREETING "Hello, C++"

int main()
{
    std::cout << GREETING << '\n';
    return 0;
}
```

### 1.1 预处理：`-E`

```bash
clang++ -std=c++17 -E hello.cpp -o hello.ii
```

预处理器处理所有以 `#` 开头的指令：把 `#include` 的头文件**原样复制进来**，把 `#define` 定义的宏**替换掉**。产物 `hello.ii` 的最后几行是：

```cpp
int main()
{
    std::cout << "Hello, C++" << '\n';
    return 0;
}
```

`GREETING` 已经被替换成了字符串。而整个文件有多长呢？

```
       9 hello.cpp
   64278 hello.ii
```

9 行的源文件，预处理之后变成了 **6 万多行**，几乎全部来自 `<iostream>` 及其包含的头文件。这就是 C++ 编译比较慢的原因之一（C++20 的模块就是为了解决这个问题，见 C++20 章节的《模块：告别 #include》）。

### 1.2 编译：`-S`

```bash
clang++ -std=c++17 -S hello.cpp -o hello.s
```

编译器检查语法和类型，把 C++ 代码翻译成**汇编代码**。`hello.s` 中 `main` 函数的开头是这样的（ARM64 汇编，在 x86 电脑上会不同）：

```
_main:                                  ; @main
	.cfi_startproc
; %bb.0:
	sub	sp, sp, #32
	stp	x29, x30, [sp, #16]             ; 16-byte Folded Spill
	add	x29, sp, #16
```

**语法错误、类型错误都在这一步报告**，也就是我们平时说的「编译错误」。

### 1.3 汇编：`-c`

```bash
clang++ -std=c++17 -c hello.cpp -o hello.o
```

汇编器把汇编代码翻译成机器码，产物是**目标文件**（`.o`，Windows 上是 `.obj`）。目标文件已经是二进制了，但还不能运行。用 `nm` 查看它包含的符号：

```bash
nm hello.o | grep -E "_main|cout"
```

```
                 U __ZNSt3__14coutE
0000000000000000 T _main
```

`T` 表示「这个文件**定义**了这个符号」，`U` 表示「这个文件**用到**了这个符号，但它定义在别处」。`std::cout` 定义在 C++ 标准库中，`hello.o` 只是留下了一个「需要它」的记录。

### 1.4 链接

```bash
clang++ hello.o -o hello
./hello
```

```
Hello, C++
```

**链接器**把一个或多个目标文件和库组合在一起，**为每个 `U` 找到它的定义**，生成最终的可执行文件。各步骤的产物大小：

| 文件 | 大小 | 内容 |
|---|---|---|
| `hello.cpp` | 116 字节 | 源代码 |
| `hello.ii` | 约 3 MB | 预处理后的源代码 |
| `hello.s` | 48 KB | 汇编代码 |
| `hello.o` | 9.3 KB | 目标文件（机器码） |
| `hello` | 37 KB | 可执行文件 |

平时一条 `g++ hello.cpp -o hello` 就自动完成了全部四步，中间文件不会保留。

### 1.5 编译错误和链接错误

理解了这四步，就能分清两类错误。准备一个由三个文件组成的小项目：

```cpp
// math_utils.h
#pragma once
int add(int a, int b);
int mul(int a, int b);

// math_utils.cpp
#include "math_utils.h"
int add(int a, int b) { return a + b; }
int mul(int a, int b) { return a * b; }

// main.cpp
#include <iostream>
#include "math_utils.h"

int main()
{
    std::cout << "add(2, 3) = " << add(2, 3) << '\n';
    std::cout << "mul(4, 5) = " << mul(4, 5) << '\n';
    return 0;
}
```

如果只编译 `main.cpp`：

```bash
clang++ -std=c++17 main.cpp -o app
```

```
Undefined symbols for architecture arm64:
  "add(int, int)", referenced from:
      _main in main-cc92de.o
  "mul(int, int)", referenced from:
      _main in main-cc92de.o
ld: symbol(s) not found for architecture arm64
```

注意最后一行的 **`ld`**，它是链接器。`main.cpp` 本身**编译成功了**（头文件中有 `add` 的声明，编译器知道怎么调用它），但链接时找不到 `add` 和 `mul` 的定义。在 GCC 上，同样的错误显示为 `undefined reference to 'add(int, int)'`。

| | 编译错误 | 链接错误 |
|---|---|---|
| 发生在 | 编译某一个 `.cpp` 时 | 把所有目标文件组合起来时 |
| 典型信息 | `error: ...`，带文件名和行号 | `Undefined symbols` / `undefined reference`、`duplicate symbol` / `multiple definition`，提到 `ld` |
| 常见原因 | 语法错误、类型不匹配、没有声明 | 漏编译了某个 `.cpp`、只声明没定义、同一个函数定义了两次 |

把所有的 `.cpp` 都交给编译器，就正确了：

```bash
clang++ -std=c++17 -Wall -Wextra main.cpp math_utils.cpp -o app
./app
```

```
add(2, 3) = 5
mul(4, 5) = 20
```

注意：**头文件不需要写在命令中**，它们通过 `#include` 被包含进 `.cpp`。

## 2. 安装编译器

主流的 C++ 编译器有三个：**GCC**（命令 `g++`）、**Clang**（命令 `clang++`）、**MSVC**（微软的编译器，命令 `cl`，随 Visual Studio 安装）。本系列的代码在 GCC 和 Clang 上都能编译。

| 系统 | 推荐方式 | 安装命令 |
|---|---|---|
| Windows | MSYS2 + MinGW-w64（GCC） | 安装 [MSYS2](https://www.msys2.org/) 后，在 MSYS2 UCRT64 终端中执行 `pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-gdb`，并把 `C:\msys64\ucrt64\bin` 加入 PATH |
| Windows | Visual Studio（MSVC） | 安装 Visual Studio Community，勾选「使用 C++ 的桌面开发」 |
| macOS | Xcode 命令行工具（Clang） | `xcode-select --install` |
| Ubuntu / Debian | GCC | `sudo apt install build-essential gdb` |
| Fedora | GCC | `sudo dnf install gcc-c++ gdb` |

安装完成后，打开一个**新的**终端，检查是否成功：

```bash
g++ --version
```

在我的 Mac 上输出的是：

```
Apple clang version 21.0.0 (clang-2100.3.34.2)
Target: arm64-apple-darwin27.0.0
```

一个有意思的细节：**macOS 上的 `g++` 其实是 Clang**，苹果为了兼容把它做成了 `clang++` 的别名。所以本系列中「`g++` / `clang++`」两种写法在 Mac 上是同一个编译器。在 Linux 上，`g++` 才是真正的 GCC。

如果提示 `command not found`（或者 Windows 上的「不是内部或外部命令」），通常是安装目录没有加入 **PATH** 环境变量，或者没有重新打开终端。

## 3. 常用编译选项

本系列文章开头的「编译环境」中经常出现这些选项：

| 选项 | 作用 |
|---|---|
| `-o 文件名` | 指定输出文件的名字。不写时默认是 `a.out`（Windows 上是 `a.exe`） |
| `-std=c++17` | 指定 C++ 标准版本，也可以是 `c++11`、`c++14`、`c++20` |
| `-Wall -Wextra` | 打开大部分警告。**一定要加** |
| `-g` | 生成调试信息，调试器才能显示源代码和变量名 |
| `-O0` / `-O2` | 优化级别。`-O0` 不优化（调试时用），`-O2` 是常用的发布级别优化 |
| `-c` | 只编译成目标文件，不链接 |
| `-I 目录` | 增加头文件的搜索目录 |
| `-fsanitize=address` | 打开 AddressSanitizer，检查内存错误（见第 7 节） |

### 3.1 为什么一定要开警告

下面的代码有三个 bug：

```cpp
#include <iostream>

int sum(int n)
{
    int total;                           // 忘了初始化
    for (int i = 1; i <= n; ++i) {
        total += i;
    }
    return total;
}

int main()
{
    unsigned int count = 3;
    for (int i = 0; i < count; ++i) {    // 有符号与无符号比较
        std::cout << i << ' ';
    }
    if (count = 0) {                     // 本意是 ==
        std::cout << "空\n";
    }
    std::cout << sum(10) << '\n';
}
```

不加任何选项编译，只有一个警告：

```
warn.cpp:18:15: warning: using the result of an assignment as a condition without parentheses [-Wparentheses]
```

加上 `-Wall -Wextra`，三个问题都被找出来了：

```
warn.cpp:7:9: warning: variable 'total' is uninitialized when used here [-Wuninitialized]
warn.cpp:18:15: warning: using the result of an assignment as a condition without parentheses [-Wparentheses]
warn.cpp:15:23: warning: comparison of integers of different signs: 'int' and 'unsigned int' [-Wsign-compare]
```

而程序的运行结果是：

```
0 1 2 37946819
```

1 到 10 的和应该是 55，却输出了一个垃圾值，因为 `total` 没有初始化。**警告往往就是 bug**。养成习惯：始终加上 `-Wall -Wextra`，并且把警告当作错误一样修复。（还可以加上 `-Werror`，让所有警告都变成错误，强制自己修复。）

## 4. 编辑器和 IDE

编译器只负责编译，写代码还需要一个编辑器。常见的选择：

| 工具 | 类型 | 平台 | 特点 |
|---|---|---|---|
| **VS Code** | 编辑器 + 扩展 | 全平台 | 免费、轻量，需要自己安装编译器并配置扩展。最流行的选择 |
| **CLion** | IDE | 全平台 | JetBrains 出品，代码分析和重构很强，基于 CMake。学生可以申请免费授权 |
| **Visual Studio** | IDE | Windows | 微软出品，自带 MSVC 编译器和优秀的调试器，安装即用 |
| **Xcode** | IDE | macOS | 苹果出品，自带 Clang |
| Vim / Neovim / Emacs | 编辑器 | 全平台 | 学习曲线陡峭，熟练后效率很高 |

给初学者的建议：

- **Windows 上，想要开箱即用**：安装 Visual Studio Community，新建一个「控制台应用」项目就可以写代码、运行、调试。
- **想要一个跨平台、通用的环境**：VS Code + 第 2 节安装的编译器。安装扩展 **C/C++**（微软官方，提供代码提示和调试）或者 **clangd**（代码提示更准确），在 macOS 上调试可以再装 **CodeLLDB**。
- **无论用哪个 IDE，都要学会第 1、3 节的命令行编译**。IDE 的「运行」按钮背后执行的就是这些命令，出了问题时，理解命令行才知道去哪里排查。

IDE 不是越复杂越好。对初学者来说，最重要的三个功能是：**代码高亮和错误提示**、**一键编译运行**、**图形化的调试**。

## 5. 调试器

程序结果不对时，很多人的第一反应是到处加 `std::cout` 打印变量。这有时候有效，但**调试器**能做得更多：让程序停在任意一行，一步一步地执行，随时查看所有变量的值，程序崩溃时直接告诉你崩溃在哪一行。

常用的调试器是 **gdb**（GCC 配套，Linux / Windows MinGW）和 **lldb**（LLVM 配套，macOS 默认）。VS Code、CLion 的图形化调试，背后用的也是它们。

### 5.1 用断点和单步找到 bug

下面的程序计算平均分，三个分数的平均值应该是 84.33，输出却是 84：

```cpp
#include <iostream>
#include <vector>

double average(const std::vector<int>& scores)
{
    int total = 0;
    for (int s : scores) {
        total += s;
    }
    return total / scores.size();
}

int main()
{
    std::vector<int> scores{90, 85, 78};
    double avg = average(scores);
    std::cout << "平均分: " << avg << '\n';
    return 0;
}
```

```
平均分: 84
```

调试时要用 **`-g` 生成调试信息，用 `-O0` 关闭优化**（优化后的代码顺序会被打乱，变量也可能被优化掉）：

```bash
clang++ -std=c++17 -g -O0 avg.cpp -o avg
lldb ./avg
```

进入 lldb 之后，在 `average` 函数上设置断点，然后运行：

```
(lldb) b average
Breakpoint 1: where = avg`average(...) + 16 at avg.cpp:6:9
(lldb) run
Process 41567 stopped
* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1
      frame #0: avg`average(scores=size=3) at avg.cpp:6:9
   3
   4   	double average(const std::vector<int>& scores)
   5   	{
-> 6   	    int total = 0;
    	        ^
   7   	    for (int s : scores) {
   8   	        total += s;
   9   	    }
```

程序停在了第 6 行，箭头 `->` 指向**即将执行**的那一行。用 `n`（next）单步执行，`p`（print）查看变量：

```
(lldb) n
-> 7   	    for (int s : scores) {
(lldb) n
-> 8   	        total += s;
(lldb) p total
(int) 0
(lldb) n
-> 7   	    for (int s : scores) {
(lldb) n
-> 8   	        total += s;
(lldb) p total
(int) 90
```

（为了简洁，这里省略了每一步打印的上下文代码。）循环执行了一次，`total` 变成了 90，符合预期。不用一直单步，在第 10 行再设一个断点，用 `c`（continue）直接运行到那里：

```
(lldb) b avg.cpp:10
(lldb) c
-> 10  	    return total / scores.size();
(lldb) frame variable
(const std::vector<int> &) scores = 0x... size=3: {
  [0] = 90
  [1] = 85
  [2] = 78
}
(int) total = 253
(lldb) bt
* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 2.1
  * frame #0: avg`average(scores=size=3) at avg.cpp:10:12
    frame #1: avg`main at avg.cpp:16:18
    frame #2: dyld`start + 6688
```

- `frame variable` 列出当前函数中所有的变量：`total = 253` 是对的。
- `bt`（backtrace）显示**调用栈**：当前在 `average` 中，它是被 `main` 的第 16 行调用的。

既然总和是对的，问题就出在除法上。调试器中可以直接计算表达式：

```
(lldb) p total / scores.size()
(std::vector<int>::size_type) 84
(lldb) p total / (double)scores.size()
(double) 84.333333333333328
```

原因找到了：`total` 是 `int`，`scores.size()` 是无符号整数，**整数除以整数，结果还是整数**，小数部分被丢掉了。修复方法是先转换为 `double`：

```cpp
return static_cast<double>(total) / scores.size();
```

```
平均分: 84.3333
```

### 5.2 定位崩溃

下面的程序遍历一个链表，但是忘了在到达末尾时停下来：

```cpp
#include <iostream>
#include <string>

struct Node {
    std::string name;
    Node* next;
};

void printAll(const Node* head)
{
    for (const Node* p = head; ; p = p->next) {     // 忘了检查 p 是否为空
        std::cout << p->name << '\n';
    }
}

int main()
{
    Node c{"C", nullptr};
    Node b{"B", &c};
    Node a{"A", &b};
    printAll(&a);
    return 0;
}
```

在终端中运行，输出 `A`、`B`、`C` 之后程序崩溃，shell 会提示 segmentation fault（段错误），退出码是 139。如果把输出重定向到文件：

```bash
./crash > out.txt
```

`out.txt` 是**空的**（0 字节）。输出到文件时，`std::cout` 会先把内容攒在缓冲区中，程序崩溃时缓冲区中的内容来不及写出就丢失了（《字符串与 IO 流》中会详细讲缓冲区）。所以**靠打印来定位崩溃并不可靠**。

用调试器运行，崩溃时程序会停下来：

```
(lldb) run
A
B
C
Process 41706 stopped
* thread #1, queue = 'com.apple.main-thread', stop reason = EXC_BAD_ACCESS (code=1, address=0x17)
      frame #0: crash`std::__1::basic_string<...>::__is_long() const at string:2153:23
(lldb) bt
* thread #1, queue = 'com.apple.main-thread', stop reason = EXC_BAD_ACCESS (code=1, address=0x17)
  * frame #0: crash`std::__1::basic_string<...>::__is_long() const at string:2153:23
    frame #1: crash`std::__1::basic_string<...>::__get_pointer() const at string:2275:12
    frame #2: crash`std::__1::basic_string<...>::data() const at string:1813:30
    frame #3: crash`std::__1::operator<<(...) at basic_ostream.h:577:52
    frame #4: crash`printAll(head=0x...) at crash.cpp:12:19
    frame #5: crash`main at crash.cpp:21:5
```

`EXC_BAD_ACCESS`（Linux 上是 `SIGSEGV`）表示访问了非法的内存地址。崩溃发生在标准库 `std::string` 的内部，但这不是标准库的 bug。**沿着调用栈往下找，第一个属于自己代码的位置就是问题所在**：frame #4，`crash.cpp` 的第 12 行。切换到那一层查看变量：

```
(lldb) frame select 4
frame #4: crash`printAll(head=0x...) at crash.cpp:12:19
-> 12  	        std::cout << p->name << '\n';
(lldb) p p
(const Node *) nullptr
```

`p` 是空指针，对它执行 `p->name` 导致了崩溃。循环条件应该写成 `p != nullptr`。

### 5.3 gdb 和 lldb 常用命令对照

| 功能 | gdb | lldb |
|---|---|---|
| 启动 | `gdb ./prog` | `lldb ./prog` |
| 运行 | `run` / `r` | `run` / `r` |
| 在函数上设置断点 | `b average` | `b average` |
| 在某一行设置断点 | `b avg.cpp:10` | `b avg.cpp:10` |
| 单步执行（不进入函数） | `next` / `n` | `next` / `n` |
| 单步执行（进入函数） | `step` / `s` | `step` / `s` |
| 运行到函数返回 | `finish` | `finish` |
| 继续运行到下一个断点 | `continue` / `c` | `continue` / `c` |
| 打印变量或表达式 | `p total` | `p total` |
| 显示所有局部变量 | `info locals` | `frame variable` / `v` |
| 调用栈 | `bt` | `bt` |
| 切换到第 n 层 | `frame 4` | `frame select 4` / `f 4` |
| 退出 | `quit` / `q` | `quit` / `q` |

两者的常用命令几乎一样。在 VS Code 或 IDE 中，这些操作都对应着按钮：点击行号左边设置断点，按 F5 开始调试，F10 单步，F11 进入函数，鼠标悬停查看变量。**原理和命令行完全相同**。

## 6. 构建工具：Make 和 CMake

项目有几十个 `.cpp` 文件时，每次都手动输入完整的编译命令很麻烦；而且只改了一个文件，也没有必要全部重新编译。**构建工具**就是用来解决这些问题的。

### 6.1 Make

**Make** 根据一个叫 `Makefile` 的文件来构建项目。为第 1.5 节的小项目写一个 Makefile：

```makefile
CXX      := clang++
CXXFLAGS := -std=c++17 -Wall -Wextra -g

app: main.o math_utils.o
	$(CXX) $^ -o $@

main.o: main.cpp math_utils.h
	$(CXX) $(CXXFLAGS) -c main.cpp -o main.o

math_utils.o: math_utils.cpp math_utils.h
	$(CXX) $(CXXFLAGS) -c math_utils.cpp -o math_utils.o

clean:
	rm -f app *.o
```

每条规则的格式是：

```makefile
目标: 依赖1 依赖2 ...
	生成目标的命令            # 注意：这一行必须以 Tab 开头，不能用空格
```

`$^` 表示所有依赖，`$@` 表示目标。在项目目录中执行 `make`：

```
clang++ -std=c++17 -Wall -Wextra -g -c main.cpp -o main.o
clang++ -std=c++17 -Wall -Wextra -g -c math_utils.cpp -o math_utils.o
clang++ main.o math_utils.o -o app
```

什么都不改，再执行一次：

```
make: `app' is up to date.
```

修改 `math_utils.cpp` 之后再执行：

```
clang++ -std=c++17 -Wall -Wextra -g -c math_utils.cpp -o math_utils.o
clang++ main.o math_utils.o -o app
```

Make 比较文件的修改时间，**只重新编译发生了变化的部分**：`main.o` 没有重新编译。对于大项目，这能节省大量时间。这也是第 1 节把编译和链接分开的实际意义。

### 6.2 CMake

Makefile 的问题是：要手动写出每个文件的依赖关系；而且它主要用于类 Unix 系统，Visual Studio 用的是另一套项目文件。**CMake** 是目前 C++ 项目事实上的标准构建工具，它不直接编译代码，而是**根据一个描述文件生成** Makefile、Visual Studio 项目或者其他构建系统的文件。

同一个项目的 `CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.16)
project(calc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(app main.cpp math_utils.cpp)
target_compile_options(app PRIVATE -Wall -Wextra)
```

构建的命令：

```bash
cmake -S . -B build                          # 生成构建文件，放在 build 目录中
cmake --build build                          # 编译
./build/app                                  # 运行
```

- 只需要列出源文件，**头文件的依赖关系由 CMake 自动分析**。
- 所有生成的文件都放在 `build` 目录中，不会弄乱源代码目录。要从头构建，删掉 `build` 目录即可。
- 用 `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug` 生成带调试信息的版本，`Release` 生成优化的版本。
- CLion 直接打开含有 `CMakeLists.txt` 的目录就能识别项目；VS Code 安装 CMake Tools 扩展后也可以。

> `target_compile_options` 中的 `-Wall -Wextra` 是 GCC 和 Clang 的写法，MSVC 对应的是 `/W4`。

学习阶段的单文件练习，直接用命令行编译就够了；当项目超过三四个文件，或者要使用第三方库时，就应该用 CMake 了。

## 7. 检查工具

有些 bug 不会导致编译错误，也不一定导致崩溃，程序「看起来」运行正常。下面这段代码多循环了一次：

```cpp
#include <iostream>
#include <vector>

int main()
{
    std::vector<int> v{1, 2, 3};
    int sum = 0;
    for (std::size_t i = 0; i <= v.size(); ++i) {   // <= 多循环了一次
        sum += v[i];
    }
    std::cout << "sum = " << sum << '\n';
    return 0;
}
```

用 `-Wall -Wextra` 编译没有任何警告，运行结果是：

```
sum = 6
```

结果「正确」，只是因为越界读到的那块内存恰好是 0。换一台机器、换一个编译选项，结果就可能不同。加上 **AddressSanitizer**（简称 ASan）：

```bash
clang++ -std=c++17 -g -fsanitize=address oob.cpp -o oob && ./oob
```

```
ERROR: AddressSanitizer: heap-buffer-overflow on address 0x60200000095c
READ of size 4 at 0x60200000095c thread T0
    #0 0x0001020e4af0 in main oob.cpp:9
0x60200000095c is located 0 bytes after 12-byte region [0x602000000950,0x60200000095c)
```

ASan 立即发现了问题：第 9 行读取了一个 12 字节（3 个 `int`）的内存块**后面紧挨着的位置**，也就是堆缓冲区溢出（heap-buffer-overflow）。

本系列中经常用到这些检查工具：

| 工具 | 用法 | 能发现的问题 |
|---|---|---|
| **AddressSanitizer** | 编译时加 `-fsanitize=address -g` | 越界访问、使用已释放的内存、重复释放；在 Linux 上还能检查内存泄漏 |
| **UndefinedBehaviorSanitizer** | 编译时加 `-fsanitize=undefined -g` | 有符号整数溢出、除以 0、非法的类型转换等未定义行为 |
| **ThreadSanitizer** | 编译时加 `-fsanitize=thread -g` | 多线程的数据竞争 |
| **leaks**（macOS） | `leaks --atExit -- ./prog` | 内存泄漏 |
| **Valgrind**（Linux） | `valgrind --leak-check=full ./prog` | 内存泄漏、越界、未初始化的值，不需要重新编译，但运行慢几十倍 |
| **nm** | `nm -C prog` | 查看目标文件和程序中的符号，排查链接错误 |

Sanitizer 会让程序变慢、占用更多内存，所以只在开发和测试时打开。建议**写练习时默认加上 `-fsanitize=address,undefined`**，很多隐藏的 bug 会立即暴露出来。（Windows 上的 MSVC 支持 `/fsanitize=address`；MinGW 版本的 GCC 目前对 Sanitizer 的支持有限，可以在 WSL 中使用。）

## 8. 在线工具

- **[Compiler Explorer](https://godbolt.org/)**：在浏览器中编写代码，实时查看各种编译器、各种优化级别生成的汇编代码，也可以运行程序。想快速验证一小段代码，或者比较 GCC 和 Clang 的行为时非常方便。
- **[cppreference.com](https://zh.cppreference.com/)**：最权威、最常用的 C++ 语言和标准库参考，每个函数都标注了从哪个标准版本开始支持，并且有示例代码。遇到不熟悉的标准库函数，第一个查的就应该是它。

## 9. 常见错误

1. **找不到编译器**：`command not found`，检查 PATH，安装后要重新打开终端。
2. **分不清编译错误和链接错误**：看到 `ld`、`Undefined symbols`、`undefined reference`，就去检查是否漏掉了某个 `.cpp`，或者函数只声明没有定义。
3. **把头文件写进编译命令，或者 `#include` 一个 `.cpp` 文件**：前者没有必要，后者会导致重复定义。
4. **不开警告**：`-Wall -Wextra` 能发现很多 bug，而且不需要任何代价。
5. **调试时没有加 `-g`，或者开了优化**：调试器看不到源代码和变量，或者执行顺序看起来是乱的。
6. **用打印来定位崩溃**：输出可能还在缓冲区中，崩溃时丢失。用调试器看调用栈。
7. **看到崩溃发生在标准库内部，就以为是标准库的 bug**：沿着调用栈找到第一个属于自己代码的位置。
8. **Makefile 中的命令行用空格缩进**：报错 `missing separator`，必须用 Tab。

## 10. 小结

- 源代码变成可执行文件要经过四步：**预处理**（展开 `#include` 和宏）→ **编译**（生成汇编）→ **汇编**（生成目标文件）→ **链接**（解析符号，生成可执行文件）
- 编译错误发生在单个 `.cpp` 中；链接错误发生在组合目标文件时，提示 `Undefined symbols` / `undefined reference`
- Windows 用 MSYS2 或 Visual Studio，macOS 用 `xcode-select --install`，Linux 用包管理器安装编译器；macOS 上的 `g++` 实际是 Clang
- 始终加上 `-Wall -Wextra`：一个不加警告就看不出来的未初始化变量，让 1 到 10 的和变成了 37946819
- 调试器可以设置断点、单步执行、查看变量和调用栈；调试时用 `-g -O0`；定位崩溃要看调用栈中第一个属于自己代码的位置
- Make 只重新编译修改过的文件；CMake 是 C++ 项目的标准构建工具
- AddressSanitizer 能发现「看起来运行正常」的内存错误

## 11. 练习

1. 写一个只包含 `#include <cstdio>` 和 `printf` 的程序，用 `-E` 查看预处理后有多少行，和第 1.1 节 `<iostream>` 的 64278 行比较。
2. 第 1.5 节的项目中，如果把 `math_utils.cpp` 中 `mul` 函数的定义删掉，`main.cpp` 中仍然调用它，会得到编译错误还是链接错误？如果把 `math_utils.h` 中 `mul` 的声明删掉呢？
3. 在你自己的电脑上安装编译器，用调试器（命令行或者 IDE 都可以）重现第 5.1 节的调试过程：在 `average` 中设置断点，单步执行循环，观察 `total` 的变化。

<details>
<summary>点击查看答案</summary>

1. 在我的环境中是 711 行，比 `<iostream>` 少了将近 90 倍。`<cstdio>` 是 C 语言的输入输出库，比基于模板和类的 `<iostream>` 简单得多。具体的行数和编译器、标准库的版本有关。

2. 删掉 `mul` 的**定义**：`main.cpp` 中仍然有声明，可以正常编译，链接时报 `Undefined symbols: "mul(int, int)"`，是**链接错误**。删掉 `mul` 的**声明**：编译 `main.cpp` 时编译器不知道 `mul` 是什么，报 `error: use of undeclared identifier 'mul'`，是**编译错误**。

3. 没有标准答案。如果使用 VS Code，在第 8 行的行号左边点击设置断点，按 F5 启动调试，在左侧的「变量」面板中可以看到 `total` 和 `scores` 的值，按 F10 单步执行。如果断点没有生效，检查编译命令中是否有 `-g`。

</details>
