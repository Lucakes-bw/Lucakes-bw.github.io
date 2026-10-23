---
title: 从 C 到 C++：第一个 C++ 程序
date: 2026-10-23 09:00:00
updated: 2026-10-23 09:00:00
categories:
  - C/C++
  - C++基础
tags:
  - C++基础
  - 入门
order: 1
description: 已经会 C 语言，怎么开始学 C++？本文从第一个 C++ 程序讲起，介绍 iostream 输入输出、std::string、命名空间、bool、函数重载和默认参数，并用 nm 命令看清函数重载在编译后是怎么实现的。
disableNunjucks: true
---

> **本文目标**：会写、会编译第一个 C++ 程序；会用 `std::cin` / `std::cout` 输入输出，会用 `std::string` 处理字符串；理解命名空间的作用；会写重载函数和带默认参数的函数；知道函数重载在编译后是如何区分的。
>
> **前置知识**：C 语言基础（本专栏第 1 章）。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。本章所有示例都用 C++17 编译。

## 1. C++ 和 C 的关系

C++ 最初的名字叫「带类的 C」（C with Classes），它几乎完全兼容 C 语言：绝大多数 C 程序改个扩展名，就能用 C++ 编译器编译。在 C 的基础上，C++ 增加了：

- **面向对象**：类、继承、多态（本章后面的文章）；
- **泛型编程**：模板（本章《函数模板与类模板》）；
- **标准库**：`string`、`vector`、`map` 等容器和大量算法（本章《STL 容器》）；
- **更安全、更方便的语法**：引用、函数重载、命名空间、异常等。

学 C++ 不需要忘掉 C，而是在 C 的基础上，逐步用更安全、更方便的写法代替原来容易出错的写法。

## 2. 第一个 C++ 程序

```cpp
#include <iostream>
#include <string>

int main()
{
    std::string name;
    int age = 0;
    std::cout << "请输入名字和年龄: ";
    std::cin >> name >> age;                      // 不需要 &，也不需要 %s、%d
    std::cout << "你好，" << name << "！明年你就 " << age + 1 << " 岁了。\n";

    std::string greeting = "Hello, " + name;      // string 可以直接用 + 拼接
    std::cout << greeting << "，长度 " << greeting.size() << '\n';
    return 0;
}
```

保存为 `hello.cpp`，编译运行：

```bash
g++ -std=c++17 -Wall hello.cpp -o hello
./hello
```

输入 `Alice 20`，运行结果：

```
请输入名字和年龄: 你好，Alice！明年你就 21 岁了。
Hello, Alice，长度 12
```

和 C 语言对比：

| | C | C++ |
| --- | --- | --- |
| 头文件 | `#include <stdio.h>` | `#include <iostream>`（没有 `.h`） |
| 输出 | `printf("%d\n", x);` | `std::cout << x << '\n';` |
| 输入 | `scanf("%d", &x);` | `std::cin >> x;` |
| 字符串 | `char name[32];` | `std::string name;` |
| 编译器 | `gcc` | `g++`（或 `clang++`） |

- **`std::cout << 值`**：把值输出到屏幕。`<<` 可以连续使用，**会自动识别类型**，不需要 `%d`、`%s` 这样的格式符，也就不会出现格式符和类型不匹配的错误。
- **`std::cin >> 变量`**：从键盘读入。不需要取地址符 `&`（这要归功于下一篇介绍的「引用」）。
- **`std::string`**：C++ 的字符串类型，**长度自动管理**，不用担心缓冲区溢出；可以用 `+` 拼接、用 `==` 比较、用 `size()` 获取长度。C 语言中要用 `strcat`、`strcmp`、`strlen`，还要时刻小心数组越界。
- C 语言的头文件在 C++ 中也能用，推荐写成去掉 `.h`、前面加 `c` 的形式：`<cstdio>`、`<cstring>`、`<cmath>`。

> `std::endl` 也能换行，但它每次都会刷新输出缓冲区，大量输出时会明显变慢。一般用 `'\n'` 即可。

## 3. 命名空间

上面代码中的 `std::` 是什么？`std` 是一个**命名空间**（namespace），C++ 标准库的所有内容都在其中。

### 3.1 为什么需要命名空间

C 语言中所有的全局函数共享同一个「名字空间」。如果你写了一个 `play()` 函数，引入的某个库里也有一个 `play()`，就会冲突，链接时报错。大型项目中只能靠加前缀来避免：`audio_play()`、`video_play()`。

命名空间就是把名字分组：

