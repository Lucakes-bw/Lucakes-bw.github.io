---
title: 返回类型推导与 decltype(auto)
date: 2026-10-24 14:00:00
updated: 2026-10-24 14:00:00
categories:
  - C/C++
  - C++14
tags:
  - C++14
  - 类型推导
order: 2
description: C++14 中普通函数的返回类型也可以写 auto，由 return 语句推导；decltype(auto) 则在推导时保留引用和 const，是编写转发函数的利器。本文讲清楚推导规则、限制，以及 decltype(auto) 多一对括号就悬空的陷阱。
disableNunjucks: true
---

> **本文目标**：会用 `auto` 作为函数返回类型；知道多条 `return` 和递归时的规则；分清 `auto` 和 `decltype(auto)` 的推导结果；会用 `decltype(auto)` 写原样返回的转发函数；避开 `return (x);` 返回悬空引用的陷阱。
>
> **前置知识**：C++11 章节的《auto 与 decltype》。
>
> **编译环境**：`g++ -std=c++14` / `clang++ -std=c++14`。

## 1. 函数返回类型推导

C++11 中，lambda 的返回类型可以由编译器推导，普通函数却不行，只能写出具体类型，或者用尾置返回类型 `auto f() -> decltype(...)`。C++14 让普通函数也能**只写 `auto`**，返回类型由 `return` 语句推导：

```cpp
#include <iostream>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

// 1. 返回类型由 return 语句推导
auto add(int a, double b) { return a + b; }            // double

// 2. 模板中特别有用：不用再写 -> decltype(t + u)
template <typename T, typename U>
auto multiply(T t, U u) { return t * u; }

// 3. 返回复杂的类型，比如迭代器
template <typename Map>
auto findOrEnd(Map& m, const typename Map::key_type& key) { return m.find(key); }

// 4. 递归：第一条 return 必须先出现，用来确定返回类型
auto factorial(int n)
{
    if (n <= 1) return 1;                 // 先确定为 int
    return n * factorial(n - 1);          // 之后才能递归调用
}

int main()
{
    static_assert(std::is_same<decltype(add(1, 2.0)), double>::value, "");
    static_assert(std::is_same<decltype(multiply(2, 3L)), long>::value, "");
    std::cout << add(1, 2.5) << ' ' << multiply(2, 3.5) << ' ' << factorial(10) << '\n';

    std::map<std::string, int> ages{{"Alice", 20}};
    auto it = findOrEnd(ages, "Alice");
    std::cout << it->first << " " << it->second << '\n';
    return 0;
}
```

运行结果：

```
3.5 7 3628800
Alice 20
```

和 C++11 的写法对比：

```cpp
// C++11：必须用尾置返回类型，并且把 return 的表达式再写一遍
template <typename T, typename U>
auto multiply(T t, U u) -> decltype(t * u) { return t * u; }

// C++14：直接写 auto
template <typename T, typename U>
auto multiply(T t, U u) { return t * u; }
```

**推导规则和 `auto` 变量完全相同**：`return` 表达式的引用和顶层 `const` 会被丢掉，所以 `auto` 返回类型**永远是按值返回**。

## 2. 规则和限制

### 2.1 多条 return 必须推导出相同的类型

```cpp
auto pick(bool flag)
{
    if (flag) return 1;       // int
    return 2.0;               // double：与上一个不一致
}
```

```
error: 'auto' in return type deduced as 'double' here but deduced as 'int' in earlier return statement
```

编译器不会帮你找一个「公共类型」，每条 `return` 推导出的类型必须**完全一致**。可以统一成 `return 1.0;`，或者干脆写出返回类型 `double pick(bool flag)`。

### 2.2 递归调用之前，必须先有一条 return

```cpp
auto fact(int n)
{
    if (n > 1) return n * fact(n - 1);   // 错误：此时还不知道 fact 的返回类型
    return 1;
}
```

```
error: function 'fact' with deduced return type cannot be used before it is defined
```

编译器从上往下处理函数体，遇到递归调用 `fact(n - 1)` 时，还没见过任何 `return`，不知道 `fact` 返回什么。把非递归的 `return 1;` 放到前面（像第 1 节的 `factorial` 那样），问题就解决了。

### 2.3 其他限制

