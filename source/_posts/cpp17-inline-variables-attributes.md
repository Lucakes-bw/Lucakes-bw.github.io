---
title: inline 变量、常用属性与实用小特性
date: 2026-10-25 16:00:00
updated: 2026-10-25 16:00:00
categories:
  - C/C++
  - C++17
tags:
  - C++17
  - 属性
order: 7
description: inline 变量让头文件中可以直接定义全局变量和静态成员；[[nodiscard]]、[[maybe_unused]]、[[fallthrough]] 三个属性让编译器帮你发现更多 bug。本文还介绍嵌套命名空间、__has_include、std::clamp 等实用的小特性。
disableNunjucks: true
---

> **本文目标**：理解「头文件中定义变量」为什么会链接失败，会用 `inline` 变量解决；会在类内直接定义静态成员变量；会用 `[[nodiscard]]` 防止返回值被忽略，用 `[[maybe_unused]]` 消除误报的警告，用 `[[fallthrough]]` 标记有意的 case 贯穿；认识嵌套命名空间、`__has_include`、`std::size`、`std::clamp`、`std::gcd` 等小特性。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。

## 1. inline 变量

### 1.1 问题：头文件中不能定义变量

一个程序有多个 `.cpp` 文件，都需要访问同一个全局配置。最直接的想法是在头文件中定义它：

```cpp
// config_bad.h：在头文件中定义变量
#pragma once
#include <string>
int requestCount = 0;
std::string appName = "lucky-server";
```

`a.cpp`、`b.cpp`、`main.cpp` 都包含这个头文件，编译时没有问题，**链接时**报错：

```
duplicate symbol '_requestCount' in:
duplicate symbol '_appName' in:
ld: 2 duplicate symbols
```

原因是 C++ 的**单一定义规则**（ODR）：每个 `.cpp` 文件独立编译成一个目标文件，`#include` 只是把头文件的内容原样复制进来。三个 `.cpp` 都包含了这个头文件，于是三个目标文件中都有一个 `requestCount` 的**定义**，链接器不知道该用哪一个。

C++17 之前的解决办法是：头文件中只写**声明** `extern int requestCount;`，然后在**某一个** `.cpp` 文件中写定义 `int requestCount = 0;`。声明和定义分在两处，修改时容易漏掉。类的静态成员变量也有同样的问题：类内只能声明，必须在某个 `.cpp` 中再定义一次（`int Logger::instances = 0;`），这也是很多「纯头文件库」难以实现的原因。

### 1.2 inline 变量

C++17 允许把变量声明为 **`inline`**。和 inline 函数一样，**inline 变量可以在多个翻译单元中重复定义，链接器会把它们合并成同一个变量**：

```cpp
// config.h：C++17 inline 变量
#pragma once
#include <string>
inline int requestCount = 0;
inline std::string appName = "lucky-server";

class Logger {
public:
    static inline int instances = 0;          // 静态成员直接在类内定义
    static constexpr int maxLevel = 5;        // constexpr 静态成员在 C++17 中隐式 inline
    Logger() { ++instances; }
};
```

同样三个 `.cpp` 文件：`a.cpp` 中的 `handleA()` 和 `b.cpp` 中的 `handleB()` 都把 `requestCount` 加 1，并打印它的地址；`main.cpp` 中调用这两个函数，并创建两个 `Logger`。编译链接通过，运行结果：

```
Logger::instances = 2, maxLevel = 5
a.cpp: &requestCount = 0x1002ec020
b.cpp: &requestCount = 0x1002ec020
main: requestCount = 2, appName = lucky-server
```

`a.cpp` 和 `b.cpp` 中 `requestCount` 的**地址完全相同**，说明它们访问的是同一个变量，最终的值是 2。

要点：

