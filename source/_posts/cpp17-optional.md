---
title: std::optional：可能没有值的类型
date: 2026-10-25 11:00:00
updated: 2026-10-25 11:00:00
categories:
  - C/C++
  - C++17
tags:
  - C++17
  - 标准库
order: 3
description: 函数可能找不到结果、配置项可能没有填写，以前只能用 -1、空指针、bool 加输出参数来表示「没有值」。std::optional 把「有或没有」写进了类型里。本文讲清楚它的用法、内存开销、optional<bool> 的陷阱，以及它和其他方案的对比。
disableNunjucks: true
---

> **本文目标**：会用 `std::optional` 作为可能失败的函数的返回值和可选的成员变量；会用 `has_value`、`*`、`->`、`value`、`value_or` 访问值；知道 `std::optional` 的内存开销；避开 `optional<bool>` 和对空 optional 解引用的陷阱；能在 `optional`、指针、异常之间做出选择。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`，需要包含头文件 `<optional>`。

## 1. 问题：怎么表示「没有值」

写一个把字符串解析成整数的函数，输入可能不合法。C++17 之前常见的做法：

| 做法 | 例子 | 问题 |
| --- | --- | --- |
| 特殊值 | 失败返回 `-1` | `-1` 本身也可能是合法结果；调用者容易忘记检查 |
| 输出参数 | `bool parse(const string& s, int& out)` | 调用前要先声明变量，不能写成一个表达式 |
| 返回指针 | 失败返回 `nullptr` | 需要动态分配内存，还得考虑谁来释放 |
| 抛出异常 | 失败时 `throw` | 「解析失败」是很常见的正常情况，不是异常；异常的开销也较大 |
| 返回 `pair<bool, int>` | `{false, 0}` | `first`、`second` 没有语义；失败时也必须构造一个 `int` |

C++17 的 **`std::optional<T>`** 专门用来表示「**可能有一个 `T` 类型的值，也可能没有**」。

## 2. 基本用法

```cpp
#include <iostream>
#include <optional>
#include <string>
#include <vector>

// 把字符串解析成整数：可能失败，失败时返回“没有值”
std::optional<int> parseInt(const std::string& s)
{
    if (s.empty()) return std::nullopt;
    int result = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return std::nullopt;
        result = result * 10 + (c - '0');
    }
    return result;                       // 自动包装成 optional<int>
}

int main()
{
    for (const std::string s : {"123", "12a", ""}) {
        std::optional<int> r = parseInt(s);
        if (r)                                           // 等价于 r.has_value()
            std::cout << '"' << s << "\" -> " << *r << '\n';
        else
            std::cout << '"' << s << "\" -> 解析失败\n";
    }

    // value_or：没有值时使用默认值
    int port = parseInt("abc").value_or(8080);
    std::cout << "port = " << port << '\n';

    // value()：没有值时抛出异常
    try {
        int n = parseInt("x").value();
        std::cout << n << '\n';
    } catch (const std::bad_optional_access& e) {
        std::cout << "异常: " << e.what() << '\n';
    }
    return 0;
}
```

运行结果：

```
"123" -> 123
"12a" -> 解析失败
"" -> 解析失败
port = 8080
异常: bad_optional_access
```

| 操作 | 作用 |
| --- | --- |
| `return value;` | 返回一个有值的 optional（自动转换） |
| `return std::nullopt;` 或 `return {};` | 返回一个空的 optional |
| `if (opt)` / `opt.has_value()` | 判断是否有值 |
| `*opt` / `opt->member` | 访问值，**不检查**是否有值 |
| `opt.value()` | 访问值，没有值时抛出 `std::bad_optional_access` |
| `opt.value_or(默认值)` | 有值返回值，没有值返回默认值 |

函数签名 `std::optional<int> parseInt(...)` 本身就告诉了调用者「这个函数可能失败」，这是它比返回 `-1` 好的地方：**语义写进了类型里**，调用者不检查就拿不到 `int`。

## 3. 更多用法

```cpp
#include <iostream>
#include <optional>
#include <string>

struct User {
    std::string name;
    std::optional<std::string> nickname;     // 可选字段：比空字符串表达得更清楚
    std::optional<int> age;
};

void show(const User& u)
{
    std::cout << u.name;
    if (u.nickname) std::cout << "（昵称 " << *u.nickname << "，长度 " << u.nickname->size() << "）";
    std::cout << "，年龄 " << (u.age ? std::to_string(*u.age) : "未填写") << '\n';
}

