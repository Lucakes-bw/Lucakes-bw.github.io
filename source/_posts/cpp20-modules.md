---
title: 模块：告别 #include
date: 2026-10-27 09:00:00
updated: 2026-10-27 09:00:00
categories:
  - C/C++
  - C++20
tags:
  - C++20
  - 模块
  - 编译
order: 5
description: '#include 的本质是文本复制，一个两行的 .cpp 预处理后有九万多行，宏还会到处泄漏。C++20 的模块只编译一次、只导出你想导出的名字。本文从零搭建一个模块工程，实测 20 个文件的编译时间从 4.7 秒降到 0.7 秒，并讲清楚各编译器的用法和现状。'
disableNunjucks: true
---

> **本文目标**：理解 `#include` 的问题；会写模块接口单元，用 `export` 控制导出内容；会在模块中使用传统头文件（全局模块片段）；会把模块拆成接口和实现；会用 Clang 编译模块工程；了解模块在各编译器和构建工具中的支持现状。
>
> **编译环境**：本文用 Apple Clang 21 编译，需要加 `-fcxx-modules`（见第 6 节）。模块是 C++20 中工具链支持最晚的特性，各编译器的命令差别很大。

## 1. #include 的问题

`#include` 在 C 语言时代就存在了，它做的事情非常简单：**把头文件的内容原样复制到当前位置**。这带来了几个长期困扰 C++ 程序员的问题。

### 1.1 编译慢：同样的代码被反复解析

写一个只有两行的 `.cpp` 文件，它包含了一个头文件，头文件里又包含了一些常用的标准库头文件（`<regex>`、`<iostream>`、`<map>`、`<vector>` 等）：

```cpp
#include "heavy.h"
int use1() { return countWords(normalize("Hello World 1")); }
```

用 `clang++ -E` 查看预处理之后的结果：**92750 行**。编译器实际要处理的，是这九万多行代码。

一个项目有 20 个这样的 `.cpp` 文件，同样的九万行就要被解析 20 遍。大型项目中，编译时间的大部分都花在了反复解析相同的头文件上。

### 1.2 宏和内部细节会泄漏

头文件中定义的宏，会影响包含它的每一个文件。最臭名昭著的例子是 Windows 的 `<windows.h>` 定义了 `min` 和 `max` 宏，导致 `std::min(a, b)` 无法编译。头文件中的辅助函数、`using namespace`，也会原封不动地暴露给使用者。

### 1.3 包含顺序会影响结果

先包含 A 再包含 B，和先包含 B 再包含 A，结果可能不同（因为宏）。还需要 `#pragma once` 或 include guard 防止重复包含。

## 2. 第一个模块

C++20 的**模块**（module）用一种全新的方式组织代码：模块被**编译一次**，生成一个二进制的接口文件；使用者通过 `import` 导入，直接读取这个已经编译好的接口，而不是重新解析源代码。

### 2.1 模块接口文件

```cpp
// geometry.cppm：模块接口文件
module;                                  // 全局模块片段：只能在这里 #include 传统头文件
#include <cmath>
#include <string>

export module geometry;                  // 声明这是 geometry 模块的接口

#define GEOMETRY_VERSION 2               // 模块内部的宏，不会泄漏给导入者

double square(double x) { return x * x; }   // 没有 export：只在模块内部可见

export double distance(double x1, double y1, double x2, double y2)
{
    return std::sqrt(square(x2 - x1) + square(y2 - y1));
}

export class Circle {
public:
    explicit Circle(double r) : r_(r) {}
    double area() const { return 3.14159265358979 * square(r_); }
    std::string describe() const { return "半径为 " + std::to_string(r_) + " 的圆"; }
private:
    double r_;
};

export namespace geometry {              // 也可以整个命名空间一起导出
    constexpr double pi = 3.14159265358979;
    int version() { return GEOMETRY_VERSION; }
}
```

逐行说明：

