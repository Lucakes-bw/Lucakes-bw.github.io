---
title: if/switch 初始化语句与 if constexpr
date: 2026-10-22 15:40:00
updated: 2026-10-22 15:40:00
categories:
  - C/C++
  - C++17
tags:
  - C++17
  - 模板
order: 2
description: C++17 的 if 和 switch 可以带一条初始化语句，让临时变量的作用域恰好覆盖条件语句；if constexpr 在编译期选择分支，被丢弃的分支不会被实例化，大大简化了模板代码。
disableNunjucks: true
---

> **本文目标**：会写 `if (初始化; 条件)` 和 `switch (初始化; 条件)`，用它缩小变量的作用域；理解普通 `if` 在模板中为什么会编译失败；会用 `if constexpr` 根据类型选择不同的实现；能用 `if constexpr` 代替递归终点函数；知道 `if constexpr` 的几个陷阱。
>
> **前置知识**：C++11 章节的《可变参数模板》，类型特征（`std::is_same` 等）。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。

## 1. 带初始化语句的 if 和 switch

### 1.1 问题：变量的作用域太大

在 `map` 中查找一个元素，C++11 的常见写法是：

```cpp
auto it = stock.find(name);
if (it != stock.end()) {
    // 使用 it
}
// it 在这里仍然可见，但已经没有用了，还可能被误用
```

`it` 只在 `if` 中有用，却在整个外层作用域里都可见。如果同一个函数中还要查找别的东西，就得起 `it2`、`it3` 这样的名字。

### 1.2 if (初始化; 条件)

C++17 允许 `if` 和 `switch` 带一条初始化语句，用分号与条件隔开：

```cpp
if (初始化语句; 条件) { ... } else { ... }
switch (初始化语句; 条件) { ... }
```

初始化语句中声明的变量，**作用域覆盖整个 `if`（包括所有 `else if` 和 `else` 分支）或整个 `switch`**，语句结束后就销毁。

```cpp
#include <iostream>
#include <map>
#include <mutex>
#include <string>

std::map<std::string, int> stock{{"apple", 5}, {"pear", 0}};
std::mutex m;

enum class Status { Ok, NotFound, Empty };

Status check(const std::string& name)
{
    // 1. if 初始化：it 只在 if/else 中可见
    if (auto it = stock.find(name); it == stock.end())
        return Status::NotFound;
    else if (it->second == 0)
        return Status::Empty;
    return Status::Ok;
}

int main()
{
    for (const char* name : {"apple", "pear", "banana"}) {
        // 2. switch 初始化
        switch (Status s = check(name); s) {
            case Status::Ok:       std::cout << name << ": 有货\n"; break;
            case Status::Empty:    std::cout << name << ": 缺货\n"; break;
            case Status::NotFound: std::cout << name << ": 不存在\n"; break;
        }
    }

    // 3. 锁的作用域正好覆盖 if 语句
    if (std::lock_guard<std::mutex> lock(m); stock["apple"] > 0) {
        --stock["apple"];
        std::cout << "卖出一个 apple，剩余 " << stock["apple"] << '\n';
    }   // lock 在这里析构
    return 0;
}
```

运行结果：

```
apple: 有货
pear: 缺货
banana: 不存在
卖出一个 apple，剩余 4
```

三个典型用法：

1. **查找后判断**：`it` 在 `else if` 分支中也能用，离开 `if` 语句就不可见了。
2. **先计算再分支**：`switch (Status s = check(name); s)`，`s` 只属于这个 `switch`。
3. **锁的作用域**：`lock_guard` 的生命周期恰好是整个 `if` 语句，检查和修改都在锁的保护下，离开 `if` 自动解锁。以前要达到同样的效果，只能额外套一对花括号。

上一篇结构化绑定中的 `if (auto [it, inserted] = ages.insert(...); inserted)` 也是这种写法。

> C++20 进一步允许范围 for 带初始化语句：`for (auto v = getData(); auto x : v)`。

## 2. if constexpr

### 2.1 问题：普通 if 的两个分支都要能编译

写一个把各种值转换成字符串的函数模板：数值用 `std::to_string`，字符串原样返回。直观的写法是：

```cpp
#include <string>
#include <type_traits>
template <typename T>
std::string toString(const T& value)
{
    if (std::is_arithmetic<T>::value)       // 普通 if：两个分支都会被编译
        return std::to_string(value);
    else
        return value;
}
int main()
{
    toString(42);                 // T = int：else 分支 return value; 无法把 int 转成 string
    toString(std::string("hi"));  // T = string：to_string(string) 不存在
}
```

Clang 的报错：

```
error: no viable conversion from returned value of type 'const int' to function return type 'std::string'
error: no matching function for call to 'to_string'
```