int main()
{
    User a{"Alice", "小A", 20};
    User b{"Bob", std::nullopt, std::nullopt};
    show(a);
    show(b);

    std::optional<std::string> o;
    o.emplace(3, '*');                       // 原地构造，相当于 std::string(3, '*')
    std::cout << "emplace 后: " << *o << '\n';
    o.reset();                               // 清空
    std::cout << "reset 后 has_value: " << o.has_value() << '\n';

    std::optional<int> x = 5;
    std::cout << std::boolalpha
              << "x == 5: " << (x == 5) << ", x > 3: " << (x > 3)
              << ", nullopt < 0: " << (std::optional<int>() < 0) << '\n';   // 空值小于任何值
    return 0;
}
```

运行结果：

```
Alice（昵称 小A，长度 4），年龄 20
Bob，年龄 未填写
emplace 后: ***
reset 后 has_value: 0
x == 5: true, x > 3: true, nullopt < 0: true
```

- **可选的成员变量**：`nickname` 用 `optional<string>` 表示「可能没填」，比用空字符串表示「没填」更准确，因为空字符串也可能是用户真的填了一个空值。年龄没填时用 0 表示更是不对。
- **`->` 访问成员**：`u.nickname->size()` 直接调用 `string` 的成员函数。（「小A」的长度是 4，因为 `size()` 返回的是字节数，一个汉字在 UTF-8 中占 3 个字节。）
- **`emplace(参数...)`**：在 optional 内部直接构造对象，参数完美转发给 `T` 的构造函数。
- **`reset()`**：销毁内部的值，变为空。
- **比较**：optional 可以和另一个 optional 或 `T` 直接比较。空的 optional **等于** `std::nullopt`，并且**小于**任何有值的 optional。

## 4. 内存开销

`std::optional<T>` **不会动态分配内存**，值直接存放在 optional 对象内部，另外再用一个 `bool` 记录是否有值：

```cpp
#include <cstdio>
#include <optional>
#include <string>
int main()
{
    std::printf("int: %zu, optional<int>: %zu\n", sizeof(int), sizeof(std::optional<int>));
    std::printf("double: %zu, optional<double>: %zu\n", sizeof(double), sizeof(std::optional<double>));
    std::printf("bool: %zu, optional<bool>: %zu\n", sizeof(bool), sizeof(std::optional<bool>));
    std::printf("string: %zu, optional<string>: %zu\n", sizeof(std::string), sizeof(std::optional<std::string>));
    return 0;
}
```

运行结果（64 位，libc++）：

```
int: 4, optional<int>: 8
double: 8, optional<double>: 16
bool: 1, optional<bool>: 2
string: 24, optional<string>: 32
```

多出来的空间是那个 `bool` 加上为了对齐而填充的字节。`optional<double>` 从 8 字节变成了 16 字节，在大数组中存放时要注意这个开销。

和「返回指针」相比，optional 没有堆分配，数据也就在旁边，对缓存更友好。**optional 不是指针，它就是值本身**，拷贝 optional 会拷贝里面的值。

## 5. 常见错误

### 5.1 对空的 optional 解引用

`*opt` 和 `opt->` **不检查**是否有值，对空的 optional 使用它们是**未定义行为**，通常会读到一个未初始化的值，而且不会有任何提示。确定有值时（比如刚检查过）用 `*`，不确定时用 `value()` 或 `value_or()`。

### 5.2 optional&lt;bool&gt; 和 optional&lt;指针&gt;

```cpp
#include <iostream>
#include <optional>
std::optional<bool> isAdmin(int userId)
{
    if (userId == 1) return true;
    if (userId == 2) return false;
    return std::nullopt;             // 用户不存在
}
int main()
{
    for (int id : {1, 2, 3}) {
        auto r = isAdmin(id);
        std::cout << "用户 " << id << ": if (r) 为 " << (r ? "真" : "假")
                  << "，但真实结果是 " << (r ? (*r ? "管理员" : "普通用户") : "不存在") << '\n';
    }
    return 0;
}
```

运行结果：

```
用户 1: if (r) 为 真，但真实结果是 管理员
用户 2: if (r) 为 真，但真实结果是 普通用户
用户 3: if (r) 为 假，但真实结果是 不存在
```

用户 2 不是管理员，`if (r)` 却是真，因为 **`if (r)` 判断的是「有没有值」，而不是「值是不是 true」**。写成 `if (isAdmin(2))` 就会把普通用户当成管理员。`optional<bool>`、`optional<int*>` 这类本身就能在条件中使用的类型，判断时一定要写清楚：`if (r.has_value())` 和 `if (r && *r)`。很多时候，用一个 `enum class { Yes, No, Unknown }` 表达得更清楚。

### 5.3 不能存放引用

`std::optional<int&>` 无法编译，libc++ 的报错是：

```
error: static assertion failed due to requirement '!is_reference_v<int &>': instantiation of optional with a reference type is ill-formed
```

需要「可能为空的引用」时，用指针 `T*` 即可，指针本来就可以为空。C++26 将会支持 `std::optional<T&>`。

### 5.4 滥用 optional 表示错误

optional 只能表达「没有值」，**不能说明为什么没有**。解析失败时，调用者可能想知道是「字符串为空」还是「第 3 个字符不是数字」。需要错误信息时，可以返回下一篇介绍的 `std::variant<结果, 错误>`，或者 C++23 的 `std::expected<T, E>`。

## 6. 进阶

### 6.1 如何选择

| 场景 | 推荐 |
| --- | --- |
| 函数可能没有结果，这是正常情况，且调用者不需要知道原因 | `std::optional<T>` |
| 需要知道失败原因 | `std::expected<T, E>`（C++23）或 `std::variant` |
| 失败是真正的异常情况（文件损坏、内存不足） | 抛出异常 |
| 可能为空的「引用」，不拥有对象 | 原始指针 `T*` |
| 可能为空的「拥有」，对象很大或需要多态 | `std::unique_ptr<T>` |

### 6.2 延迟初始化

optional 可以用来表示「还没有初始化」的成员，在第一次使用时才构造，而且不需要堆分配：

```cpp
class Report {
public:
    const Data& data()
    {
        if (!cache_) cache_.emplace(loadExpensiveData());   // 第一次访问时才加载
        return *cache_;
    }
private:
    std::optional<Data> cache_;
};
```

### 6.3 C++23 的链式操作

C++23 给 optional 增加了 `and_then`、`transform`、`or_else` 三个成员函数，可以把多个可能失败的步骤串起来，不用写一层层的 `if`：

```cpp
// C++23
auto result = parseInt(input)
    .transform([](int n) { return n * 2; })     // 有值时变换
    .value_or(0);                                // 没有值时使用默认值
