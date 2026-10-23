---
title: enum class 与 static_assert
date: 2026-10-22 12:20:00
updated: 2026-10-22 12:20:00
categories:
  - C/C++
  - C++11
tags:
  - C++11
  - 枚举
  - 编译期检查
order: 11
description: 强类型枚举 enum class 解决了传统枚举名字冲突、隐式转换成整数的问题；static_assert 让你在编译期检查各种假设。本文还会讲如何让 enum class 支持位运算标志。
disableNunjucks: true
---

> **本文目标**：知道传统 `enum` 的三个问题；会用 `enum class` 并指定底层类型；会让 `enum class` 支持 `|`、`&` 等位运算；会用 `static_assert` 检查平台假设、结构体布局和模板参数。
>
> **编译环境**：`g++ -std=c++11` / `clang++ -std=c++11`。

## 1. 传统 enum 的问题

```cpp
#include <iostream>

enum Color { Red, Green, Blue };
enum TrafficLight { Stop, Wait, Go };
// enum Fruit { Apple, Orange, Red };   // 错误：Red 已经被 Color 占用了

int main()
{
    Color c = Green;
    TrafficLight t = Wait;
    int n = c;                       // 隐式转换成 int
    if (c == t)                      // 两个完全无关的枚举居然可以比较
        std::cout << "Green == Wait ?!\n";
    std::cout << "n = " << n << ", c + 10 = " << c + 10 << '\n';
    return 0;
}
```

运行结果：

```
Green == Wait ?!
n = 1, c + 10 = 11
```

Clang 给出了一个警告 `comparison of different enumeration types ('Color' and 'TrafficLight')`，但它只是警告，代码照样能编译。传统枚举有三个问题：

1. **名字污染**：枚举值直接暴露在外层作用域。`Color` 定义了 `Red`，`Fruit` 就不能再用 `Red`（报错 `redefinition of enumerator 'Red'`）。C 语言时代的习惯是加前缀，比如 `COLOR_RED`、`FRUIT_RED`。
2. **隐式转换成整数**：枚举可以直接赋给 `int`、参与算术运算，甚至两个无关的枚举可以互相比较。`Green == Wait` 是 `true`，因为它们的值都是 1。
3. **底层类型不确定**：枚举用什么整数类型存储由编译器决定，所以不能前置声明（`enum Color;` 是错误的），也无法控制它占几个字节。

## 2. enum class

C++11 的**强类型枚举**（scoped enumeration）在 `enum` 后面加上 `class`（或 `struct`）：

```cpp
#include <cstdint>
#include <iostream>

enum class Color { Red, Green, Blue };
enum class Fruit { Apple, Orange, Red };          // OK：名字在各自的作用域里，不冲突

enum class Level : std::uint8_t { Low = 1, Medium = 5, High = 10 };   // 指定底层类型

const char* toString(Color c)
{
    switch (c) {
        case Color::Red:   return "红";
        case Color::Green: return "绿";
        case Color::Blue:  return "蓝";
    }
    return "?";
}

int main()
{
    Color c = Color::Green;              // 必须带上枚举名
    Fruit f = Fruit::Red;
    // int n = c;                        // 错误：不能隐式转换成 int
    int n = static_cast<int>(c);         // 需要时显式转换
    // if (c == f) {}                    // 错误：不同类型不能比较
    std::cout << "c = " << toString(c) << ", 数值 " << n << '\n';
    std::cout << "Fruit::Red = " << static_cast<int>(f) << '\n';
    std::cout << "sizeof(Color) = " << sizeof(Color) << ", sizeof(Level) = " << sizeof(Level) << '\n';
    std::cout << "Level::High = " << static_cast<int>(Level::High) << '\n';
    return 0;
}
```

运行结果：

```
c = 绿, 数值 1
Fruit::Red = 2
sizeof(Color) = 4, sizeof(Level) = 1
Level::High = 10
```

三个问题全部解决：

| | 传统 `enum` | `enum class` |
| --- | --- | --- |
| 作用域 | 枚举值暴露在外层 | 必须写 `Color::Red` |
| 转换成整数 | 隐式 | 必须 `static_cast` |
| 不同枚举比较 | 可以（仅警告） | 编译错误 |
| 底层类型 | 编译器决定 | 默认 `int`，可以用 `: 类型` 指定 |
| 前置声明 | 不行（除非指定底层类型） | 可以：`enum class Color;` |

把注释掉的那几行打开，Clang 的报错：

```
error: cannot initialize a variable of type 'int' with an lvalue of type 'Color'
error: invalid operands to binary expression ('Color' and 'Fruit')
error: use of undeclared identifier 'Green'
```