- **使用之前必须看到函数定义**。只有声明 `auto f();` 是不够的，编译器需要函数体才能推导类型。所以返回类型为 `auto` 的普通函数，定义通常放在头文件里（模板本来就是这样）。
- **虚函数不能用 `auto` 返回类型**。
- **不能返回花括号列表**：`auto f() { return {1, 2, 3}; }` 是错误的。（`auto x = {1, 2, 3};` 可以推导成 `initializer_list`，但返回类型推导不支持这种情况。）

### 2.4 什么时候用

返回类型推导主要用于**泛型代码**和**返回类型很复杂**的场合（迭代器、lambda、各种模板类型）。对于普通的、对外公开的函数，**明确写出返回类型更好**：调用者看声明就知道函数返回什么，不需要去读函数体。

## 3. decltype(auto)

### 3.1 auto 会丢掉引用

```cpp
#include <iostream>
#include <type_traits>
#include <vector>

std::vector<int> data{1, 2, 3};

auto getAuto(std::size_t i) { return data[i]; }            // auto：丢掉引用，返回 int 的副本
decltype(auto) getRef(std::size_t i) { return data[i]; }   // decltype(auto)：保留引用，返回 int&

int main()
{
    static_assert(std::is_same<decltype(getAuto(0)), int>::value, "");
    static_assert(std::is_same<decltype(getRef(0)), int&>::value, "");

    // getAuto(0) = 100;      // 错误：不能给临时值赋值
    getRef(0) = 100;          // OK：修改的是 data[0]
    std::cout << "data[0] = " << data[0] << '\n';

    int x = 5;
    auto a = x;               // int
    decltype(auto) b = x;     // int（x 是变量名）
    decltype(auto) c = (x);   // int&（(x) 是左值表达式）
    c = 50;
    static_assert(std::is_same<decltype(a), int>::value, "");
    static_assert(std::is_same<decltype(b), int>::value, "");
    static_assert(std::is_same<decltype(c), int&>::value, "");
    std::cout << "x = " << x << '\n';
    return 0;
}
```

运行结果：

```
data[0] = 100
x = 50
```

`data[i]` 的类型是 `int&`，但 `auto` 返回类型把引用丢掉了，`getAuto` 返回的是一个副本。如果想返回引用，可以写 `auto&`，但那样就**只能**返回引用了。

**`decltype(auto)`** 的意思是：**用 `decltype` 的规则来推导**。回顾《auto 与 decltype》中的规则：

- `decltype(变量名)`：变量声明时的类型；
- `decltype(表达式)`：左值表达式得到 `T&`，纯右值得到 `T`。

所以 `return data[i];` 推导出 `int&`，`return a + b;` 推导出 `int`。**表达式本身是什么类型，就原样返回什么类型。**

`decltype(auto)` 也可以用来声明变量，规则相同。上例中 `decltype(auto) b = x;` 是 `int`，而 `decltype(auto) c = (x);` 是 `int&`，就因为多了一对括号。

### 3.2 用途：原样返回的转发函数

`decltype(auto)` 最主要的用途，是编写**包装函数**：被包装的函数返回引用，包装函数也返回引用；返回值，包装函数也返回值。

```cpp
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

// 一个通用的“调用并记录日志”包装器：被调用函数返回什么，它就原样返回什么
template <typename F, typename... Args>
decltype(auto) logCall(const char* name, F&& f, Args&&... args)
{
    std::cout << "[调用 " << name << "]\n";
    return std::forward<F>(f)(std::forward<Args>(args)...);
}

std::vector<int> v{10, 20, 30};
int& at(std::size_t i) { return v[i]; }
int sum(int a, int b) { return a + b; }

int main()
{
    logCall("at", at, 1) = 99;              // 返回 int&，可以赋值
    std::cout << "v[1] = " << v[1] << '\n';
    int s = logCall("sum", sum, 3, 4);      // 返回 int
    std::cout << "sum = " << s << '\n';
    return 0;
}
```

运行结果：

```
[调用 at]
v[1] = 99
[调用 sum]
sum = 7
```

C++11 的《可变参数模板》中，同样的包装函数要写成 `auto logged(...) -> decltype(std::forward<F>(f)(std::forward<Args>(args)...))`，把整个调用表达式写两遍。`decltype(auto)` 一个关键字就解决了。

参数完美转发（`std::forward`），返回值原样返回（`decltype(auto)`），两者配合，包装函数就对调用者完全「透明」。

## 4. 常见错误：多一对括号就悬空

