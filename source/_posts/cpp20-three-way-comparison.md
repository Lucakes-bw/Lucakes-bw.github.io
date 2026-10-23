---
title: 三路比较运算符 <=>
date: 2026-10-22 19:00:00
updated: 2026-10-22 19:00:00
categories:
  - C/C++
  - C++20
tags:
  - C++20
  - 运算符重载
order: 3
description: C++17 中要让一个类型支持全部 6 种比较，需要手写 6 个运算符；C++20 的「宇宙飞船运算符」<=> 加上 = default，一行就够了。本文讲清楚 <=> 的三种结果类型、自定义 <=> 时为什么还要写 ==，以及 C++20 的比较改写规则。
disableNunjucks: true
---

> **本文目标**：会用 `auto operator<=>(const T&) const = default;` 让类型支持全部比较；理解 `strong_ordering`、`weak_ordering`、`partial_ordering` 的区别；会手写 `<=>`，并知道此时为什么要单独提供 `==`；理解 C++20 自动改写比较表达式的规则。
>
> **编译环境**：`g++ -std=c++20` / `clang++ -std=c++20`，GCC 10+ / Clang 10+ / VS2019 16.7+，需要包含头文件 `<compare>`。

## 1. 问题：6 个比较运算符

要让一个版本号类型能比较大小、能排序、能放进 `std::set`，C++17 需要这样写：

```cpp
// C++17：要支持全部比较，需要手写 6 个运算符
struct Version {
    int major, minor, patch;
};
bool operator==(const Version& a, const Version& b) { return a.major == b.major && a.minor == b.minor && a.patch == b.patch; }
bool operator!=(const Version& a, const Version& b) { return !(a == b); }
bool operator<(const Version& a, const Version& b)
{
    if (a.major != b.major) return a.major < b.major;
    if (a.minor != b.minor) return a.minor < b.minor;
    return a.patch < b.patch;
}
bool operator>(const Version& a, const Version& b)  { return b < a; }
bool operator<=(const Version& a, const Version& b) { return !(b < a); }
bool operator>=(const Version& a, const Version& b) { return !(a < b); }
```

十几行几乎全是模板化的代码，而且很容易出错：比如 `operator<` 中漏比较了某个成员，或者 `<=` 写成了 `!(a < b)`。增加一个成员时，还要记得同时修改 `==` 和 `<`。

## 2. = default：一行搞定

C++20 引入了**三路比较运算符** `<=>`（因为长得像宇宙飞船，也叫 spaceship operator）。只需要一行：

```cpp
#include <algorithm>
#include <compare>
#include <iostream>
#include <string>
#include <vector>

struct Version {
    int major;
    int minor;
    int patch;
    auto operator<=>(const Version&) const = default;   // 一行生成全部 6 个比较运算符
};

std::ostream& operator<<(std::ostream& os, const Version& v)
{
    return os << v.major << '.' << v.minor << '.' << v.patch;
}

int main()
{
    Version a{1, 2, 3}, b{1, 10, 0}, c{1, 2, 3};
    std::cout << std::boolalpha
              << "a <  b: " << (a < b) << '\n'
              << "a == c: " << (a == c) << '\n'
              << "a != b: " << (a != b) << '\n'
              << "b >= a: " << (b >= a) << '\n';

    std::vector<Version> vs{{2, 0, 0}, {1, 10, 0}, {1, 2, 3}, {1, 2, 10}};
    std::sort(vs.begin(), vs.end());                    // 默认用 <，按成员顺序逐个比较
    for (const auto& v : vs) std::cout << v << "  ";
    std::cout << '\n';

    auto r = a <=> b;                                   // 三路比较本身的结果
    std::cout << "a <=> b 小于 0: " << (r < 0) << '\n';
    return 0;
}
```

运行结果：

```
a <  b: true
a == c: true
a != b: true
b >= a: true
1.2.3  1.2.10  1.10.0  2.0.0  
a <=> b 小于 0: true
```