```cpp
#include <iostream>

namespace audio {
    void play() { std::cout << "播放音频\n"; }
    int volume = 80;
}

namespace video {
    void play() { std::cout << "播放视频\n"; }   // 和 audio::play 同名，但不冲突
}

int main()
{
    audio::play();
    video::play();
    std::cout << "音量 " << audio::volume << '\n';

    using video::play;          // 只引入一个名字
    play();                      // 现在 play 指 video::play
    return 0;
}
```

运行结果：

```
播放音频
播放视频
音量 80
播放视频
```

- `命名空间::名字` 访问其中的内容，`::` 叫做**作用域解析运算符**。
- `using video::play;` 把一个名字引入当前作用域，之后可以直接写 `play()`。

### 3.2 using namespace std 的问题

很多教程会在开头写 `using namespace std;`，然后直接用 `cout`、`string`。这在小练习中没问题，但有两个隐患：

1. **`std` 中有成百上千个名字**，全部引入后，很容易和自己定义的名字冲突。比如自己定义了一个叫 `count` 或 `distance` 的变量，可能会和 `std::count`、`std::distance` 发生意想不到的冲突。
2. **绝对不要写在头文件中**：头文件会被很多文件包含，`using namespace std;` 会被带到所有包含它的文件中。

本专栏的代码统一写 `std::`。

## 4. bool 类型

C 语言（C99 之前）没有布尔类型，用 `int` 的 0 和非 0 表示假和真。C++ 内置了 `bool` 类型，取值为 `true` 和 `false`：

```cpp
bool ok = true;
std::cout << std::boolalpha << ok;    // 输出 true；不加 boolalpha 则输出 1
```

`bool` 通常占 1 个字节。整数、指针可以自动转换为 `bool`：0 和空指针为 `false`，其他为 `true`。

## 5. 函数重载

### 5.1 同名函数，不同参数

C 语言中，求两个 `int` 的最大值和两个 `double` 的最大值，必须写成两个不同名字的函数（比如 `maxInt` 和 `maxDouble`）。C++ 允许**同名函数，只要参数的个数或类型不同**，这叫**函数重载**：

```cpp
#include <iostream>
#include <string>

// 函数重载：同名函数，参数不同
int maxOf(int a, int b)             { std::cout << "  调用 int 版本\n";    return a > b ? a : b; }
double maxOf(double a, double b)    { std::cout << "  调用 double 版本\n"; return a > b ? a : b; }
int maxOf(int a, int b, int c)      { std::cout << "  调用三参数版本\n";   return maxOf(maxOf(a, b), c); }

// 默认参数：从右往左给出
void connect(const std::string& host, int port = 80, int timeout = 30)
{
    std::cout << "连接 " << host << ':' << port << "，超时 " << timeout << " 秒\n";
}

int main()
{
    std::cout << maxOf(3, 7) << '\n';
    std::cout << maxOf(2.5, 1.5) << '\n';
    std::cout << maxOf(4, 9, 2) << '\n';

    connect("example.com");
    connect("example.com", 8080);
    connect("example.com", 8080, 5);

    bool ok = true;                 // C++ 内置 bool 类型
    std::cout << std::boolalpha << "ok = " << ok << ", sizeof(bool) = " << sizeof(bool) << '\n';
    return 0;
}
```

运行结果：

```
  调用 int 版本
7
  调用 double 版本
2.5
  调用三参数版本
  调用 int 版本
  调用 int 版本
9
连接 example.com:80，超时 30 秒
连接 example.com:8080，超时 30 秒
连接 example.com:8080，超时 5 秒
ok = true, sizeof(bool) = 1
```

编译器根据**调用时实参的类型和个数**，选择最匹配的版本。`std::cout` 能输出各种类型的值，本身就是靠重载实现的。

### 5.2 重载的两条限制

**只有返回类型不同，不能构成重载**：

```cpp
int getValue() { return 1; }
double getValue() { return 1.0; }       // 只有返回类型不同
```

```
error: functions that differ only in their return type cannot be overloaded
```

调用 `getValue()` 时，编译器无法根据「你打算怎么用返回值」来选择版本。

**参数需要转换、并且有多个版本同样合适时，会产生二义性**：

```cpp
int maxOf(int a, int b);
double maxOf(double a, double b);
maxOf(3, 2.5);                          // int 和 double 混合
```

```
error: call to 'maxOf' is ambiguous
note: candidate function
note: candidate function
```

`maxOf(int, int)` 需要把 2.5 转成 `int`，`maxOf(double, double)` 需要把 3 转成 `double`，两者「一样差」，编译器无法决定。写成 `maxOf(3.0, 2.5)` 就可以了。

### 5.3 重载是怎么实现的：名字修饰

C 语言不支持重载，是因为编译后的目标文件中，函数就是用它的名字来标识的，两个同名函数必然冲突。C++ 编译器会把**参数类型编码进函数名**中，这叫**名字修饰**（name mangling）。用 `nm` 命令查看编译出的目标文件：