- **`module;`**：开始**全局模块片段**。模块中如果需要使用传统的头文件（比如标准库的 `<cmath>`），必须在这里 `#include`，它们不属于这个模块。
- **`export module geometry;`**：声明这个文件是名为 `geometry` 的模块的**接口单元**。从这一行开始，后面的代码都属于这个模块。
- **`export`**：只有标记了 `export` 的声明，才对导入这个模块的代码可见。可以导出函数、类、变量、模板、命名空间。
- **没有 `export` 的 `square`**：模块内部可以随意使用，外部完全看不到。
- 模块接口文件的扩展名没有统一标准：Clang 习惯用 `.cppm`，MSVC 用 `.ixx`，GCC 对扩展名没有要求。

### 2.2 使用模块

```cpp
// main.cpp：使用模块
#include <iostream>
import geometry;

int main()
{
    std::cout << "distance = " << distance(0, 0, 3, 4) << '\n';
    Circle c(2);
    std::cout << c.describe() << "，面积 " << c.area() << '\n';
    std::cout << "pi = " << geometry::pi << ", version = " << geometry::version() << '\n';
    return 0;
}
```

### 2.3 编译和运行

模块必须**先于**使用它的文件编译。用 Clang 分四步：

```bash
# 1. 预编译模块接口，生成二进制的模块文件 geometry.pcm
clang++ -std=c++20 -fcxx-modules --precompile geometry.cppm -o geometry.pcm
# 2. 编译使用者，告诉编译器到当前目录查找预编译好的模块
clang++ -std=c++20 -fcxx-modules -fprebuilt-module-path=. -c main.cpp -o main.o
# 3. 把模块中的函数实现编译成目标文件
clang++ -std=c++20 -fcxx-modules -c geometry.pcm -o geometry.o
# 4. 链接
clang++ main.o geometry.o -o app
```

运行结果：

```
distance = 5
半径为 2.000000 的圆，面积 12.5664
pi = 3.14159, version = 2
```

### 2.4 导出控制与宏隔离

尝试访问没有导出的函数和模块内部的宏：

```cpp
import geometry;
int main()
{
    double a = square(3.0);          // 错误：square 没有被导出
    int v = GEOMETRY_VERSION;        // 错误：模块中的宏不会泄漏
    return (int)a + v;
}
```

```
error: use of undeclared identifier 'square'
error: use of undeclared identifier 'GEOMETRY_VERSION'
```

两个都无法访问。**模块只导出你用 `export` 标记的名字，宏永远不会被导出。** 这彻底解决了 `#include` 的「泄漏」问题：模块的作者可以随意使用辅助函数和宏，而不用担心影响使用者。同样，**`import` 的顺序也不会影响结果**，因为导入的模块之间互不干扰。

## 3. 实测：编译时间

用一个模拟的「重量级」库来对比。`heavy.h` 包含了 `<regex>`、`<iostream>`、`<map>` 等 8 个标准库头文件，声明了两个函数；再准备一个功能完全相同的模块 `heavy.cppm`。然后各写 20 个 `.cpp` 文件，分别用 `#include "heavy.h"` 和 `import heavy;` 使用它，每个文件只有两行，依次编译，统计总时间：

```
第 1 次: #include 4.74 秒；模块 预编译 0.45 秒 + 20 个文件 0.30 秒
第 2 次: #include 4.74 秒；模块 预编译 0.43 秒 + 20 个文件 0.31 秒
```

| 方式 | 总耗时 |
| --- | --- |
| `#include` | 约 4.7 秒（每个文件约 0.24 秒） |
| 模块 | 约 0.75 秒（预编译一次 0.44 秒，每个文件约 0.015 秒） |

用 `#include` 时，每个文件都要把九万多行代码重新解析一遍；用模块时，那些标准库头文件只在预编译模块时解析**一次**，20 个使用者只需要读取已经编译好的接口，而且接口中只导出了两个函数，`<regex>` 等头文件的内容根本不会进入使用者的编译过程。**文件越多，模块的优势越大。**