`= default` 的 `<=>` 会**按成员的声明顺序，逐个比较**：先比 `major`，相等再比 `minor`，再比 `patch`，这正是字典序。`1.2.10` 排在 `1.10.0` 之前，说明它比较的是数值，而不是字符串。

**一个 `= default` 的 `<=>` 会让编译器同时生成 `==`**，所以 `<`、`<=`、`>`、`>=`、`==`、`!=` 六个运算符全部可用。

## 3. <=> 的结果

`a <=> b` 不返回 `bool`，而是返回一个**比较类别**类型的值，表示 a 小于、等于还是大于 b。这个结果可以直接和 `0` 比较：

| 想判断 | 写法 |
| --- | --- |
| a < b | `(a <=> b) < 0` |
| a == b | `(a <=> b) == 0` |
| a > b | `(a <=> b) > 0` |

这和 C 语言的 `strcmp` 返回负数、零、正数的约定一致。

### 3.1 三种比较类别

```cpp
#include <cmath>
#include <compare>
#include <iostream>
#include <string>

void show(const char* expr, std::partial_ordering r)
{
    std::cout << expr << " -> "
              << (r < 0 ? "less" : r > 0 ? "greater" : r == 0 ? "equivalent" : "unordered") << '\n';
}

int main()
{
    std::strong_ordering s = 3 <=> 5;
    std::cout << "3 <=> 5 是 less: " << (s == std::strong_ordering::less) << '\n';

    std::strong_ordering t = std::string("apple") <=> std::string("banana");
    std::cout << "\"apple\" <=> \"banana\" < 0: " << (t < 0) << '\n';

    double nan = std::nan("");
    show("1.0 <=> 2.0", 1.0 <=> 2.0);
    show("2.0 <=> 2.0", 2.0 <=> 2.0);
    show("1.0 <=> NaN", 1.0 <=> nan);                   // 浮点数是偏序：NaN 和任何数都“无法比较”
    std::cout << "NaN < 1: " << (nan < 1) << ", NaN > 1: " << (nan > 1) << ", NaN == NaN: " << (nan == nan) << '\n';
    return 0;
}
```

运行结果：

```
3 <=> 5 是 less: 1
"apple" <=> "banana" < 0: 1
1.0 <=> 2.0 -> less
2.0 <=> 2.0 -> equivalent
1.0 <=> NaN -> unordered
NaN < 1: 0, NaN > 1: 0, NaN == NaN: 0
```

| 类别 | 可能的值 | 含义 | 例子 |
| --- | --- | --- | --- |
| `std::strong_ordering` | less、equal、greater | 「相等」意味着**完全可以互相替换** | 整数、指针、`std::string` |
| `std::weak_ordering` | less、equivalent、greater | 「等价」的两个值不一定完全相同 | 不区分大小写的字符串：`"Hello"` 和 `"hello"` 等价，但不相同 |
| `std::partial_ordering` | less、equivalent、greater、**unordered** | 有些值之间**无法比较** | 浮点数：`NaN` 和任何数都无法比较 |

浮点数是偏序的典型例子：`NaN < 1`、`NaN > 1`、`NaN == NaN` 全部是 `false`。这也是为什么对包含 `NaN` 的数组排序会出问题。

`= default` 的 `<=>` 用 `auto` 作返回类型时，会根据成员的比较类别自动选择**最弱**的那个：成员全是 `int` 时是 `strong_ordering`，有一个 `double` 成员时就是 `partial_ordering`。

## 4. 自定义 <=>

需要特殊的比较规则时，可以自己实现 `<=>`。比如不区分大小写的字符串：

