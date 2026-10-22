---
title: 可变参数模板
date: 2026-10-23 16:00:00
updated: 2026-10-23 16:00:00
categories:
  - C/C++
  - C++11
tags:
  - C++11
  - 模板
order: 12
description: 可变参数模板让函数和类接受任意个、任意类型的参数，是 emplace_back、make_unique、std::tuple、std::thread 的基础。本文讲清楚参数包、展开模式、递归处理，以及 C++11 中不用递归的展开技巧。
disableNunjucks: true
---

> **本文目标**：会写接受任意个参数的函数模板；理解参数包和包展开；会用递归处理参数包；会用「数组初始化」技巧在 C++11 中不递归地展开参数包；会把参数包完美转发给其他函数；会用 `std::tuple` 返回多个值。
>
> **前置知识**：函数模板，《万能引用与完美转发》。
>
> **编译环境**：`g++ -std=c++11` / `clang++ -std=c++11`。

## 1. 为什么需要可变参数模板

想写一个 `print` 函数，能打印任意个、任意类型的参数：

```cpp
print(1, 2.5, "three");
print("只有一个参数");
```

C 语言的 `printf` 用的是 `...`（可变参数列表），但它**没有类型安全**：参数的类型全靠格式字符串说明，`printf("%d", 3.14)` 能编译，运行结果却是错的。C++98 只能为 1 个参数、2 个参数、3 个参数……各写一个重载，标准库的 `std::bind` 在 C++98 时代（Boost 版本）就是这样写了几十个重载。

C++11 的**可变参数模板**（variadic template）让一个模板接受**任意个、任意类型**的参数，而且保持完全的类型安全。

## 2. 参数包

```cpp
template <typename... Args>          // Args 是一个「模板参数包」：零个或多个类型
void f(Args... args);                // args 是一个「函数参数包」：零个或多个参数
```

- `typename... Args` 中的 `...` 表示 `Args` 不是一个类型，而是**一组类型**，叫做**模板参数包**。
- `Args... args` 表示 `args` 是**一组参数**，叫做**函数参数包**，每个参数的类型对应 `Args` 中的一个类型。
- `sizeof...(args)` 或 `sizeof...(Args)` 得到包中元素的个数，这是一个编译期常量。

调用 `f(1, 2.5, "hi")` 时，编译器推导出 `Args` 是 `int, double, const char*`，实例化出一个接受这三个参数的函数。

参数包不能直接使用，比如不能写 `args[0]`。唯一能做的事是**展开**它。

## 3. 递归处理参数包

最经典的办法是递归：每次「剥离」出第一个参数单独处理，剩下的参数组成一个更小的包，传给下一次递归，直到包为空。

```cpp
#include <iostream>
#include <string>

// 递归终点：没有参数时调用这个版本
void print()
{
    std::cout << '\n';
}

// 每次处理第一个参数，剩下的交给下一次递归
template <typename T, typename... Rest>
void print(const T& first, const Rest&... rest)
{
    std::cout << first;
    if (sizeof...(rest) > 0) std::cout << ", ";
    print(rest...);                      // 展开参数包，递归调用
}

int main()
{
    print(1, 2.5, "three", std::string("four"), '5');
    print("只有一个参数");
    print();
    return 0;
}
```

运行结果：

```
1, 2.5, three, four, 5
只有一个参数

```

`print(1, 2.5, "three")` 的执行过程：

```
print(1, 2.5, "three")   first = 1,       rest = (2.5, "three")
  print(2.5, "three")    first = 2.5,     rest = ("three")
    print("three")       first = "three", rest = ()
      print()            调用非模板的终点版本
```

注意这里的「递归」和普通函数的递归不同：每一层都是**一个不同的函数**（模板的不同实例），在编译期就全部生成好了，运行时只是普通的函数调用，编译器通常会把它们全部内联。