```bash
g++ -c overload.cpp -o overload.o
nm overload.o | grep maxOf
```

```
0000000000000044 T __Z5maxOfdd
0000000000000000 T __Z5maxOfii
```

`maxOf(int, int)` 变成了 `_Z5maxOfii`（`5` 是名字的长度，`ii` 表示两个 `int` 参数），`maxOf(double, double)` 变成了 `_Z5maxOfdd`（macOS 会在前面再加一个下划线）。对链接器来说，它们是两个名字完全不同的函数。用 `c++filt` 可以把修饰后的名字还原：

```bash
nm overload.o | grep maxOf | awk '{print $3}' | c++filt
```

```
maxOf(double, double)
maxOf(int, int)
```

这也解释了 C++ 调用 C 语言库时为什么需要 **`extern "C"`**：C 编译器编译出的函数名没有修饰，C++ 代码需要用 `extern "C" { ... }` 告诉编译器「这些函数按 C 的规则命名」，否则链接时会找不到。标准库中的 `<cstdio>` 等头文件已经替你处理好了。

## 6. 默认参数

上面的 `connect` 函数给 `port` 和 `timeout` 指定了**默认值**，调用时可以省略。规则：

1. **默认参数必须从右往左连续给出**。`void f(int a = 1, int b);` 是错误的，因为调用 `f(5)` 时无法确定 5 是给 `a` 还是给 `b` 的。
2. **调用时只能从右往左省略**。不能跳过 `port` 只指定 `timeout`。
3. **函数的声明和定义分开写时，默认值只写在声明中**（通常是头文件中），定义中不要重复写，否则会报「重复定义默认参数」的错误。

默认参数和重载都能实现「同一个函数名，不同的调用方式」。参数只是「有一个常用值」时，用默认参数更简洁；不同参数类型需要完全不同的处理逻辑时，用重载。

## 7. 常见错误

1. **把 `std::cin >> x` 写成 `std::cin >> &x`**：C++ 中不需要取地址。
2. **用 `+` 拼接两个字符串字面量**：`"Hello, " + "world"` 编译失败，两边都是 `const char*`，至少要有一边是 `std::string`。
3. **在头文件中写 `using namespace std;`**。
4. **重载时出现二义性**：传入的参数类型与所有版本都不完全匹配时，检查是否需要显式转换。
5. **默认参数在声明和定义中都写了一遍**。

## 8. 小结

- C++ 兼容 C，在 C 的基础上增加了面向对象、泛型编程、标准库和更安全的语法
- `std::cout <<` 输出、`std::cin >>` 输入，自动识别类型；`std::string` 自动管理长度，可以 `+` 拼接、`==` 比较
- 命名空间把名字分组，避免冲突；标准库都在 `std` 中；不要在头文件中写 `using namespace std;`
- C++ 有内置的 `bool` 类型
- **函数重载**：同名函数、参数不同；只有返回类型不同不能重载；编译器通过**名字修饰**区分它们
- **默认参数**：从右往左给出、从右往左省略，只写在声明中

## 9. 练习

1. 写一个程序，读入若干个整数（以 Ctrl+D 或 Ctrl+Z 结束输入），输出它们的和与平均值。提示：`while (std::cin >> x)`。
2. 写三个重载的 `print` 函数，分别打印 `int`、`double`、`std::string`，打印格式分别为 `int: 42`、`double: 3.14`、`string: "hi"`。调用 `print('A')` 会选择哪个版本？
3. 下面的函数声明有什么问题？

   ```cpp
   void draw(int x = 0, int y, int color = 255);
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   int x, count = 0;
   long long sum = 0;
   while (std::cin >> x) {
       sum += x;
       ++count;
   }
   std::cout << "和 = " << sum;
   if (count > 0) std::cout << "，平均值 = " << static_cast<double>(sum) / count;
   std::cout << '\n';
   ```

   读到文件末尾或者遇到无法解析的输入时，`std::cin >> x` 的结果为假，循环结束。

2. 参考代码：

   ```cpp
   void print(int x)                { std::cout << "int: " << x << '\n'; }
   void print(double x)             { std::cout << "double: " << x << '\n'; }
   void print(const std::string& s) { std::cout << "string: \"" << s << "\"\n"; }
   ```

   `print('A')` 调用 `int` 版本，输出 `int: 65`。`char` 提升为 `int` 是「整型提升」，比转换为 `double` 的优先级更高。

3. 默认参数必须从右往左连续给出。`x` 有默认值，而它右边的 `y` 没有，编译报错。改成 `void draw(int x, int y, int color = 255);` 或者给 `y` 也加上默认值。

</details>