```cpp
#include <cctype>
#include <compare>
#include <iostream>
#include <string>

// 不区分大小写的字符串：自定义 <=>
class CaseInsensitive {
public:
    CaseInsensitive(std::string s) : s_(std::move(s)) {}

    std::weak_ordering operator<=>(const CaseInsensitive& other) const
    {
        std::size_t n = std::min(s_.size(), other.s_.size());
        for (std::size_t i = 0; i < n; ++i) {
            int a = std::tolower(static_cast<unsigned char>(s_[i]));
            int b = std::tolower(static_cast<unsigned char>(other.s_[i]));
            if (a != b) return a <=> b;
        }
        return s_.size() <=> other.s_.size();
    }
    bool operator==(const CaseInsensitive& other) const   // 自定义 <=> 时，== 需要单独提供
    {
        return (*this <=> other) == 0;
    }
private:
    std::string s_;
};

int main()
{
    CaseInsensitive a("Hello"), b("hello"), c("World");
    std::cout << std::boolalpha
              << "Hello == hello: " << (a == b) << '\n'
              << "Hello <  World: " << (a < c) << '\n'
              << "World >= hello: " << (c >= b) << '\n';
    return 0;
}
```

运行结果：

```
Hello == hello: true
Hello <  World: true
World >= hello: true
```

返回类型是 `std::weak_ordering`，因为 `"Hello"` 和 `"hello"` 只是等价，并不完全相同。`int` 之间的 `a <=> b` 返回 `strong_ordering`，可以隐式转换为更弱的 `weak_ordering`。

### 4.1 为什么要单独写 ==

**只有 `= default` 的 `<=>` 才会自动生成 `==`；自己实现的 `<=>` 不会**：

```cpp
#include <compare>
struct S {
    int v;
    std::strong_ordering operator<=>(const S& o) const { return v <=> o.v; }   // 自定义，不是 = default
};
int main()
{
    S a{1}, b{2};
    bool lt = a < b;      // OK：由 <=> 改写
    bool eq = a == b;     // 错误：自定义的 <=> 不会生成 ==
    return lt + eq;
}
```

```
error: invalid operands to binary expression ('S' and 'S')
```

这是故意的设计，原因是**性能**：对于 `vector`、`string` 这类类型，判断「是否相等」可以先比较长度，长度不同立即返回 `false`；而 `<=>` 必须找到第一个不同的元素才能确定大小关系。如果 `==` 总是由 `<=>` 实现，就会失去这个优化。所以 C++20 把「相等」和「排序」分成两组：

- `==` 和 `!=`：由 `operator==` 提供；
- `<`、`<=`、`>`、`>=`：由 `operator<=>` 提供。

如果你确定 `==` 用默认的逐成员比较就可以，写 `bool operator==(const S&) const = default;` 即可。

## 5. C++20 的比较改写规则

C++20 中，编译器会**自动改写**比较表达式：

- `a != b` 改写为 `!(a == b)`；
- `a < b` 改写为 `(a <=> b) < 0`，`<=`、`>`、`>=` 同理；
- **还会尝试交换两个操作数**：`b == a` 可以使用 `a == b` 的实现，`b < a` 可以使用 `(a <=> b) > 0`。

最后一条让「和其他类型比较」变得简单得多：

```cpp
#include <compare>
#include <iostream>

struct Money {
    long long cents;
    auto operator<=>(const Money&) const = default;
    bool operator==(long long c) const { return cents == c; }          // 只写了 Money == long long
    auto operator<=>(long long c) const { return cents <=> c; }        // 只写了 Money <=> long long
};

int main()
{
    Money m{500};
    std::cout << std::boolalpha
              << "m == 500: " << (m == 500) << '\n'
              << "500 == m: " << (500 == m) << '\n'      // C++20 自动尝试反过来的 m == 500
              << "m < 600:  " << (m < 600) << '\n'
              << "600 > m:  " << (600 > m) << '\n';      // 改写为 (m <=> 600) < 0
    return 0;
}
```

运行结果：

```
m == 500: true
500 == m: true
m < 600:  true
600 > m:  true
```

只写了 `Money` 在左边的版本，`500 == m`、`600 > m` 这样整数在左边的写法也能工作。C++17 中，每种比较、每种顺序都要单独写一个（通常还要写成友元函数），一共需要 12 个函数，现在只需要 2 个。