- **`static inline` 静态成员**：可以在类定义中直接初始化，不再需要在 `.cpp` 中额外定义一次。
- **`static constexpr` 静态成员在 C++17 中自动是 inline 的**。C++11/14 中，如果对它取地址或绑定引用（比如传给参数为 `const int&` 的函数），还需要在 `.cpp` 中再写一次定义，否则会链接失败，C++17 起不再需要。
- 常量（`const` 或 `constexpr` 的命名空间作用域变量）本来就是**内部链接**的，每个 `.cpp` 各有一份副本，不会冲突；但它们不是同一个对象，地址也不同。需要「全程序唯一」的常量时，写 `inline constexpr`。

> 全局可变变量本身是应该尽量避免的设计。inline 变量最主要的用途是**常量**和**类的静态成员**，以及让纯头文件库成为可能。

## 2. 常用属性

C++11 引入了 `[[属性]]` 语法，C++14 增加了 `[[deprecated]]`。C++17 新增了三个日常非常有用的属性。

### 2.1 [[nodiscard]]：返回值不能被忽略

```cpp
#include <string>

[[nodiscard]] int computeChecksum(const std::string& data)
{
    int sum = 0;
    for (char c : data) sum += c;
    return sum;
}

[[nodiscard]] bool saveFile(const std::string& path);   // 返回值表示是否成功，忽略它很危险

struct [[nodiscard]] ErrorCode {                        // 修饰类型：所有返回它的函数都受影响
    int code;
};
ErrorCode connect(const std::string& host);

int main()
{
    computeChecksum("hello");          // 警告：计算了却不用结果
    saveFile("data.txt");              // 警告：没有检查是否保存成功
    connect("example.com");            // 警告：返回 ErrorCode 却被忽略
    (void)computeChecksum("ok");       // 确实不需要时，显式转换为 void
    return 0;
}
```

Clang 的编译输出：

```
warning: ignoring return value of function declared with 'nodiscard' attribute [-Wunused-result]
warning: ignoring return value of function declared with 'nodiscard' attribute [-Wunused-result]
warning: ignoring return value of type 'ErrorCode' declared with 'nodiscard' attribute [-Wunused-value]
```

适合加 `[[nodiscard]]` 的函数：

- **返回错误码或成功标志**的函数，比如 `saveFile`。忽略它意味着错误被悄悄吞掉。
- **没有副作用、只计算结果**的函数，比如 `computeChecksum`。忽略它的返回值，调用就毫无意义，很可能是写错了。
- **返回需要释放的资源**的函数，比如返回裸指针的工厂函数。

修饰**类型**时（`struct [[nodiscard]] ErrorCode`），所有返回这个类型的函数都会自动得到这个检查，非常适合错误码类型。

本专栏前面的文章中已经多次遇到过它：标准库给 `std::async`、`std::any_cast`、`vector::empty()` 等函数加上了 `[[nodiscard]]`，忽略它们的返回值时 libc++ 会给出警告，帮我们发现了 `std::async` 串行执行这样的 bug。C++20 起还可以写上原因：`[[nodiscard("必须检查是否保存成功")]]`。

### 2.2 [[maybe_unused]]：消除「未使用」的误报

```cpp
#include <cassert>

int compute(int x, [[maybe_unused]] int debugLevel)    // 参数暂时不用
{
    [[maybe_unused]] int checked = x * 2;               // 只在 assert 中使用，发布版会被去掉
    assert(checked >= 0);
    return x + 1;
}

int computeNoAttr(int x, int debugLevel)
{
    int checked = x * 2;
    assert(checked >= 0);
    return x + 1;
}
```

以发布模式编译（`-DNDEBUG -Wall -Wextra`），`assert` 被展开为空：

```
warning: unused variable 'checked' [-Wunused-variable]
warning: unused parameter 'debugLevel' [-Wunused-parameter]
```

两条警告都来自没有加属性的 `computeNoAttr`。`checked` 只在 `assert` 中使用，发布模式下 `assert` 消失了，它就变成了「未使用的变量」。这类警告是误报，但大量的误报会让真正有用的警告被淹没。`[[maybe_unused]]` 告诉编译器「这里可能不会用到，是有意的」。它可以用于变量、参数、函数、类型等。