> 这个对比对 `#include` 稍微有些不公平：设计良好的头文件应该只包含真正需要的头文件（`heavy.h` 中只需要 `<string>`，`<regex>` 等可以放到 `.cpp` 中）。但在真实项目中，头文件互相包含、越来越重是非常普遍的现象，模块从机制上避免了这个问题。

预编译好的 `geometry.pcm` 有 11MB，因为它包含了全局模块片段中 `<cmath>`、`<string>` 的完整内容。模块文件是编译器内部的格式，**不能分发给使用不同编译器或不同编译选项的人**，发布库时仍然要提供模块的源代码。

## 4. 接口与实现分离

和头文件 + 源文件一样，模块也可以把**接口**（声明）和**实现**（定义）放在不同的文件中。修改实现时，导入这个模块的代码不需要重新编译。

```cpp
// logger.cppm：接口单元，只放声明
module;
#include <string>
export module logger;

export void logInfo(const std::string& msg);
export int logCount();
```

```cpp
// logger.cpp：实现单元，不写 export
module;
#include <iostream>
#include <string>
module logger;                           // 属于 logger 模块，自动看到接口中的声明

namespace { int counter = 0; }

void logInfo(const std::string& msg)
{
    ++counter;
    std::cout << "[INFO #" << counter << "] " << msg << '\n';
}
int logCount() { return counter; }
```

```cpp
import logger;
#include <string>
int main()
{
    logInfo("程序启动");
    logInfo("处理完成");
    return logCount() == 2 ? 0 : 1;
}
```

编译：

```bash
clang++ -std=c++20 -fcxx-modules --precompile logger.cppm -o logger.pcm
clang++ -std=c++20 -fcxx-modules -fprebuilt-module-path=. -c logger.pcm -o logger_if.o
clang++ -std=c++20 -fcxx-modules -fprebuilt-module-path=. -c logger.cpp -o logger_impl.o
clang++ -std=c++20 -fcxx-modules -fprebuilt-module-path=. -c main.cpp -o main.o
clang++ main.o logger_if.o logger_impl.o -o app
```

运行结果：

```
[INFO #1] 程序启动
[INFO #2] 处理完成
```

- 实现单元以 **`module logger;`**（没有 `export`）开头，表示它属于 `logger` 模块，能看到接口单元中的所有声明（包括没有导出的）。
- `<iostream>` 只在实现单元中包含，**使用者完全不需要解析它**。

模块还可以拆分成多个**分区**（partition），用 `export module logger:format;` 这样的语法，适合很大的模块，这里不展开。

## 5. 常见错误

1. **编译顺序错误**：使用模块的文件必须在模块预编译之后才能编译。头文件没有这个限制，所以模块对**构建系统**提出了新的要求：必须先分析出文件之间的依赖关系，再按顺序编译。
2. **在 `export module` 之后才 `#include`**：标准库头文件应该放在 `module;` 开始的全局模块片段中。放在模块声明之后，头文件的内容会被当作模块的一部分，可能导致重复定义等问题。
3. **以为 `import` 会把宏也带过来**：模块不导出宏。需要宏的库（比如很多 C 库），仍然要用 `#include`。
4. **混用不同编译选项生成的模块文件**：影响语言特性的选项必须一致。实测用 `-std=c++20` 生成的 `.pcm`，在 `-std=c++23` 的编译中导入会报错 `C++23 was disabled in precompiled file ... but is currently enabled`；开启异常生成、在 `-fno-exceptions` 下导入也会报错。优化级别（`-O0` / `-O2`）和普通的 `-D` 宏定义不同则不会报错。不同编译器、不同编译器版本之间的模块文件完全不能共用。

## 6. 编译器和工具支持现状

模块是 C++20 中**支持得最晚、各家差异最大**的特性：

