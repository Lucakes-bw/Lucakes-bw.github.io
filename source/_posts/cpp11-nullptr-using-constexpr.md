---
title: nullptr、using 别名与 constexpr 入门
date: 2026-10-22 15:00:00
updated: 2026-10-22 15:00:00
categories:
  - C/C++
  - C++11
tags:
  - C++11
  - constexpr
order: 3
description: 三个让代码更安全、更清晰的小特性：用 nullptr 代替 NULL，用 using 代替 typedef 并支持别名模板，用 constexpr 把计算提前到编译期。
disableNunjucks: true
---

> **本文目标**：知道为什么要用 `nullptr` 代替 `NULL`；会用 `using` 定义类型别名和别名模板；会写 `constexpr` 变量和函数，分清 `const` 与 `constexpr`。
>
> **编译环境**：`g++ -std=c++11` / `clang++ -std=c++11`。

## 1. nullptr：真正的空指针

### 1.1 NULL 有什么问题

在 C 语言里，`NULL` 通常定义为 `((void*)0)`。C++ 不允许 `void*` 隐式转换成其他指针类型，所以 C++ 里的 `NULL` 一般定义为 `0` 或编译器内置的 `__null`。也就是说，**`NULL` 本质上是一个整数**。

这在函数重载时就出问题了：

```cpp
void show(int n);
void show(char* p);

show(NULL);   // 想调用 show(char*)，结果呢？
```

Clang 的编译结果：

```
error: call to 'show' is ambiguous
note: candidate function
note: candidate function
```

编译器不知道 `NULL` 应该当整数还是当指针。在 `NULL` 定义为 `0` 的编译器上，情况更糟：代码能编译，但会**悄悄调用 `show(int)`**，和你的意图完全相反。

### 1.2 nullptr

C++11 引入了关键字 `nullptr`，它的类型是 `std::nullptr_t`，**只能转换成指针类型，不能转换成整数**：

```cpp
#include <cstddef>
#include <iostream>

void show(int n)   { std::cout << "show(int): " << n << '\n'; }
void show(char* p) { std::cout << "show(char*): " << (p ? "非空" : "空指针") << '\n'; }

int main()
{
    show(0);          // 0 是 int，调用 show(int)
    // show(NULL);    // 编译错误或调用 show(int)，取决于编译器
    show(nullptr);    // nullptr 只能转换成指针，调用 show(char*)

    int* p = nullptr;
    if (p == nullptr) std::cout << "p 是空指针\n";

    std::nullptr_t np = nullptr;   // nullptr 的类型
    char* q = np;
    show(q);
    return 0;
}
```

运行结果：

```
show(int): 0
show(char*): 空指针
p 是空指针
show(char*): 空指针
```

**结论：在 C++11 及以后的代码里，空指针一律写 `nullptr`，不要再用 `NULL` 或 `0`。**

## 2. using：更好用的类型别名

### 2.1 代替 typedef

`using 新名字 = 类型;` 和 `typedef 类型 新名字;` 的作用完全一样，但读起来更自然，尤其是函数指针：

```cpp
typedef void (*HandlerOld)(int);   // 名字 HandlerOld 夹在中间，不好找
using Handler = void (*)(int);     // 名字在左边，类型在右边，一目了然
```

### 2.2 别名模板：typedef 做不到的事

`using` 真正的优势是可以带模板参数，这叫**别名模板**（alias template）：

```cpp
#include <iostream>
#include <map>
#include <string>
#include <vector>

// 1. 普通类型别名
typedef unsigned long long u64_old;
using u64 = unsigned long long;

// 2. 函数指针：using 的写法更直观
typedef void (*HandlerOld)(int);
using Handler = void (*)(int);

// 3. 别名模板：typedef 做不到
template <typename T>
using Dict = std::map<std::string, T>;

template <typename T>
using Matrix = std::vector<std::vector<T>>;

void onEvent(int code) { std::cout << "收到事件 " << code << '\n'; }

int main()
{
    u64 big = 1ULL << 40;
    std::cout << big << '\n';

    Handler h = onEvent;
    h(42);

    Dict<int> ages{{"Alice", 20}, {"Bob", 22}};
    Dict<std::vector<int>> scores{{"Alice", {90, 95}}};
    std::cout << "Bob " << ages["Bob"] << " 岁, Alice 有 " << scores["Alice"].size() << " 个成绩\n";

    Matrix<double> m(2, std::vector<double>(3, 0.5));
    std::cout << "矩阵 " << m.size() << " x " << m[0].size() << '\n';
    return 0;
}
```

