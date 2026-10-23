---
title: 统一初始化与 std::initializer_list
date: 2026-10-22 09:20:00
updated: 2026-10-22 09:20:00
categories:
  - C/C++
  - C++11
tags:
  - C++11
  - 初始化
order: 2
description: C++11 用花括号统一了各种对象的初始化写法，还能防止窄化转换、避开「最令人头疼的解析」。本文也讲清楚 initializer_list 构造函数的优先级陷阱。
disableNunjucks: true
---

> **本文目标**：会用花括号 `{}` 初始化任何对象；理解花括号如何防止窄化转换；分清 `vector<int> v(3, 5)` 和 `vector<int> v{3, 5}`；会给自己的类写 `initializer_list` 构造函数；会用类成员默认值。
>
> **编译环境**：`g++ -std=c++11` / `clang++ -std=c++11`。

## 1. C++98 的初始化有多乱

在 C++98 里，不同的东西要用不同的方式初始化：

```cpp
int a = 10;                  // 赋值风格
int b(10);                   // 构造函数风格
int arr[3] = {1, 2, 3};      // 数组用花括号
struct Point { int x, y; };
Point p = {1, 2};            // 结构体用花括号
std::vector<int> v;          // 容器没法直接初始化，只能一个个 push_back
v.push_back(1);
v.push_back(2);
```

更麻烦的是，有些写法根本不能用：`new` 出来的数组无法初始化，类的成员数组没法在构造函数里初始化，容器也不能像数组那样直接列出元素。

C++11 引入了**统一初始化**（uniform initialization），也叫**列表初始化**（list initialization）：**任何对象都可以用花括号 `{}` 初始化**。

## 2. 花括号可以初始化一切

```cpp
#include <iostream>
#include <map>
#include <string>
#include <vector>

struct Point {          // 聚合类型：没有构造函数、全是 public 成员
    int x;
    int y;
};

class Student {
public:
    Student(std::string name, int age) : name_(name), age_(age) {}
    void print() const { std::cout << name_ << ", " << age_ << " 岁\n"; }
private:
    std::string name_;
    int age_;
};

Point makeOrigin()
{
    return {0, 0};      // 返回值也能用花括号
}

int main()
{
    int a{10};                              // 基本类型
    int arr[]{1, 2, 3};                     // 数组
    Point p{3, 4};                          // 聚合
    Student s{"小明", 18};                   // 调用构造函数
    std::vector<int> v{1, 2, 3, 4};         // 容器
    std::map<std::string, int> m{{"one", 1}, {"two", 2}};
    int* heap = new int[3]{7, 8, 9};        // new 出来的数组

    std::cout << a << ' ' << arr[2] << ' ' << p.x << ',' << p.y << '\n';
    s.print();
    std::cout << "v.size() = " << v.size() << ", m[\"two\"] = " << m["two"] << '\n';
    std::cout << heap[0] << heap[1] << heap[2] << '\n';
    Point o = makeOrigin();
    std::cout << "origin = (" << o.x << ", " << o.y << ")\n";

    int zero{};                             // 空花括号：值初始化为 0
    std::cout << "zero = " << zero << '\n';
    delete[] heap;
    return 0;
}
```

运行结果：

```
10 3 3,4
小明, 18 岁
v.size() = 4, m["two"] = 2
789
origin = (0, 0)
zero = 0
```

几个值得注意的地方：

- `int zero{};`：**空花括号会把基本类型初始化为 0**，指针初始化为 `nullptr`。而 `int x;` 在函数内部是**未初始化**的，值是随机的。养成用 `{}` 的习惯可以避免这类 bug。
- `return {0, 0};`：返回值的类型已知，可以省略类型名。
- `std::map` 的每个元素是一个 `pair`，所以写成 `{{"one", 1}, {"two", 2}}`，外层是整个 map，内层是每个 pair。
- 等号可写可不写：`int a{10};` 和 `int a = {10};` 基本等价。

## 3. 花括号的两大好处

### 3.1 禁止窄化转换

**窄化转换**（narrowing conversion）是指可能丢失数据的隐式转换，比如 `double` 转 `int` 会丢掉小数部分，`int` 转 `char` 可能溢出。

```cpp
int main()
{
    double pi = 3.14;
    int a = pi;      // 允许，悄悄截断成 3
    int b{pi};       // 错误：花括号禁止窄化转换
    char c{300};     // 错误：300 超出 char 范围
    return a + b + c;
}
```

Clang 的编译结果：

```
error: type 'double' cannot be narrowed to 'int' in initializer list [-Wc++11-narrowing]
error: constant expression evaluates to 300 which cannot be narrowed to type 'char' [-Wc++11-narrowing]
```

用 `=` 或 `()` 初始化时，数据丢了编译器也不会报错；用 `{}` 则直接编译失败，把 bug 消灭在编译期。

