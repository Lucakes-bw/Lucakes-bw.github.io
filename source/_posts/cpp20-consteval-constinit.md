---
title: consteval、constinit 与更强大的 constexpr
date: 2026-10-27 10:00:00
updated: 2026-10-27 10:00:00
categories:
  - C/C++
  - C++20
tags:
  - C++20
  - constexpr
  - 编译期计算
order: 6
description: C++20 进一步加强了编译期计算：consteval 函数保证只在编译期执行，constinit 保证全局变量在编译期初始化、从根本上避免「静态初始化顺序灾难」，constexpr 中也终于可以使用 vector、string、虚函数和动态内存。
disableNunjucks: true
---

> **本文目标**：分清 `constexpr`、`consteval`、`constinit` 三者的区别；会用 `consteval` 强制编译期计算；理解「静态初始化顺序灾难」，会用 `constinit` 避免它；会在 `constexpr` 函数中使用 `vector`、`string`、虚函数和 `new`；会用 `std::is_constant_evaluated` 区分编译期和运行期。
>
> **前置知识**：C++11 章节的《nullptr、using 别名与 constexpr 入门》，C++14 章节的《放宽的 constexpr》。
>
> **编译环境**：`g++ -std=c++20` / `clang++ -std=c++20`，GCC 10+ / Clang 10+（`constexpr` 中的 `vector`、`string` 需要 GCC 12+ / Clang 15+）。

## 1. 回顾：constexpr 的演进

| 标准 | constexpr 函数能做什么 |
| --- | --- |
| C++11 | 只能有一条 `return` 语句 |
| C++14 | 可以有局部变量、`if`、循环 |
| C++17 | lambda 可以是 `constexpr`；`if constexpr` |
| **C++20** | 可以使用 `try`、虚函数、`new`/`delete`、`std::vector`、`std::string`；新增 `consteval`、`constinit` |

但 `constexpr` 函数有一个特点一直没变：它**可以**在编译期执行，**也可以**在运行期执行。C++11 章节中提到过，只有在必须得到常量的地方，才保证在编译期计算。有时我们希望**强制**编译期计算，这就是 `consteval` 的用途。

## 2. consteval：只能在编译期执行

```cpp
#include <iostream>

constexpr int squareCx(int x) { return x * x; }    // 编译期、运行期都可以
consteval int squareCe(int x) { return x * x; }    // 只能在编译期调用（立即函数）

// 编译期计算字符串哈希，用在 switch 中
consteval unsigned hashOf(const char* s)
{
    unsigned h = 5381;
    while (*s) h = h * 33 + static_cast<unsigned char>(*s++);
    return h;
}

int main()
{
    int n = 0;
    std::cin >> n;
    int a = squareCx(n);          // OK：constexpr 函数可以在运行期调用
    int b = squareCe(5);          // OK：参数是常量，在编译期算出 25
    // int c = squareCe(n);       // 错误：n 是运行期的值
    std::cout << a << ' ' << b << '\n';

    const char* cmd = n > 0 ? "start" : "stop";
    unsigned h = 5381;
    for (const char* p = cmd; *p; ++p) h = h * 33 + static_cast<unsigned char>(*p);
    switch (h) {
        case hashOf("start"): std::cout << "收到 start\n"; break;
        case hashOf("stop"):  std::cout << "收到 stop\n"; break;
    }
    return 0;
}
```

输入 `7`：

```
49 25
收到 start
```

输入 `-1`：

```
1 25
收到 stop
```

用 `consteval` 声明的函数叫做**立即函数**（immediate function）：**每一次调用都必须能在编译期求值**。用运行期的值调用它：

```cpp
consteval int squareCe(int x) { return x * x; }
int main(int argc, char**) { return squareCe(argc); }
```

```
error: call to consteval function 'squareCe' is not a constant expression
note: function parameter 'argc' with unknown value cannot be used in a constant expression
```

什么时候用 `consteval` 而不是 `constexpr`？

- **计算结果必须是常量**，在运行期调用就是使用错误：比如上例中用于 `case` 标签的哈希函数、编译期的格式字符串检查、生成查找表。
- **不希望它出现在最终的程序中**：`consteval` 函数不会生成任何运行期代码。