### 2.3 [[fallthrough]]：标记有意的贯穿

`switch` 中忘记写 `break` 是 C/C++ 的经典 bug。但有时候贯穿到下一个 `case` 又是故意的：

```cpp
#include <iostream>

void describe(int level)
{
    switch (level) {
        case 3:
            std::cout << "管理 ";
            [[fallthrough]];          // 明确告诉编译器：这里故意不写 break
        case 2:
            std::cout << "编辑 ";
            [[fallthrough]];
        case 1:
            std::cout << "查看";
            break;
        case 0:
            std::cout << "无权限";
            // 忘了写 break！
        default:
            std::cout << "（未知）";
    }
    std::cout << '\n';
}

int main()
{
    describe(3);
    describe(1);
    describe(0);
    return 0;
}
```

开启 `-Wimplicit-fallthrough` 编译：

```
warning: unannotated fall-through between switch labels [-Wimplicit-fallthrough]
note: insert '[[fallthrough]];' to silence this warning
```

运行结果：

```
管理 编辑 查看
查看
无权限（未知）
```

编译器只对 `case 0` 给出了警告：前面两处贯穿用 `[[fallthrough]]` 标明了是故意的，而 `case 0` 忘写 `break`，导致输出了多余的「（未知）」。

注意：GCC 的 `-Wextra` 包含这个警告，**Clang 的 `-Wall -Wextra` 都不包含**，需要单独加上 `-Wimplicit-fallthrough`。

## 3. 其他实用的小特性

```cpp
#include <algorithm>
#include <iostream>
#include <numeric>
#include <vector>

#if __has_include(<filesystem>)
#include <filesystem>
#define HAS_FS 1
#else
#define HAS_FS 0
#endif

namespace company::project::utils {        // C++17：嵌套命名空间一行定义
    int version() { return 3; }
}

int main()
{
    std::cout << "version = " << company::project::utils::version() << '\n';
    std::cout << "有 <filesystem>: " << HAS_FS << '\n';

    int arr[] = {5, 3, 8};
    std::vector<int> v{1, 2, 3, 4};
    std::cout << "std::size(arr) = " << std::size(arr) << ", std::size(v) = " << std::size(v) << '\n';

    std::cout << "clamp(150, 0, 100) = " << std::clamp(150, 0, 100)
              << ", clamp(-5, 0, 100) = " << std::clamp(-5, 0, 100) << '\n';
    std::cout << "gcd(48, 18) = " << std::gcd(48, 18) << ", lcm(4, 6) = " << std::lcm(4, 6) << '\n';
    return 0;
}
```

运行结果：

```
version = 3
有 <filesystem>: 1
std::size(arr) = 3, std::size(v) = 4
clamp(150, 0, 100) = 100, clamp(-5, 0, 100) = 0
gcd(48, 18) = 6, lcm(4, 6) = 12
```

| 特性 | 说明 |
| --- | --- |
| 嵌套命名空间 | `namespace a::b::c { }`，代替三层嵌套的 `namespace a { namespace b { namespace c { } } }` |
| `__has_include(<头文件>)` | 在预处理阶段判断某个头文件是否存在，方便写兼容不同平台、不同标准库版本的代码 |
| `std::size(c)` | 得到数组或容器的元素个数。以前求数组长度要写 `sizeof(arr) / sizeof(arr[0])`，而这种写法用在指针上会悄悄得到错误的结果；`std::size` 用在指针上会直接编译失败 |
| `std::clamp(v, lo, hi)` | 把值限制在 `[lo, hi]` 范围内，代替 `std::min(std::max(v, lo), hi)` |
| `std::gcd` / `std::lcm` | 最大公约数和最小公倍数，在 `<numeric>` 中 |
| `static_assert(条件)` | 错误信息可以省略 |
| `u8` 字符字面量 | `u8'a'`，保证是 UTF-8 编码的字符 |

## 4. 常见错误