条件 `std::is_arithmetic<T>::value` 在编译期就是确定的，但**普通 `if` 只在运行时选择分支，编译时两个分支都必须合法**。`T` 是 `int` 时，`else` 分支的 `return value;` 要把 `int` 转换成 `string`，编译失败；`T` 是 `string` 时，`std::to_string(string)` 不存在，同样编译失败。

C++17 之前，解决这个问题需要用函数重载、标签分发（tag dispatch）或 `std::enable_if`，代码复杂又难读。

### 2.2 if constexpr：编译期选择分支

C++17 的 **`if constexpr (条件)`** 要求条件是编译期常量，并且**在模板中，不满足条件的分支会被丢弃，根本不会被实例化**：

```cpp
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

template <typename T>
std::string toString(const T& value)
{
    if constexpr (std::is_same_v<T, std::string>)
        return value;                                   // 只有 T 是 string 时才编译这一行
    else if constexpr (std::is_arithmetic_v<T>)
        return std::to_string(value);                   // 只有 T 是数值时才编译这一行
    else if constexpr (std::is_same_v<T, std::vector<int>>) {
        std::string s = "[";
        for (std::size_t i = 0; i < value.size(); ++i)
            s += (i ? ", " : "") + std::to_string(value[i]);
        return s + "]";
    } else
        return "<未知类型>";
}

// 可变参数模板不再需要单独的递归终点函数
template <typename T, typename... Rest>
void print(const T& first, const Rest&... rest)
{
    std::cout << toString(first);
    if constexpr (sizeof...(rest) > 0) {   // 包为空时，这个分支根本不会被实例化
        std::cout << ", ";
        print(rest...);
    }
    std::cout << (sizeof...(rest) == 0 ? "\n" : "");
}

struct Unknown {};

int main()
{
    print(42, 3.5, std::string("text"), std::vector<int>{1, 2, 3}, Unknown{});
    return 0;
}
```

运行结果：

```
42, 3.500000, text, [1, 2, 3], <未知类型>
```

`T` 为 `int` 时，编译器实际看到的函数体只有 `return std::to_string(value);` 这一行，其他分支被完全丢弃，所以不会出错。可以把 `if constexpr` 理解为**作用于模板的条件编译**，但它比 `#if` 强大得多：条件可以依赖模板参数。

`std::is_same_v<T, U>` 是 C++17 新增的简写，等价于 `std::is_same<T, U>::value`。所有返回 `bool` 的类型特征都有对应的 `_v` 版本。

### 2.3 简化可变参数模板

`print` 函数展示了 `if constexpr` 的另一个用途：C++11 章节《可变参数模板》中，递归处理参数包需要一个单独的、无参数的终点函数 `print()`，而且它必须声明在模板之前。用 `if constexpr (sizeof...(rest) > 0)` 判断还有没有剩余参数，包为空时递归调用所在的分支被丢弃，**不再需要终点函数**。

## 3. 常见错误

### 3.1 不写 else，后面的代码照样会被实例化

```cpp
#include <type_traits>
template <typename T>
auto getValue(T t)
{
    if constexpr (std::is_pointer_v<T>)
        return *t;
    return t;           // 没有写 else：T 是指针时，这一行也会被实例化
}
int main()
{
    int x = 1;
    return getValue(&x);
}
```

```
error: 'auto' in return type deduced as 'int *' here but deduced as 'int' in earlier return statement
```

`if constexpr` 只丢弃它**自己的分支**。没有写 `else` 时，`return t;` 不属于任何分支，无论条件如何都会被编译。`T` 是 `int*` 时，函数中有两条 `return`，分别推导出 `int` 和 `int*`，于是报错。**使用 `if constexpr` 时，互斥的代码一定要放在 `else` 中**。

### 3.2 只在模板中才会「丢弃」

```cpp
int main()
{
    if constexpr (sizeof(int) == 2)
        undeclared_function();       // 不在模板中：被丢弃的分支仍然要能通过语法和名字检查
    return 0;
}
```

```
error: use of undeclared identifier 'undeclared_function'
```

只有在**模板中、并且条件依赖模板参数**时，被丢弃的分支才不会被实例化。在普通函数中，被丢弃的分支虽然不会执行，也不会生成代码，但仍然要完整地通过编译检查。所以 `if constexpr` **不能**代替 `#if` 去处理「某个平台上根本不存在的函数」。

### 3.3 static_assert(false) 的历史问题

想在所有分支都不匹配时报错，自然会写：

```cpp
template <typename T>
int convert(T)
{
    if constexpr (std::is_integral_v<T>)
        return 1;
    else
        static_assert(false, "convert 只支持整数类型");
}
```