## 6. 常见错误

1. **成员的声明顺序就是比较的优先级**：`= default` 按声明顺序比较。如果 `Version` 的成员写成了 `patch, minor, major` 的顺序，比较结果就错了。需要不同的优先级时，自己实现 `<=>`。
2. **自定义 `<=>` 后忘记提供 `==`**：见第 4.1 节。
3. **把 `<=>` 的结果当作 `int`**：不能写 `if (a <=> b == -1)`，它返回的不是整数，只能和字面量 `0` 比较。
4. **对浮点成员使用 `= default` 后用于排序**：结果类型是 `partial_ordering`，遇到 `NaN` 时排序的结果是未定义的。

## 7. 进阶：在标准库中的应用

C++20 中，标准库的大部分类型都已经改用 `<=>` 实现比较：`std::string`、`std::vector`、`std::pair`、`std::tuple`、`std::optional` 等。这带来两个好处：

- 对于包含这些类型作为成员的结构体，`= default` 的 `<=>` 可以直接工作。
- 标准库提供了 `std::compare_three_way` 函数对象，以及 `std::lexicographical_compare_three_way` 算法，可以对两个序列做一次三路比较，而不是分别判断小于和等于。

另外，如果某个成员类型只有 `<` 和 `==`、没有 `<=>`（比如老代码中的类），`= default` 的 `auto operator<=>` 会被**隐式删除**，Clang 会给出警告 `explicitly defaulted three-way comparison operator is implicitly deleted`，真正使用比较运算时编译失败。这时可以明确写出返回类型，例如 `std::weak_ordering operator<=>(const T&) const = default;`，编译器会用成员的 `<` 和 `==` 合成比较结果。

## 8. 小结

- `auto operator<=>(const T&) const = default;` 一行生成全部 6 个比较运算符，按成员声明顺序做字典序比较
- `a <=> b` 的结果和 0 比较：`< 0` 表示小于、`== 0` 表示等于、`> 0` 表示大于
- 三种结果类别：`strong_ordering`（相等即相同）、`weak_ordering`（等价不一定相同）、`partial_ordering`（可能无法比较，如 NaN）
- 只有 `= default` 的 `<=>` 会生成 `==`；自定义 `<=>` 时要单独提供 `==`，这是出于性能考虑
- C++20 会自动改写比较表达式，并尝试交换操作数，和其他类型比较时只需写一个方向

## 9. 练习

1. 定义一个 `Date` 结构体（年、月、日），让它支持全部比较运算，并用它对一组日期排序。
2. 定义一个 `Student`（姓名、分数），要求按**分数从高到低**排序，分数相同时按姓名升序。用自定义的 `<=>` 实现。
3. 下面的代码能否编译？

   ```cpp
   struct P {
       double x, y;
       auto operator<=>(const P&) const = default;
   };
   std::strong_ordering r = P{1, 2} <=> P{1, 3};
   ```

<details>
<summary>点击查看答案</summary>

1. 成员按「年、月、日」的顺序声明，`= default` 就能正确比较：

   ```cpp
   struct Date {
       int year, month, day;
       auto operator<=>(const Date&) const = default;
   };
   std::vector<Date> dates{{2024, 5, 1}, {2023, 12, 31}, {2024, 1, 15}};
   std::sort(dates.begin(), dates.end());
   ```

2. 参考代码，分数比较时交换左右顺序实现降序：

   ```cpp
   struct Student {
       std::string name;
       int score;
       std::strong_ordering operator<=>(const Student& o) const
       {
           if (auto c = o.score <=> score; c != 0) return c;   // 分数降序
           return name <=> o.name;                             // 姓名升序
       }
       bool operator==(const Student& o) const = default;
   };
   ```

3. 不能。成员是 `double`，`= default` 推导出的返回类型是 `std::partial_ordering`，它不能隐式转换为更强的 `std::strong_ordering`。应该写 `std::partial_ordering r = ...;` 或 `auto r = ...;`。

</details>