指定底层类型在两种场合特别有用：

- **节省空间**：`Level` 只占 1 个字节，大量存储时（比如游戏地图的每个格子）能省下不少内存。
- **与外部格式对接**：文件格式、网络协议规定某个字段是 1 字节或 2 字节时，可以精确对应。

> C++11 也允许给传统 `enum` 指定底层类型（`enum Color : int { ... };`），这样它也能前置声明，但名字污染和隐式转换的问题依然存在。新代码一律用 `enum class`。

## 3. switch 与 enum class

`switch` 一个枚举时，如果漏掉了某个枚举值，编译器会给出警告（GCC 和 Clang 的 `-Wall` 包含 `-Wswitch`）：

```cpp
enum class State { Idle, Running, Stopped, Error };
const char* name(State s)
{
    switch (s) {
        case State::Idle:    return "空闲";
        case State::Running: return "运行";
        case State::Stopped: return "停止";
    }   // 漏掉了 State::Error
    return "?";
}
```

```
warning: enumeration value 'Error' not handled in switch [-Wswitch]
```

这个警告非常有用：以后给枚举增加新的值时，编译器会提醒你所有需要处理它的 `switch`。所以**对枚举做 `switch` 时不要写 `default`**，写了 `default` 编译器就不会再警告了。

## 4. static_assert：编译期断言

`static_assert(常量表达式, "错误信息");` 在**编译期**检查条件，不满足就编译失败并显示错误信息。它和运行期的 `assert` 互补：`assert` 检查程序运行时的状态，`static_assert` 检查编译时就能确定的事实，而且没有任何运行时开销。

```cpp
#include <cstdint>
#include <type_traits>

// 1. 检查平台假设
static_assert(sizeof(void*) == 8, "本程序只支持 64 位平台");
static_assert(sizeof(int) == 4, "要求 int 为 32 位");

// 2. 检查结构体布局：网络协议头必须正好 8 字节
struct PacketHeader {
    std::uint16_t type;
    std::uint16_t length;
    std::uint32_t sequence;
};
static_assert(sizeof(PacketHeader) == 8, "PacketHeader 必须是 8 字节");

// 3. 检查模板参数
template <typename T>
T average(T a, T b)
{
    static_assert(std::is_arithmetic<T>::value, "average 只能用于数值类型");
    return (a + b) / 2;
}

struct BadHeader {
    std::uint8_t flag;
    std::uint32_t sequence;     // 对齐要求导致 flag 后面有 3 字节填充
};
```

用字符串调用 `average`，Clang 的报错：

```
error: static assertion failed due to requirement 'std::is_arithmetic<std::string>::value': average 只能用于数值类型
```

没有 `static_assert` 时，同样的错误会在 `(a + b) / 2` 这一行报出来，错误信息是一大堆 `std::basic_string` 的模板展开，很难看懂。`static_assert` 让模板的使用者一眼就知道问题在哪里。

再看结构体布局。`BadHeader` 的两个成员加起来是 5 字节，但：

```cpp
static_assert(sizeof(BadHeader) == 5, "BadHeader 应该是 5 字节");
```

```
error: static assertion failed due to requirement 'sizeof(BadHeader) == 5': BadHeader 应该是 5 字节
note: expression evaluates to '8 == 5'
```

`uint32_t` 要求 4 字节对齐，编译器在 `flag` 后面插入了 3 个字节的**填充**。如果直接把这个结构体 `memcpy` 到网络数据包里，接收方按 5 字节解析就会出错。用 `static_assert` 锁定布局，就能在编译期发现这类问题。

`static_assert` 可以写在**任何地方**：命名空间作用域（全局）、类定义内部、函数内部。

## 5. 常见错误

1. **用 `enum class` 当数组下标**：`arr[Color::Red]` 无法编译，需要 `arr[static_cast<int>(Color::Red)]`。频繁转换时可以写一个辅助函数。
2. **用 `static_assert` 检查运行期的值**：条件必须是常量表达式，`static_assert(n > 0, "")` 中 `n` 是普通变量时无法编译。运行期检查用 `assert`。
3. **以为模板里的 `static_assert` 一定会触发**：模板中依赖模板参数的 `static_assert`，只有在模板被**实例化**时才会检查。从来没被调用过的函数模板，里面的断言不会失败。

## 6. 进阶：让 enum class 支持位标志

权限、选项这类「可以组合」的值，传统做法是用整数的每一位表示一个标志，再用 `|` 组合、用 `&` 检查。`enum class` 不能隐式转换成整数，也就不支持这些运算，需要自己重载：