按照最初的 C++17 标准，**`static_assert(false)` 即使在被丢弃的分支中也会立即触发**，因为它的条件不依赖模板参数，在模板定义时就能确定为假。这导致上面的代码无论怎么调用都编译失败。

C++23 的提案 P2593 修正了这个问题：被丢弃分支中的 `static_assert(false)` 不再触发。GCC 13、Clang 17 起在所有标准模式下都采用了这个新规则，所以本文用 Clang 21 以 `-std=c++17` 编译时，`convert(1)` 能通过，`convert(2.0)` 会报错：

```
error: static assertion failed: convert 只支持整数类型
```

如果需要支持较旧的编译器，常用的写法是让条件**依赖模板参数**：

```cpp
template <typename> inline constexpr bool always_false = false;

// ...
    else
        static_assert(always_false<T>, "convert 只支持整数类型");
```

`always_false<T>` 的值只有在实例化时才能确定，所以只会在真正进入这个分支时才触发。

## 4. 进阶：if constexpr 与 SFINAE

C++17 之前，根据类型选择实现，常用的技巧是 **SFINAE**（替换失败不是错误）配合 `std::enable_if`：

```cpp
// C++11/14：为数值类型和字符串分别写一个重载，用 enable_if 让不匹配的那个“消失”
template <typename T>
typename std::enable_if<std::is_arithmetic<T>::value, std::string>::type
toString(const T& v) { return std::to_string(v); }

template <typename T>
typename std::enable_if<std::is_same<T, std::string>::value, std::string>::type
toString(const T& v) { return v; }
```

每种情况都要写一个单独的重载，返回类型被 `enable_if` 弄得难以阅读，条件之间还必须互斥，否则会产生二义性。`if constexpr` 把所有情况写在一个函数里，按顺序判断，可读性好得多。

两者也有适用范围的区别：

| | `if constexpr` | SFINAE / `enable_if` |
| --- | --- | --- |
| 作用位置 | 函数**内部**，选择实现 | 函数**签名**上，决定这个重载是否参与重载决议 |
| 能否让「调用本身」不合法 | 不能，函数总是存在 | 能，不匹配时这个重载不存在 |
| 可读性 | 好 | 差 |

需要「某些类型根本不能调用这个函数」，并且让调用者看到「没有匹配的函数」这样的错误时，C++17 中仍然需要 SFINAE；C++20 的 **Concepts** 为此提供了更好的写法，会在 C++20 章节介绍。

## 5. 小结

- `if (初始化; 条件)` 和 `switch (初始化; 条件)`：初始化的变量作用域恰好覆盖整个语句，适合查找结果、加锁等场合
- 普通 `if` 在模板中，两个分支都必须对所有类型合法
- **`if constexpr`**：条件必须是编译期常量；在模板中，被丢弃的分支**不会被实例化**
- 用 `if constexpr (sizeof...(rest) > 0)` 可以省去可变参数模板的递归终点函数
- 互斥的代码要放在 `else` 中；非模板代码中被丢弃的分支仍需能通过编译
- C++17 新增 `std::is_same_v` 等 `_v` 简写

## 6. 练习

1. 用 if 初始化语句改写：

   ```cpp
   auto pos = s.find(':');
   if (pos != std::string::npos) {
       std::cout << "key = " << s.substr(0, pos) << '\n';
   }
   ```

2. 写一个函数模板 `length(const T& x)`：`T` 是 `std::string` 时返回字符数，`T` 是 `const char*` 时返回 `std::strlen(x)`，`T` 是有 `size()` 成员的容器时返回 `size()`（提示：容器的判断可以先简化为「不是前两种」）。

3. 下面的代码用 `T = int` 实例化时能否编译？

   ```cpp
   template <typename T>
   void f(T x)
   {
       if constexpr (std::is_pointer_v<T>)
           std::cout << *x;
       else
           std::cout << x;
   }
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   if (auto pos = s.find(':'); pos != std::string::npos)
       std::cout << "key = " << s.substr(0, pos) << '\n';
   ```

2. 参考代码：

   ```cpp
   template <typename T>
   std::size_t length(const T& x)
   {
       if constexpr (std::is_same_v<T, std::string>)
           return x.size();
       else if constexpr (std::is_same_v<T, const char*>)
           return std::strlen(x);
       else
           return x.size();
   }
   ```

   注意 `length("abc")` 中 `T` 被推导为 `char[4]` 而不是 `const char*`（按引用传递时数组不会退化），需要再加一个 `std::is_array_v<T>` 的分支，或者调用时写 `length(static_cast<const char*>("abc"))`。

3. 能。`T = int` 时 `std::is_pointer_v<int>` 为 `false`，`*x` 所在的分支被丢弃，不会实例化，所以不会出现「对 int 解引用」的错误。如果改成普通 `if`，就会编译失败。

</details>