| 工具 | 支持情况 |
| --- | --- |
| **MSVC** | 支持得最早也最完整（VS2019 16.10 起），接口文件用 `.ixx`，Visual Studio 中直接可用 |
| **GCC** | GCC 11 起支持，需要加 `-fmodules-ts`（GCC 15 起可以用 `-fmodules`） |
| **Clang** | Clang 16 起比较完整，用 `--precompile` 生成 `.pcm`，再用 `-fprebuilt-module-path` 或 `-fmodule-file` 指定位置 |
| **Apple Clang** | 本文实测：默认**不识别** `export module` 语法，报错 `expected template`；加上 `-fcxx-modules` 后可以正常使用 |
| **CMake** | 3.28 起正式支持 C++20 模块（需要 Ninja 或 Visual Studio 生成器），会自动处理编译顺序 |
| **`import std;`** | 一次导入整个标准库，是 **C++23** 的特性，MSVC 和较新的 Clang/libc++、GCC 15 开始支持 |

实际项目中，不建议手写上面那样的编译命令，而是使用 CMake 3.28+ 这样能自动扫描模块依赖的构建工具：

```cmake
cmake_minimum_required(VERSION 3.28)
project(demo CXX)
set(CMAKE_CXX_STANDARD 20)
add_executable(app main.cpp)
target_sources(app PUBLIC FILE_SET CXX_MODULES FILES geometry.cppm)
```

## 7. 进阶：模块会取代头文件吗

长期来看会，但这是一个漫长的过程：

- 大量已有的库（包括很多 C 库和系统 API）只提供头文件，不会很快改写成模块。模块可以通过全局模块片段使用它们，两者能够共存。
- C++20 还提供了**头文件单元**（header unit）：`import <vector>;` 把一个传统头文件当作模块导入，可以获得部分编译速度的收益，但编译器支持程度不一。
- 构建工具、IDE、代码分析工具需要时间适配模块。

对于新项目，如果你使用 MSVC，或者使用较新的 Clang/GCC 加 CMake 3.28+，已经可以在项目内部的代码中使用模块了。

## 8. 小结

- `#include` 是文本复制：同样的头文件被反复解析（两行的文件预处理后九万多行），宏和内部细节会泄漏，包含顺序会影响结果
- 模块接口单元：`export module 名字;`，用 `export` 标记要导出的声明；传统头文件放在 `module;` 开始的全局模块片段中
- 使用者 `import 名字;`，只能看到导出的名字，**宏永远不会被导出**，导入顺序无关
- 实测 20 个文件：`#include` 约 4.7 秒，模块约 0.75 秒；文件越多优势越大
- 可以分成接口单元（`export module m;`）和实现单元（`module m;`），修改实现不影响使用者
- 模块对构建顺序有要求；各编译器命令差异大（Apple Clang 需要 `-fcxx-modules`），实际项目用 CMake 3.28+

## 9. 练习

1. 把 C++11 章节《可变参数模板》中的 `print` 函数做成一个模块 `printer`，导出 `print`，在 `main.cpp` 中导入使用。（提示：模板可以直接导出，模板的定义写在接口文件中。）
2. 在一个模块中定义一个没有导出的类 `Detail`，再导出一个返回 `Detail` 对象的函数 `make()`。使用者能否调用 `make()`？能否写 `Detail d = make();`？能否写 `auto d = make();`？
3. 为什么说「模块对构建系统提出了新要求」？用头文件时，20 个 `.cpp` 文件可以以任意顺序、并行编译；用模块时呢？

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   // printer.cppm
   module;
   #include <iostream>
   export module printer;

   export template <typename... Args>
   void print(const Args&... args)
   {
       ((std::cout << args << ' '), ...);
       std::cout << '\n';
   }
   ```

   ```cpp
   // main.cpp
   import printer;
   int main() { print(1, "two", 3.0); }
   ```

2. 能调用 `make()`，也能写 `auto d = make();` 并调用 `d` 的成员函数：类型 `Detail` 本身是「可达的」（reachable），只是它的**名字**不可见。所以 `Detail d = make();` 无法编译，因为在模块外部找不到 `Detail` 这个名字。
3. 导入了某个模块的文件，必须等这个模块预编译完成后才能编译。构建系统需要先扫描每个文件，找出它 `export module` 了什么、`import` 了什么，建立依赖图，再按顺序编译；没有依赖关系的文件仍然可以并行编译。

</details>