运行结果：

```
1099511627776
收到事件 42
Bob 22 岁, Alice 有 2 个成绩
矩阵 2 x 3
```

在 C++98 里想实现 `Dict<T>`，只能用一个结构体包一层：

```cpp
template <typename T>
struct DictOld {
    typedef std::map<std::string, T> type;
};
DictOld<int>::type ages;              // 每次都要写 ::type
```

在模板内部使用时还得加 `typename`：`typename DictOld<T>::type`，非常啰嗦。标准库在 C++14 中给所有 `std::xxx<T>::type` 都补了一个 `std::xxx_t<T>` 别名，比如 `std::remove_const_t<T>`，用的就是别名模板。

## 3. constexpr：编译期计算

### 3.1 const 和 constexpr 的区别

```cpp
int main()
{
    int n = 5;
    const int a = n;       // OK：const 只表示“不能修改”，可以运行期初始化
    constexpr int b = n;   // 错误：constexpr 要求编译期就能算出值
    return a + b;
}
```

Clang 的报错：

```
error: constexpr variable 'b' must be initialized by a constant expression
note: read of non-const variable 'n' is not allowed in a constant expression
```

| | `const` | `constexpr` |
| --- | --- | --- |
| 含义 | 只读，初始化后不能修改 | **编译期常量**，值在编译时就确定 |
| 初始值 | 运行期的值也可以 | 必须是编译期能算出的常量表达式 |
| 能否做数组长度、模板参数 | 只有用常量初始化时才行 | 一定可以 |

`constexpr` 变量一定是 `const` 的，反过来不成立。

### 3.2 constexpr 函数

`constexpr` 还能修饰函数，表示**这个函数可以在编译期计算**：

```cpp
#include <array>
#include <iostream>

constexpr int square(int x) { return x * x; }

// C++11 的 constexpr 函数只能有一条 return 语句，可以用递归和三目运算符
constexpr long long factorial(int n) { return n <= 1 ? 1 : n * factorial(n - 1); }

struct Point {
    constexpr Point(int x, int y) : x_(x), y_(y) {}
    constexpr int x() const { return x_; }
    constexpr int y() const { return y_; }
private:
    int x_, y_;
};

int main()
{
    constexpr int size = square(4);          // 编译期算出 16
    int buffer[size];                        // 可以当数组长度
    std::array<int, square(3)> arr{};        // 可以当模板参数
    static_assert(factorial(5) == 120, "5! 应该等于 120");

    constexpr Point p(3, 4);
    static_assert(p.x() + p.y() == 7, "");

    int n = 0;
    std::cin >> n;
    std::cout << "运行期调用: square(" << n << ") = " << square(n) << '\n';   // 同一个函数也能在运行期用

    std::cout << "sizeof(buffer) = " << sizeof(buffer) << ", arr.size() = " << arr.size()
              << ", 10! = " << factorial(10) << '\n';
    return 0;
}
```

输入 `7`，运行结果：

```
运行期调用: square(7) = 49
sizeof(buffer) = 64, arr.size() = 9, 10! = 3628800
```

要点：

1. **参数是编译期常量时，结果也可以是编译期常量**，可以用在数组长度、模板参数、`static_assert` 等必须是常量的地方。
2. **参数是运行期的值时，它就是一个普通函数**。同一个 `square` 在两种场合都能用，不需要写两份。
3. `static_assert(条件, "消息")` 在编译期检查条件，不满足就编译失败。`factorial(5) == 120` 在编译时就验证了，程序运行时没有任何开销。
4. 构造函数也可以是 `constexpr`，这样就能在编译期创建对象，比如上面的 `constexpr Point p(3, 4);`。

### 3.3 C++11 中 constexpr 函数的限制