```

## 7. 小结

- `std::optional<T>` 表示「可能有一个 `T`，也可能没有」，把可能失败写进了函数签名
- `return std::nullopt;` 返回空值，`if (opt)` 判断，`*opt` / `->` 访问（不检查），`value()` 访问（检查并抛异常），`value_or()` 提供默认值
- 值直接存放在 optional 内部，不分配堆内存，额外开销是一个 `bool` 加对齐
- 对空 optional 解引用是未定义行为；`optional<bool>` 的 `if (opt)` 判断的是有没有值
- 不能存放引用；需要错误原因时用 `variant` 或 C++23 的 `expected`

## 8. 练习

1. 写一个函数 `std::optional<std::size_t> findIndex(const std::vector<int>& v, int target)`，返回 `target` 第一次出现的下标，找不到时返回空。
2. 写一个函数 `std::optional<std::string> getEnv(const char* name)`，封装 `std::getenv`（它在变量不存在时返回 `nullptr`），然后用 `value_or` 读取环境变量 `HOME`，不存在时默认为 `"/tmp"`。
3. 下面的代码有什么问题？

   ```cpp
   std::optional<int> maybe = compute();
   int x = *maybe + 1;
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   std::optional<std::size_t> findIndex(const std::vector<int>& v, int target)
   {
       for (std::size_t i = 0; i < v.size(); ++i)
           if (v[i] == target) return i;
       return std::nullopt;
   }
   ```

   下标 0 是合法结果，所以不能用 0 或 -1 表示「没找到」，optional 正好解决这个问题。

2. 参考代码：

   ```cpp
   std::optional<std::string> getEnv(const char* name)
   {
       if (const char* v = std::getenv(name)) return std::string(v);
       return std::nullopt;
   }

   std::string home = getEnv("HOME").value_or("/tmp");
   ```

3. 没有检查 `maybe` 是否有值就解引用，`compute()` 返回空值时是未定义行为。改成 `if (maybe) { int x = *maybe + 1; }`，或者 `int x = maybe.value_or(0) + 1;`，或者用 `maybe.value()` 让它在没有值时抛出异常。

</details>