1. **以为 `inline` 变量只是一个「建议」**：和 inline 函数的「内联展开」无关，`inline` 在这里的意思是「允许在多个翻译单元中重复定义」。
2. **头文件中的 `inline` 变量初始化不一致**：多个翻译单元中的定义必须完全相同。如果用宏让不同的 `.cpp` 中初始值不同，就违反了 ODR，是未定义行为，链接器通常不会报错。
3. **给所有函数都加 `[[nodiscard]]`**：会产生大量噪音，反而让人习惯性地忽略警告。只给「忽略返回值几乎肯定是错误」的函数加。
4. **`[[fallthrough]]` 后面忘写分号**，或者把它放在最后一个 `case` 之后（后面没有可以贯穿的标签了），都会编译报错。

## 5. 进阶：属性的通用规则

- **编译器会忽略不认识的属性**（可能给出警告），所以属性不会影响程序在不同编译器之间的可移植性。各编译器也有自己的属性，如 `[[gnu::always_inline]]`、`[[clang::fallthrough]]`、`[[msvc::noinline]]`。
- 属性可以写在声明的多个位置，含义略有不同。例如 `[[nodiscard]] int f();` 修饰函数，`struct [[nodiscard]] S {};` 修饰类型。
- C++17 还允许 `[[using gnu: always_inline, hot]]` 这样的写法，一次给多个属性加上同一个命名空间前缀。
- 后续标准新增的常用属性：C++20 的 `[[likely]]`、`[[unlikely]]`（提示分支概率）、`[[no_unique_address]]`（允许空成员不占空间）；C++23 的 `[[assume(表达式)]]`（告诉优化器某个条件一定成立）。

## 6. 小结

- 头文件中定义普通变量会导致链接时 `duplicate symbol`；**`inline` 变量**可以在多个翻译单元中定义，链接器合并为同一个对象
- `static inline` 静态成员可以在类内直接初始化；`static constexpr` 成员在 C++17 中自动 inline
- **`[[nodiscard]]`**：忽略返回值时警告，适合错误码、纯计算函数；可以修饰类型
- **`[[maybe_unused]]`**：消除「未使用」的误报，如只在 `assert` 中使用的变量
- **`[[fallthrough]]`**：标记有意的 case 贯穿；Clang 需要 `-Wimplicit-fallthrough` 才会检查
- 嵌套命名空间 `a::b::c`、`__has_include`、`std::size`、`std::clamp`、`std::gcd` / `std::lcm` 等小特性

## 7. 练习

1. 写一个头文件 `counter.h`，提供一个全局计数器和 `increment()`、`get()` 两个函数，要求所有内容都写在头文件中，被多个 `.cpp` 包含时能正确链接。
2. 下面的代码中，哪些函数适合加 `[[nodiscard]]`？

   ```cpp
   int add(int a, int b);
   void log(const std::string& msg);
   bool tryLock();
   std::vector<int>& data();
   std::unique_ptr<Widget> createWidget();
   ```

3. 用 `std::clamp` 写一个函数，把 RGB 颜色的每个分量限制在 0 到 255 之间。

<details>
<summary>点击查看答案</summary>

1. 参考代码，变量用 `inline`，函数也要用 `inline`（或者写在类中、作为模板）：

   ```cpp
   // counter.h
   #pragma once
   inline int globalCounter = 0;
   inline void increment() { ++globalCounter; }
   inline int get() { return globalCounter; }
   ```

2. `add`（纯计算，忽略结果毫无意义）、`tryLock`（忽略返回值就不知道是否拿到了锁）、`createWidget`（忽略返回值会立刻销毁刚创建的对象）适合加。`log` 返回 `void`，不能加；`data()` 返回引用，调用者可能只是想通过它修改数据，比如 `data().push_back(1)`，不适合加。

3. 参考代码：

   ```cpp
   struct Color { int r, g, b; };
   Color normalize(Color c)
   {
       return {std::clamp(c.r, 0, 255), std::clamp(c.g, 0, 255), std::clamp(c.b, 0, 255)};
   }
   ```

</details>