C++11 的 `constexpr` 函数非常严格，函数体里**只能有一条 `return` 语句**（外加 `static_assert`、`using`、`typedef` 等不产生代码的语句）。不能有局部变量、不能有 `if`、不能有循环，只能用三目运算符和递归来实现逻辑，就像上面的 `factorial`。

这个限制在 C++14 中大幅放宽：可以使用局部变量、`if`、`for`、`while`，写起来和普通函数几乎一样。我们在 C++14 章节再详细介绍。

## 4. 常见错误

1. **以为 `constexpr` 函数一定在编译期执行**。只有在**必须**得到常量的地方（数组长度、模板参数、`constexpr` 变量的初始值、`static_assert`），编译器才保证在编译期计算。像 `std::cout << square(4);` 这样的调用，编译器可以在编译期算，也可以在运行期算。想强制编译期计算，先赋值给 `constexpr` 变量。
2. **`constexpr` 指针的含义**：`constexpr int* p = nullptr;` 中 `constexpr` 修饰的是指针本身（相当于 `int* const p`），不是指向的对象。
3. **把 `using` 和 `using namespace` 搞混**。`using X = Y;` 是类型别名；`using namespace std;` 是引入命名空间，不要在头文件里写后者。

## 5. 进阶

### 5.1 nullptr 让模板更可靠

在模板中，`0` 和 `NULL` 会被推导成整数类型，从而失去「空指针」的含义：

```cpp
template <typename F, typename P>
void call(F f, P p) { f(p); }

void take(int* p);

call(take, 0);         // P 推导为 int，int 不能转成 int*，编译失败
call(take, nullptr);   // P 推导为 std::nullptr_t，能转成 int*，编译通过
```

### 5.2 字面类型

能用在 `constexpr` 里的类型叫**字面类型**（literal type）：基本类型、引用、指针，以及满足条件的类，至少有一个 `constexpr` 构造函数（不能是拷贝或移动构造）、析构函数是平凡的、所有成员都是字面类型。上面的 `Point` 就是字面类型，而 `std::string`、`std::vector` 在 C++20 之前都不是，因为它们需要动态分配内存。

### 5.3 编译期计算的价值

把计算搬到编译期有两个好处：**运行时零开销**，以及**错误提前暴露**。例如可以在编译期生成查找表、校验配置参数、计算哈希值：

```cpp
constexpr unsigned hash(const char* s, unsigned h = 5381)
{
    return *s ? hash(s + 1, h * 33 + static_cast<unsigned char>(*s)) : h;
}

// 编译期算出哈希值，可以用在 switch 的 case 标签中
switch (hash(cmd)) {
    case hash("start"): /* ... */ break;
    case hash("stop"):  /* ... */ break;
}
```

`case` 标签必须是编译期常量，普通函数做不到，`constexpr` 函数可以。

## 6. 小结

| 特性 | 用途 | 一句话 |
| --- | --- | --- |
| `nullptr` | 空指针 | 永远用 `nullptr`，不用 `NULL` 和 `0` |
| `using X = T;` | 类型别名 | 比 `typedef` 好读，支持模板 |
| `template<...> using` | 别名模板 | 给模板起别名，`typedef` 做不到 |
| `constexpr` 变量 | 编译期常量 | 值必须在编译期确定 |
| `constexpr` 函数 | 编译期计算 | 参数是常量时在编译期算，否则就是普通函数 |

## 7. 练习

1. 下面哪些语句能编译通过？

   ```cpp
   int a = 10;
   const int b = 20;
   const int c = a;
   constexpr int d = b * 2;
   constexpr int e = c;
   int arr[b];
   ```

2. 用 C++11 的 `constexpr` 写一个计算斐波那契数的函数 `fib(n)`，并用 `static_assert` 验证 `fib(10) == 55`。

<details>
<summary>点击查看答案</summary>

1. 只有 `constexpr int e = c;` 不能通过：`c` 虽然是 `const`，但它的初始值 `a` 是运行期的变量，所以 `c` 不是常量表达式。`b` 用字面量初始化，是常量表达式，所以 `d` 和 `int arr[b];` 都可以。
2. C++11 只能写一条 `return`，用递归和三目运算符：

   ```cpp
   constexpr int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
   static_assert(fib(10) == 55, "fib(10) 应为 55");
   ```

</details>