`rest...` 中的 `...` 是**包展开**：把包 `rest` 展开成用逗号分隔的各个元素，`print(rest...)` 就变成了 `print(2.5, "three")`。

再看一个求和的例子：

```cpp
#include <iostream>

template <typename T>
T sum(T value)                           // 终点：只剩一个参数
{
    return value;
}

template <typename T, typename... Rest>
T sum(T first, Rest... rest)
{
    return first + sum(rest...);
}

template <typename... Args>
constexpr std::size_t count(const Args&...)   // sizeof... 得到参数个数
{
    return sizeof...(Args);
}

int main()
{
    std::cout << "sum(1, 2, 3, 4, 5) = " << sum(1, 2, 3, 4, 5) << '\n';
    std::cout << "sum(1.5, 2.5) = " << sum(1.5, 2.5) << '\n';
    static_assert(count(1, 'a', "s", 2.0) == 4, "");
    std::cout << "count() = " << count() << ", count(1, 'a', \"s\") = " << count(1, 'a', "s") << '\n';
    return 0;
}
```

运行结果：

```
sum(1, 2, 3, 4, 5) = 15
sum(1.5, 2.5) = 4
count() = 0, count(1, 'a', "s") = 3
```

这里的终点是「只剩一个参数」的版本，所以 `sum()` 不带参数是无法编译的，这正符合「空的和没有意义」的语义。

## 4. 包展开的模式

`...` 不只能跟在包名后面，还可以跟在**包含包的表达式（模式）**后面，表示「对包中的每个元素，都把这个模式重复一次」：

| 写法 | 展开结果（假设 `args` 是 `a, b, c`） |
| --- | --- |
| `f(args...)` | `f(a, b, c)` |
| `f(g(args)...)` | `f(g(a), g(b), g(c))` |
| `f(args * 2 ...)` | `f(a * 2, b * 2, c * 2)` |
| `f(std::forward<Args>(args)...)` | `f(std::forward<A>(a), std::forward<B>(b), std::forward<C>(c))` |
| `{args...}` | `{a, b, c}` |

包展开只能出现在**允许逗号分隔列表的地方**：函数调用的参数、花括号初始化列表、模板参数列表、基类列表等。

### 4.1 不用递归的展开技巧

递归写法需要额外的终点函数，而且每处理一个参数就实例化一个新函数。C++11 有一个常用的技巧，利用**花括号初始化列表**来展开参数包，**按从左到右的顺序**对每个参数执行一个操作：

```cpp
#include <iostream>
#include <string>
#include <vector>

int square(int x) { return x * x; }

template <typename... Args>
std::vector<int> squares(Args... args)
{
    return {square(args)...};            // 模式 square(args) 对每个参数重复一次
}

template <typename T>
void printOne(const T& x) { std::cout << '[' << x << ']'; }

// C++11 中不用递归、依次处理每个参数的技巧：借助数组初始化
template <typename... Args>
void printAll(const Args&... args)
{
    int dummy[] = {0, (printOne(args), 0)...};   // 逗号表达式：先调用 printOne，结果取 0
    (void)dummy;
    std::cout << '\n';
}

int main()
{
    std::vector<int> v = squares(1, 2, 3, 4);
    for (int x : v) std::cout << x << ' ';
    std::cout << '\n';
    printAll(1, "two", 3.0, std::string("four"));
    printAll();
    return 0;
}
```

运行结果：

```
1 4 9 16 
[1][two][3][four]

```

拆解 `int dummy[] = {0, (printOne(args), 0)...};`：

1. 模式是 `(printOne(args), 0)`，这是一个**逗号表达式**：先执行 `printOne(args)`，整个表达式的值是 `0`。
2. 展开后变成 `{0, (printOne(a), 0), (printOne(b), 0), ...}`，用一串 0 初始化一个 `int` 数组。我们根本不关心这个数组，只是借用它来展开参数包。
3. **花括号初始化列表保证从左到右求值**，所以参数按顺序打印。（函数调用的参数则没有这个保证。）
4. 开头的 `0` 是为了处理空包：`printAll()` 时数组是 `{0}`，而不是不合法的空数组 `{}`。
5. `(void)dummy;` 避免「变量未使用」的警告。