```cpp
#include <iostream>
decltype(auto) bad()
{
    int local = 42;
    return (local);           // 多了一对括号：返回 int&，指向即将销毁的局部变量！
}
int main()
{
    int& r = bad();
    std::cout << r << '\n';
    return 0;
}
```

Clang 给出了警告：

```
warning: reference to stack memory associated with local variable 'local' returned [-Wreturn-stack-address]
```

用 AddressSanitizer 运行：

```
ERROR: AddressSanitizer: stack-use-after-return
```

`return local;` 中 `local` 是变量名，`decltype(local)` 是 `int`，函数返回 `int`，没有问题；而 `return (local);` 中 `(local)` 是左值表达式，`decltype((local))` 是 `int&`，函数返回了局部变量的**引用**，局部变量在函数返回时就销毁了。

在 `auto` 返回类型中，括号没有任何影响；而在 `decltype(auto)` 中，**`return x;` 和 `return (x);` 的含义完全不同**。规则：

- 使用 `decltype(auto)` 返回类型时，**不要给 return 的表达式加括号**；
- **只在确实需要「原样转发返回值」时才用 `decltype(auto)`**，其他场合用 `auto`。

## 5. 进阶：auto、auto& 与 decltype(auto) 的选择

| 返回类型 | `return data[i];`（`int&`） | `return a + b;`（纯右值 `int`） | `return local;` | 适用场景 |
| --- | --- | --- | --- | --- |
| `auto` | `int` | `int` | `int` | 返回值（最常用） |
| `auto&` | `int&` | 编译错误 | 悬空引用 | 一定返回某个长期存在对象的引用 |
| `const auto&` | `const int&` | 悬空引用（引用了临时对象） | 悬空引用 | 一定返回只读引用 |
| `decltype(auto)` | `int&` | `int` | `int` | 原样转发另一个函数的返回值 |

可以看到 `decltype(auto)` 在三种情况下都「做对了」，但它的正确性依赖于你**不写多余的括号**，并且被转发的函数本身返回的引用是安全的。

另外，lambda 也可以用 `decltype(auto)` 作为返回类型：`[](auto& c) -> decltype(auto) { return c[0]; }` 返回容器第一个元素的引用。

## 6. 小结

- C++14 中，普通函数的返回类型可以写 `auto`，按 `auto` 变量的规则从 `return` 推导，**总是按值返回**
- 多条 `return` 必须推导出相同的类型；递归调用之前必须已经有一条 `return`；使用前必须看到定义；虚函数不能用
- **`decltype(auto)`** 按 `decltype` 规则推导，**保留引用和 const**，用于原样转发返回值
- 使用 `decltype(auto)` 时，**`return (x);` 会返回引用**，作用于局部变量时会悬空
- 对外公开的普通函数，明确写出返回类型通常更清晰

## 7. 练习

1. 下面每个函数的返回类型是什么？

   ```cpp
   std::string name = "lucky";
   auto f1() { return name; }
   auto& f2() { return name; }
   decltype(auto) f3() { return name; }
   decltype(auto) f4() { return (name); }
   decltype(auto) f5() { return name.size(); }
   ```

2. 写一个函数模板 `first(c)`，返回任意容器的第一个元素，要求对 `vector<int>` 返回 `int&`（可以修改），对 `const vector<int>` 返回 `const int&`。

3. 下面的代码能编译吗？

   ```cpp
   auto sign(int x)
   {
       if (x > 0) return 1;
       if (x < 0) return -1;
       return 0u;
   }
   ```

<details>
<summary>点击查看答案</summary>

1. `f1`：`std::string`（拷贝）；`f2`：`std::string&`；`f3`：`std::string`（`name` 是变量名，`decltype(name)` 是 `std::string`）；`f4`：`std::string&`（`(name)` 是左值表达式，这里 `name` 是全局变量，所以不会悬空）；`f5`：`std::size_t`（`size()` 返回值，纯右值）。
2. 用 `decltype(auto)` 原样返回 `c.front()` 的结果：

   ```cpp
   template <typename Container>
   decltype(auto) first(Container& c) { return c.front(); }
   ```

   `c` 是 `vector<int>&` 时 `front()` 返回 `int&`，是 `const vector<int>&` 时返回 `const int&`。也可以写 `auto&`，在这个例子里效果相同。

3. 不能。前两条 `return` 推导出 `int`，第三条是 `unsigned int`，类型不一致。把 `0u` 改成 `0`。

</details>