C++20 的 `std::format` 就利用了 `consteval`：格式字符串在编译期检查，`std::format("{:d}", "text")` 这样类型不匹配的写法会直接编译失败。下一篇会介绍它。

## 3. constinit 与静态初始化顺序灾难

### 3.1 静态初始化顺序灾难

全局变量和静态变量在 `main` 之前初始化。如果初始值不是常量表达式（比如调用了一个普通函数），就需要在程序启动时**动态初始化**。问题在于：**不同 `.cpp` 文件中全局变量的动态初始化顺序是不确定的。**

来做一个实验。两个文件：

```cpp
// config.cpp
#include <string>
std::string loadName() { return "lucky-server"; }
std::string appName = loadName();              // 动态初始化：在 main 之前的某个时刻运行
```

```cpp
// logger.cpp
#include <cstdio>
#include <string>
extern std::string appName;
struct Logger {
    Logger() { std::printf("Logger 初始化时 appName = \"%s\"（长度 %zu）\n", appName.c_str(), appName.size()); }
};
Logger globalLogger;                            // 也在 main 之前构造，用到了另一个文件的全局变量
int main() { std::printf("main 中 appName = \"%s\"\n", appName.c_str()); }
```

两个文件的内容完全不变，只改变**链接时的文件顺序**：

```bash
clang++ -std=c++20 config.cpp logger.cpp -o a1 && ./a1
```

```
Logger 初始化时 appName = "lucky-server"（长度 12）
main 中 appName = "lucky-server"
```

```bash
clang++ -std=c++20 logger.cpp config.cpp -o a2 && ./a2
```

```
Logger 初始化时 appName = ""（长度 0）
main 中 appName = "lucky-server"
```

第二种顺序下，`globalLogger` 构造时，`appName` **还没有被初始化**，它读到的是一个尚未构造的 `std::string`（这里恰好表现为空字符串，严格来说是未定义行为）。这就是著名的**静态初始化顺序灾难**（static initialization order fiasco）：程序的行为取决于链接顺序，换一个构建系统、增加一个文件，问题就可能突然出现或消失。

### 3.2 constinit

C++20 的 **`constinit`** 要求变量**必须在编译期完成初始化**（称为常量初始化），这样的变量在任何动态初始化开始之前就已经有了正确的值，与初始化顺序无关：

```cpp
#include <iostream>

constexpr int computeLimit() { return 100 * 1024; }
int runtimeValue() { return 42; }

constinit int bufferLimit = computeLimit();   // 保证在编译期初始化，但之后可以修改
// constinit int bad = runtimeValue();        // 错误：初始值必须是常量表达式

int main()
{
    std::cout << "bufferLimit = " << bufferLimit << '\n';
    bufferLimit *= 2;                          // 和 constexpr 不同：constinit 变量不是 const
    std::cout << "修改后 = " << bufferLimit << '\n';
    return 0;
}
```

运行结果：

```
bufferLimit = 102400
修改后 = 204800
```

如果初始值不是常量表达式：

```cpp
int runtimeValue() { return 42; }
constinit int bad = runtimeValue();
```

```
error: variable does not have a constant initializer
note: required by 'constinit' specifier here
```

`constinit` 并不会改变变量的初始化方式，它只是一个**断言**：「这个变量一定是常量初始化的，如果做不到，就编译失败」。这样，某个同事修改了初始化代码、不小心引入了动态初始化时，编译器会立即报错，而不是在某个链接顺序下才出现诡异的 bug。

`std::string` 的构造在 C++20 中可以是 `constexpr` 的，但字符串较长时需要堆分配，而编译期分配的内存不能留到运行期（见第 4.2 节），所以第 3.1 节的 `appName` 无法用 `constinit` 解决。对于这类对象，通常的做法是改成函数内的静态变量，在第一次使用时才初始化：

```cpp
const std::string& appName()
{
    static const std::string name = loadName();   // 第一次调用时初始化，C++11 起保证线程安全
    return name;
}
```

### 3.3 三者的区别