> 如果确实需要截断，用 `static_cast` 明确表达意图：`int b{static_cast<int>(pi)};`

### 3.2 避开「最令人头疼的解析」

```cpp
#include <iostream>

struct Widget {
    Widget() { std::cout << "Widget 构造\n"; }
};

int main()
{
    Widget w1();     // 本意是创建对象，实际上声明了一个函数！
    Widget w2{};     // 一定是创建对象
    return 0;
}
```

编译器的警告：

```
warning: empty parentheses interpreted as a function declaration [-Wvexing-parse]
note: remove parentheses to declare a variable
```

运行结果只有一行：

```
Widget 构造
```

`Widget w1();` 在 C++ 语法里可以解读为「声明一个名为 `w1`、没有参数、返回 `Widget` 的函数」。C++ 规定**只要能解读为声明，就按声明处理**，所以 `w1` 根本不是对象。这个问题被称为 *most vexing parse*（最令人头疼的解析）。花括号不会有这种歧义，`Widget w2{};` 一定创建对象。

## 4. std::initializer_list

### 4.1 让函数接受任意个参数

`std::vector<int> v{1, 2, 3, 4};` 能写任意多个元素，靠的是 `std::initializer_list<T>`。编译器看到花括号列表时，会在背后创建一个数组，再把它包装成 `initializer_list` 传给函数。我们自己的函数也能用：

```cpp
#include <initializer_list>

int sum(std::initializer_list<int> nums)
{
    int total = 0;
    for (int n : nums) total += n;
    return total;
}

sum({1, 2, 3, 4, 5});   // 15
```

`initializer_list` 提供了 `size()`、`begin()`、`end()`，可以用范围 for 遍历。它的元素是**只读的**（`const T`）。

### 4.2 优先级陷阱：v(3, 5) 和 v{3, 5}

**如果一个类有 `initializer_list` 构造函数，花括号初始化会优先匹配它**，哪怕别的构造函数更合适：

```cpp
#include <initializer_list>
#include <iostream>
#include <vector>

class IntBag {
public:
    IntBag(int count, int value)                       // ①
    {
        std::cout << "调用 (int, int) 构造: " << count << " 个 " << value << '\n';
    }
    IntBag(std::initializer_list<int> list)            // ②
    {
        std::cout << "调用 initializer_list 构造, 元素个数 = " << list.size() << ": ";
        for (const int* p = list.begin(); p != list.end(); ++p)
            std::cout << *p << ' ';
        std::cout << '\n';
    }
};

int sum(std::initializer_list<int> nums)
{
    int total = 0;
    for (int n : nums) total += n;
    return total;
}

int main()
{
    IntBag a(3, 5);       // 圆括号 -> ①
    IntBag b{3, 5};       // 花括号 -> 优先 ②

    std::vector<int> v1(3, 5);   // 3 个 5
    std::vector<int> v2{3, 5};   // 两个元素：3 和 5
    std::cout << "v1.size() = " << v1.size() << ", v2.size() = " << v2.size() << '\n';

    std::cout << "sum = " << sum({1, 2, 3, 4, 5}) << '\n';
    return 0;
}
```

运行结果：

```
调用 (int, int) 构造: 3 个 5
调用 initializer_list 构造, 元素个数 = 2: 3 5 
v1.size() = 3, v2.size() = 2
sum = 15
```

这就是 `std::vector` 最经典的坑：

| 写法 | 结果 |
| --- | --- |
| `std::vector<int> v(3, 5);` | `[5, 5, 5]`，3 个元素，每个都是 5 |
| `std::vector<int> v{3, 5};` | `[3, 5]`，2 个元素 |
| `std::vector<int> v(3);` | `[0, 0, 0]` |
| `std::vector<int> v{3};` | `[3]` |

**经验法则**：初始化容器时，想「列出元素」就用 `{}`，想「指定数量」就用 `()`。

## 5. 类成员默认值

C++11 允许在声明成员变量时直接给出默认值（non-static data member initializer，简称 NSDMI）：

```cpp
#include <iostream>
#include <string>

class Config {
public:
    Config() = default;                       // 使用成员默认值
    explicit Config(int port) : port_(port) {} // 初始化列表会覆盖默认值
    void print() const
    {
        std::cout << host_ << ':' << port_ << " timeout=" << timeout_ << "s\n";
    }
private:
    std::string host_{"127.0.0.1"};   // C++11：成员可以就地初始化
    int port_ = 8080;
    double timeout_{3.0};
};

int main()
{
    Config a;
    Config b{9000};
    a.print();
    b.print();
    return 0;
}
```

运行结果：

```
127.0.0.1:8080 timeout=3s
127.0.0.1:9000 timeout=3s
```