```cpp
#include <cstdint>
#include <iostream>
#include <type_traits>

enum class Permission : std::uint8_t {
    None  = 0,
    Read  = 1 << 0,
    Write = 1 << 1,
    Exec  = 1 << 2,
};

// enum class 默认不支持 | 和 &，需要自己重载
constexpr Permission operator|(Permission a, Permission b)
{
    using U = std::underlying_type<Permission>::type;   // 取出底层类型 uint8_t
    return static_cast<Permission>(static_cast<U>(a) | static_cast<U>(b));
}
constexpr Permission operator&(Permission a, Permission b)
{
    using U = std::underlying_type<Permission>::type;
    return static_cast<Permission>(static_cast<U>(a) & static_cast<U>(b));
}
constexpr bool has(Permission set, Permission p) { return (set & p) != Permission::None; }

int main()
{
    Permission p = Permission::Read | Permission::Write;
    std::cout << std::boolalpha
              << "可读: " << has(p, Permission::Read)
              << ", 可写: " << has(p, Permission::Write)
              << ", 可执行: " << has(p, Permission::Exec) << '\n';
    static_assert(std::is_same<std::underlying_type<Permission>::type, std::uint8_t>::value, "");
    return 0;
}
```

运行结果：

```
可读: true, 可写: true, 可执行: false
```

要点：

- **`std::underlying_type<E>::type`** 得到枚举的底层类型，这样运算符就不必写死 `uint8_t`，以后修改底层类型也不用改运算符。C++14 可以简写为 `std::underlying_type_t<E>`，C++23 增加了 `std::to_underlying(e)`。
- **`Read | Write` 的结果（值为 3）不是任何一个枚举值，这是合法的**：枚举变量可以保存底层类型范围内的任何值。
- 运算符写成 `constexpr`，组合标志就可以在编译期完成，例如 `constexpr Permission rw = Permission::Read | Permission::Write;`。

和直接用整数常量相比，这样做的好处是类型安全：`has(p, 3)` 或者把 `Permission` 和别的枚举混在一起用，都会编译失败。

## 7. 小结

- 传统 `enum` 有名字污染、隐式转换成整数、底层类型不确定三个问题
- `enum class`：枚举值有作用域，不会隐式转换，可以指定底层类型，可以前置声明，**新代码一律使用**
- 对枚举 `switch` 时不写 `default`，让编译器帮你检查是否处理了所有值
- `static_assert(条件, "信息")` 在编译期检查平台假设、结构体布局、模板参数，没有运行时开销
- 需要位运算时，用 `std::underlying_type` 为 `enum class` 重载 `|`、`&`

## 8. 练习

1. 定义一个 `enum class Weekday`，写一个函数 `bool isWeekend(Weekday d)`，要求用 `switch` 实现，并且不写 `default`。
2. 下面的结构体在 64 位系统上 `sizeof` 是多少？调整成员顺序使它最小，并用 `static_assert` 验证。

   ```cpp
   struct Record {
       char tag;
       double value;
       int id;
   };
   ```

3. 写一个函数模板 `toBytes(T value)`，把 `value` 按字节复制到 `std::array<unsigned char, sizeof(T)>` 中返回，用 `static_assert` 确保 `T` 是可以安全 `memcpy` 的类型（提示：`std::is_trivially_copyable`）。

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   enum class Weekday { Mon, Tue, Wed, Thu, Fri, Sat, Sun };
   bool isWeekend(Weekday d)
   {
       switch (d) {
           case Weekday::Sat:
           case Weekday::Sun:
               return true;
           case Weekday::Mon: case Weekday::Tue: case Weekday::Wed:
           case Weekday::Thu: case Weekday::Fri:
               return false;
       }
       return false;
   }
   ```

2. 原顺序是 24 字节：`char`（1）+ 填充（7）+ `double`（8）+ `int`（4）+ 末尾填充（4，使整体大小是 8 的倍数）。按从大到小排成 `double value; int id; char tag;` 后是 16 字节：8 + 4 + 1 + 末尾填充 3。

   ```cpp
   struct Record { double value; int id; char tag; };
   static_assert(sizeof(Record) == 16, "Record 应为 16 字节");
   ```

3. 参考代码：

   ```cpp
   #include <array>
   #include <cstring>
   #include <type_traits>

   template <typename T>
   std::array<unsigned char, sizeof(T)> toBytes(const T& value)
   {
       static_assert(std::is_trivially_copyable<T>::value, "T 必须是可以平凡复制的类型");
       std::array<unsigned char, sizeof(T)> bytes;
       std::memcpy(bytes.data(), &value, sizeof(T));
       return bytes;
   }
   ```

</details>