| | `constexpr` | `consteval` | `constinit` |
| --- | --- | --- | --- |
| 修饰 | 变量、函数 | 函数 | 静态 / 全局 / thread_local 变量 |
| 函数 | 可以在编译期执行，也可以在运行期执行 | **只能**在编译期执行 | — |
| 变量 | 编译期初始化，**并且是 const** | — | 编译期初始化，**但可以修改** |
| 主要用途 | 编译期计算，能用就用 | 强制编译期计算 | 避免静态初始化顺序问题 |

## 4. 更强大的 constexpr

### 4.1 constexpr 中的 vector、string、虚函数和 new

```cpp
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

// C++20：constexpr 函数中可以使用 vector、string（只要在编译期内释放）
constexpr int sumOfSquares(int n)
{
    std::vector<int> v;
    for (int i = 1; i <= n; ++i) v.push_back(i * i);
    int total = 0;
    for (int x : v) total += x;
    return total;
}

constexpr std::size_t countVowels(std::string_view s)
{
    std::string str(s);
    return std::count_if(str.begin(), str.end(), [](char c) {
        return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
    });
}

// C++20：constexpr 中可以使用 try、虚函数、动态分配（编译期内释放）
struct Shape { constexpr virtual ~Shape() = default; constexpr virtual int sides() const = 0; };
struct Triangle : Shape { constexpr int sides() const override { return 3; } };
struct Square : Shape { constexpr int sides() const override { return 4; } };

constexpr int totalSides()
{
    Shape* shapes[] = {new Triangle, new Square, new Square};
    int total = 0;
    for (Shape* s : shapes) total += s->sides();
    for (Shape* s : shapes) delete s;
    return total;
}

static_assert(sumOfSquares(10) == 385);
static_assert(countVowels("constexpr evaluation") == 8);
static_assert(totalSides() == 11);

int main()
{
    std::cout << "全部在编译期验证通过: " << sumOfSquares(10) << ' ' << countVowels("constexpr evaluation")
              << ' ' << totalSides() << '\n';
    return 0;
}
```

运行结果：

```
全部在编译期验证通过: 385 8 11
```

三个 `static_assert` 都通过了，说明 `vector`、`string`、`new`、`delete`、虚函数调用都在**编译期**正确执行了。C++20 之前，这些都不能出现在 `constexpr` 函数中，编译期计算只能使用数组和简单的结构体，就像 C++14 章节中自己定义 `PrimeTable` 那样。

### 4.2 编译期分配的内存不能带到运行期

```cpp
#include <vector>
constexpr std::vector<int> makeVec() { return {1, 2, 3}; }
constexpr auto v = makeVec();     // 错误：编译期分配的内存不能“带到”运行期
```

```
error: constexpr variable 'v' must be initialized by a constant expression
note: pointer to subobject of heap-allocated object is not a constant expression
```

编译期中 `new` 出来的内存，**必须在同一次编译期计算中 `delete` 掉**。`constexpr` 变量 `v` 的值要保存到程序中供运行期使用，而编译器无法把一块「编译期的堆内存」放进程序里。这种限制叫做**瞬时分配**（transient allocation）。所以 `vector` 可以在 `constexpr` 函数的**内部**使用，但不能作为 `constexpr` 变量的类型。需要把结果带到运行期时，转换成 `std::array`。

### 4.3 std::is_constant_evaluated

有时同一个函数在编译期和运行期需要不同的实现：编译期只能用纯 C++ 代码，运行期则希望使用更快的硬件指令或系统调用。`std::is_constant_evaluated()` 可以判断当前是否处于编译期求值：

```cpp
#include <cmath>
#include <iostream>
#include <type_traits>

constexpr double mySqrt(double x)
{
    if (std::is_constant_evaluated()) {       // 编译期：用牛顿迭代法
        double g = x / 2;
        for (int i = 0; i < 30; ++i) g = (g + x / g) / 2;
        return g;
    } else {
        return std::sqrt(x);                  // 运行期：用硬件指令
    }
}

int main()
{
    constexpr double a = mySqrt(2.0);         // 编译期分支
    double x = 2.0;
    double b = mySqrt(x);                      // 运行期分支
    std::cout.precision(17);
    std::cout << a << ' ' << b << "，相差 " << a - b << '\n';
    return 0;
}
```