规则很简单：**构造函数的初始化列表里写了某个成员，就用初始化列表的值；没写，就用默认值**。有多个构造函数时，不再需要在每个构造函数里重复初始化同样的成员，也不会忘记初始化。

> `= default` 表示让编译器生成默认构造函数，`explicit` 禁止 `Config c = 9000;` 这样的隐式转换。这两个关键字在《类的增强》一文中详细介绍。

## 6. 常见错误

1. **对有 `initializer_list` 构造函数的类误用花括号**：`std::vector<int> v{10};` 是只有一个元素 10 的 vector，不是 10 个元素。`std::string s{65, 'a'};` 得到的是 `"Aa"`（65 被当作字符 `'A'`），而不是 65 个 `'a'`。
2. **和 `auto` 一起用**：`auto x = {1};` 推导出的是 `initializer_list<int>`，见上一篇《auto 与 decltype》。
3. **以为花括号能防止所有转换**：只禁止**窄化**转换。`double d{10};` 是合法的，`int` 转 `double` 不会丢数据。

## 7. 进阶

### 7.1 initializer_list 只是一个「视图」

`std::initializer_list` 内部只保存了两样东西：指向底层数组的指针和长度。复制 `initializer_list` 不会复制元素，它们共享同一个底层数组。这带来两个后果：

- **元素是 const 的，无法移动**。`std::vector<std::unique_ptr<int>> v{std::unique_ptr<int>(new int(1))};` 编译失败，报错 `call to implicitly-deleted copy constructor of 'std::unique_ptr<int>'`。`unique_ptr` 只能移动不能拷贝，而 `initializer_list` 的元素只能被拷贝出来。
- **不要保存 `initializer_list`**。底层数组的生命周期和临时对象一样，语句结束后就销毁，保存下来的 `initializer_list` 会指向已经释放的内存。

```cpp
std::initializer_list<int> bad()
{
    return {1, 2, 3};   // 返回后底层数组已经销毁，悬空！
}
```

### 7.2 重载决议的完整规则

使用花括号初始化类对象时，编译器分两步选择构造函数：

1. 如果类有 `initializer_list` 构造函数，并且花括号里的元素**能转换成**列表的元素类型，就选它。即使需要窄化转换，也会选它，然后因为窄化而报错。
2. 否则，按普通规则在所有构造函数中选择。

**空花括号是例外**：`Widget w{};` 调用的是默认构造函数，而不是用空列表调用 `initializer_list` 构造函数。如果真想传一个空列表，要写 `Widget w({});`。注意不要写成 `Widget w{{}};`：对 `initializer_list<int>` 来说，内层的 `{}` 会被当成一个值初始化为 0 的元素，传进去的是只有一个元素 `0` 的列表。

### 7.3 聚合初始化

`Point p{3, 4};` 并不是调用构造函数，`Point` 根本没有构造函数。这种初始化叫**聚合初始化**：按成员声明的顺序，依次用花括号里的值初始化每个成员，没给出的成员初始化为 0。在 C++11 中，聚合类型需要满足：没有用户声明的构造函数、没有私有或保护的非静态成员、没有基类、没有虚函数。

## 8. 小结

- 花括号 `{}` 可以初始化任何对象：基本类型、数组、结构体、类、容器、`new` 的数组、返回值
- 花括号**禁止窄化转换**，能避开 most vexing parse，`{}` 还能把基本类型初始化为 0
- 有 `initializer_list` 构造函数时，花括号**优先**匹配它：`vector<int> v(3, 5)` 和 `v{3, 5}` 完全不同
- 类成员可以在声明时给默认值，构造函数初始化列表会覆盖它

## 9. 练习

1. 下面每个 `vector` 有几个元素？分别是什么？

   ```cpp
   std::vector<int> a(5);
   std::vector<int> b{5};
   std::vector<int> c(5, 1);
   std::vector<int> d{5, 1};
   std::vector<std::string> e{3, "hi"};
   ```

2. 给下面的 `Matrix` 类加一个构造函数，使 `Matrix m{{1, 2}, {3, 4}};` 可以创建一个 2×2 矩阵。

   ```cpp
   class Matrix {
       std::vector<std::vector<int>> data_;
   };
   ```

<details>
<summary>点击查看答案</summary>

1. `a`：5 个 0；`b`：1 个元素 5；`c`：5 个 1；`d`：2 个元素 5 和 1；`e`：3 个 `"hi"`。`e` 的元素类型是 `string`，`3` 无法转换成 `string`，`initializer_list<string>` 构造函数不可行，于是退回到 `(size_type, const string&)` 构造函数。
2. 参数类型是「列表的列表」：

   ```cpp
   Matrix(std::initializer_list<std::initializer_list<int>> rows)
   {
       for (const auto& row : rows)
           data_.emplace_back(row);   // vector 可以由 initializer_list 构造
   }
   ```

</details>