这个技巧看起来很别扭。C++17 提供了**折叠表达式**，同样的功能可以直接写成 `(printOne(args), ...);`，会在 C++17 章节详细介绍。

## 5. 完美转发参数包

可变参数模板最重要的用途，是配合完美转发，把**任意参数原封不动地传给另一个函数**。`emplace_back`、`std::make_shared`、`std::thread` 的构造函数都是这样实现的：

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <utility>

template <typename T, typename... Args>
std::unique_ptr<T> make_unique_11(Args&&... args)        // C++11 自己实现 make_unique
{
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

struct Point3 {
    Point3(int x, int y, int z) : x(x), y(y), z(z) {}
    int x, y, z;
};

// 记录日志后调用任意函数，参数和返回值都原样传递
template <typename F, typename... Args>
auto logged(const std::string& name, F&& f, Args&&... args)
    -> decltype(std::forward<F>(f)(std::forward<Args>(args)...))
{
    std::cout << "[调用 " << name << "，" << sizeof...(args) << " 个参数]\n";
    return std::forward<F>(f)(std::forward<Args>(args)...);
}

int add3(int a, int b, int c) { return a + b + c; }

int main()
{
    auto p = make_unique_11<Point3>(1, 2, 3);
    auto s = make_unique_11<std::string>(5, '*');
    std::cout << p->x << p->y << p->z << ' ' << *s << '\n';

    int r = logged("add3", add3, 1, 2, 3);
    std::cout << "结果 " << r << '\n';
    logged("lambda", [] { std::cout << "无参 lambda\n"; });
    return 0;
}
```

运行结果：

```
123 *****
[调用 add3，3 个参数]
结果 6
[调用 lambda，0 个参数]
无参 lambda
```

`std::forward<Args>(args)...` 是这一节的核心：模式 `std::forward<Args>(args)` 中同时包含了两个包 `Args` 和 `args`，它们**同步展开**，第 i 个类型对应第 i 个参数。`Args&&...` 中的每个参数都是万能引用，所以左值和右值都能正确转发。

`logged` 的返回类型用了上一章介绍的尾置返回类型 `-> decltype(...)`：被调用函数返回什么，`logged` 就返回什么；返回 `void` 也没问题。

## 6. std::tuple

`std::tuple` 是一个用可变参数模板实现的类模板，可以保存**任意个、不同类型**的值，相当于一个「匿名结构体」。`std::pair` 只能存两个值，`tuple` 能存任意多个。

```cpp
#include <iostream>
#include <string>
#include <tuple>

// 函数返回多个值
std::tuple<int, int, double> stats(const int* data, int n)
{
    int mn = data[0], mx = data[0];
    long long total = 0;
    for (int i = 0; i < n; ++i) {
        if (data[i] < mn) mn = data[i];
        if (data[i] > mx) mx = data[i];
        total += data[i];
    }
    return std::make_tuple(mn, mx, static_cast<double>(total) / n);
}

int main()
{
    std::tuple<std::string, int, double> student("Alice", 20, 92.5);
    std::cout << std::get<0>(student) << ' ' << std::get<1>(student) << ' ' << std::get<2>(student) << '\n';
    std::get<1>(student) = 21;
    std::cout << "元素个数: " << std::tuple_size<decltype(student)>::value << '\n';

    int data[] = {4, 8, 15, 16, 23, 42};
    int mn, mx;
    double avg;
    std::tie(mn, mx, avg) = stats(data, 6);        // 解包到已有变量
    std::cout << "min=" << mn << " max=" << mx << " avg=" << avg << '\n';

    std::tie(std::ignore, mx, std::ignore) = stats(data, 3);   // 只要其中一个
    std::cout << "前三个的 max=" << mx << '\n';

    // tie 的另一个妙用：按多个字段比较
    std::tuple<int, std::string> a(1, "b"), b(1, "a");
    std::cout << std::boolalpha << "a < b: " << (a < b) << '\n';
    return 0;
}
```

运行结果：

```
Alice 20 92.5
元素个数: 3
min=4 max=42 avg=18
前三个的 max=15
a < b: false
```

- **`std::get<N>(t)`** 访问第 N 个元素，`N` 必须是编译期常量，越界会编译失败。
- **`std::make_tuple`** 自动推导元素类型。
- **`std::tie(a, b, c)`** 创建一个由引用组成的 tuple，给它赋值就能把 tuple 的各个元素**解包**到已有变量中；不需要的位置用 `std::ignore` 占位。
- tuple 之间可以按**字典序**比较：先比第一个元素，相等再比第二个……利用这一点，实现多字段排序非常方便：`return std::tie(a.age, a.name) < std::tie(b.age, b.name);`

> 返回多个值时，如果这些值有明确的含义，**定义一个结构体通常比 tuple 更清晰**：`result.min` 比 `std::get<0>(result)` 好懂得多。C++17 的结构化绑定 `auto [mn, mx, avg] = stats(...)` 让 tuple 用起来方便了很多，会在 C++17 章节介绍。

## 7. 常见错误

### 7.1 终点函数要先声明

```cpp
template <typename T, typename... Rest>
void print(const T& first, const Rest&... rest) { std::cout << first; print(rest...); }
void print() { std::cout << '\n'; }     // 写在了模板后面
```

```
error: call to function 'print' that is neither visible in the template definition nor found by argument-dependent lookup
```

模板中的 `print(rest...)` 在包为空时会调用 `print()`，而模板定义的位置看不到后面才声明的 `print()`。**终点函数必须写在可变参数模板之前。**

### 7.2 返回类型取决于第一个参数

第 3 节的 `sum` 返回类型是 `T`，也就是第一个参数的类型：

```cpp
sum(1, 2.5);   // 结果是 3，不是 3.5！
```

`T` 是 `int`，`2.5` 被转换成 `int` 时丢掉了小数部分，开启 `-Wall -Wextra` 都不会有警告。可以用 `std::common_type` 计算所有参数的公共类型作为返回类型：

```cpp
template <typename T, typename... Rest>
auto sum2(T first, Rest... rest) -> typename std::common_type<T, Rest...>::type
{
    return first + sum2(rest...);
}
// sum2(1, 2.5) = 3.5
```

### 7.3 以为函数参数中的包展开有顺序

`f(g(args)...)` 展开成 `f(g(a), g(b), g(c))`，但**函数参数的求值顺序是未指定的**，`g(c)` 可能先于 `g(a)` 执行。需要按顺序执行时，用第 4.1 节的花括号技巧。

## 8. 进阶：编译期处理类型包

参数包不仅可以在运行时处理，也可以在编译期对**类型**做计算。手段和第 3 节一样是递归，只是递归的对象换成了类模板的**特化**：

```cpp
#include <iostream>
#include <string>
#include <type_traits>

// 编译期递归：判断参数包里的类型是否全部是整数类型
template <typename... Ts>
struct AllIntegral;

template <>
struct AllIntegral<> : std::true_type {};          // 空包：true

template <typename T, typename... Rest>
struct AllIntegral<T, Rest...>
    : std::integral_constant<bool, std::is_integral<T>::value && AllIntegral<Rest...>::value> {};

// 编译期计算：所有类型的 sizeof 之和
template <typename... Ts>
struct TotalSize;

template <>
struct TotalSize<> : std::integral_constant<std::size_t, 0> {};

template <typename T, typename... Rest>
struct TotalSize<T, Rest...>
    : std::integral_constant<std::size_t, sizeof(T) + TotalSize<Rest...>::value> {};

template <typename... Args>
long long sumInts(Args... args)
{
    static_assert(AllIntegral<Args...>::value, "sumInts 只接受整数");
    long long total = 0;
    int dummy[] = {0, (total += args, 0)...};
    (void)dummy;
    return total;
}

int main()
{
    static_assert(AllIntegral<int, long, char>::value, "");
    static_assert(!AllIntegral<int, double>::value, "");
    static_assert(TotalSize<char, int, double>::value == 13, "");
    std::cout << "sumInts(1, 2L, 'a') = " << sumInts(1, 2L, 'a') << '\n';
    return 0;
}
```

运行结果：

```
sumInts(1, 2L, 'a') = 100
```

（`'a'` 的 ASCII 码是 97，1 + 2 + 97 = 100。）调用 `sumInts(1, 2.5)` 会在编译期报错：

```
error: static assertion failed due to requirement 'AllIntegral<int, double>::value': sumInts 只接受整数
```

结构是固定的套路：

1. 声明主模板 `template <typename... Ts> struct X;`
2. 为空包写一个特化，作为递归终点；
3. 为「第一个类型 + 剩余类型」写一个特化，用剩余类型递归。

`std::integral_constant<类型, 值>` 是标准库提供的「编译期常量包装」，它有一个静态成员 `value`；`std::true_type` 就是 `integral_constant<bool, true>`。标准库的 `<type_traits>` 中大量使用了这种技巧，`std::tuple` 的实现也是基于类似的递归继承。

## 9. 小结

- `template <typename... Args>` 声明模板参数包，`Args... args` 声明函数参数包，`sizeof...` 得到元素个数
- 参数包只能**展开**使用：`模式...` 对包中每个元素重复一次模式
- 处理参数包的两种方法：**递归**（需要终点函数，且要先声明）和 C++11 的**花括号初始化技巧**（保证从左到右）；C++17 的折叠表达式更简洁
- `std::forward<Args>(args)...` 完美转发任意个参数，是 `emplace_back`、`make_unique`、`std::thread` 的基础
- `std::tuple` 存储任意个不同类型的值，`std::get`、`std::tie` 访问和解包
- 类模板特化 + 递归可以在编译期对类型包做计算

## 10. 练习

1. 写一个函数模板 `maxOf(a, b, c, ...)`，返回所有参数中的最大值（假设类型相同）。
2. 写一个函数模板 `join(sep, args...)`，把任意个参数用分隔符拼接成 `std::string`，例如 `join(", ", 1, 2.5, "abc")` 返回 `"1, 2.5, abc"`。提示：用 `std::ostringstream`。
3. 写一个编译期模板 `Contains<T, Ts...>`，判断类型 `T` 是否在 `Ts...` 中出现，例如 `Contains<int, char, int>::value` 为 `true`。

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   template <typename T>
   T maxOf(T value) { return value; }

   template <typename T, typename... Rest>
   T maxOf(T first, Rest... rest)
   {
       T m = maxOf(rest...);
       return first > m ? first : m;
   }
   ```

2. 参考代码，用花括号技巧按顺序写入：

   ```cpp
   #include <sstream>
   #include <string>

   template <typename... Args>
   std::string join(const std::string& sep, const Args&... args)
   {
       std::ostringstream os;
       bool first = true;
       int dummy[] = {0, ((os << (first ? "" : sep) << args), first = false, 0)...};
       (void)dummy;
       return os.str();
   }
   ```

3. 参考代码：

   ```cpp
   template <typename T, typename... Ts>
   struct Contains : std::false_type {};

   template <typename T, typename First, typename... Rest>
   struct Contains<T, First, Rest...>
       : std::integral_constant<bool, std::is_same<T, First>::value || Contains<T, Rest...>::value> {};
   ```

</details>