运行结果：

```
1.4142135623730949 1.4142135623730951，相差 -2.2204460492503131e-16
```

`a` 走了编译期分支（牛顿迭代），`b` 走了运行期分支（`std::sqrt`）。注意两者在最后一位上相差了一个最小精度单位：牛顿迭代的舍入方式和硬件指令不完全相同。这提醒我们，**用 `is_constant_evaluated` 写两套实现时，必须保证它们的结果一致**，否则同一个函数在编译期和运行期算出不同的值，会造成非常隐蔽的问题。

> 不要写成 `if constexpr (std::is_constant_evaluated())`：`if constexpr` 的条件本身总是在编译期求值，结果永远是 `true`。Clang 会直接警告：`'std::is_constant_evaluated' will always evaluate to 'true' in a manifestly constant-evaluated expression`。C++23 提供了更清晰的 `if consteval { ... } else { ... }` 语法。

## 5. 常见错误

1. **在运行期调用 `consteval` 函数**：编译失败，需要运行期调用就用 `constexpr`。
2. **以为 `constinit` 变量是常量**：它只保证初始化发生在编译期，之后可以修改；想要常量，用 `constexpr` 或 `constinit const`。
3. **`constexpr` 变量的类型是 `vector` 或 `string`**：编译期分配的内存不能留到运行期，用 `std::array` 或 `std::string_view`（指向字符串字面量）。
4. **编译期分支和运行期分支的结果不一致**：见第 4.3 节。

## 6. 小结

- **`consteval`** 函数（立即函数）每次调用都必须在编译期求值，不会生成运行期代码
- **静态初始化顺序灾难**：不同文件中全局变量的动态初始化顺序不确定，实测改变链接顺序就会读到未初始化的值
- **`constinit`** 断言变量是在编译期初始化的，从根本上避免初始化顺序问题；它不是 const，之后可以修改
- C++20 的 `constexpr` 中可以使用 `vector`、`string`、`new`/`delete`、虚函数、`try`，但编译期分配的内存必须在编译期释放
- `std::is_constant_evaluated()` 区分编译期和运行期，两个分支的结果必须一致

## 7. 练习

1. 写一个 `consteval` 函数 `fibonacci(n)`，用它在编译期生成一个包含前 20 个斐波那契数的 `std::array`。
2. 下面哪些声明能够编译？

   ```cpp
   int f();
   constexpr int g() { return 1; }
   constinit int a = g();
   constinit int b = f();
   constinit const int c = g();
   constexpr int d = g();
   consteval int h() { return f(); }
   ```

3. 写一个 `constexpr` 函数，在编译期用 `std::vector` 和埃拉托斯特尼筛法找出 100 以内的质数个数，用 `static_assert` 验证结果为 25。

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   #include <array>

   consteval std::array<long long, 20> fibTable()
   {
       std::array<long long, 20> t{};
       t[0] = 0; t[1] = 1;
       for (int i = 2; i < 20; ++i) t[i] = t[i - 1] + t[i - 2];
       return t;
   }
   constexpr auto kFib = fibTable();
   static_assert(kFib[19] == 4181);
   ```

   C++17 起 `std::array` 的非 const `operator[]` 是 `constexpr` 的，可以在编译期修改元素。

2. `a`、`c`、`d` 能编译。`b` 不能：`f()` 不是 `constexpr` 函数，无法常量初始化。`h` 不能：`consteval` 函数体中调用了非 `constexpr` 函数 `f()`，它永远不可能在编译期求值。

3. 参考代码：

   ```cpp
   #include <vector>

   constexpr int countPrimes(int n)
   {
       std::vector<bool> isPrime(n + 1, true);
       int count = 0;
       for (int i = 2; i <= n; ++i) {
           if (!isPrime[i]) continue;
           ++count;
           for (int j = i * i; j <= n; j += i) isPrime[j] = false;
       }
       return count;
   }
   static_assert(countPrimes(100) == 25);
   ```

   `vector` 在函数返回前就被销毁了，所以可以在 `constexpr` 函数中使用。

</details>
