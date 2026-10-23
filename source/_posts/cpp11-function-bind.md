---
title: std::function 与 std::bind
date: 2026-10-22 11:20:00
updated: 2026-10-22 11:20:00
categories:
  - C/C++
  - C++11
tags:
  - C++11
  - 回调
order: 8
description: std::function 能保存任何可调用对象，是实现回调的标准工具；std::bind 可以绑定部分参数。本文还会讲清楚 std::function 的开销，以及为什么现代 C++ 更推荐用 lambda 代替 bind。
disableNunjucks: true
---

> **本文目标**：认识 C++ 中的各种可调用对象；会用 `std::function` 保存回调函数；会用 `std::bind` 固定参数和绑定成员函数；了解 `std::function` 的性能开销；知道什么时候该用模板参数而不是 `std::function`。
>
> **前置知识**：上一篇《Lambda 表达式》。
>
> **编译环境**：`g++ -std=c++11` / `clang++ -std=c++11`，需要包含头文件 `<functional>`。

## 1. 可调用对象

C++ 中能用 `f(参数)` 形式调用的东西很多，统称**可调用对象**（callable）：

| 种类 | 例子 |
| --- | --- |
| 普通函数 | `int add(int a, int b);` |
| 函数指针 | `int (*fp)(int, int) = add;` |
| 函数对象（仿函数） | 重载了 `operator()` 的类的对象 |
| lambda | `[](int a, int b) { return a + b; }` |
| 成员函数指针 | `&Calculator::sub`，需要配合对象调用 |

它们的类型各不相同。上一篇提到，每个 lambda 的类型都是独一无二的。那么问题来了：**怎么把不同种类的可调用对象存进同一个变量、同一个容器？** 比如一个计算器程序，想用 `map` 把运算符映射到对应的计算函数。

## 2. std::function

`std::function` 是一个类模板，模板参数是**函数签名**：

```cpp
std::function<返回类型(参数类型1, 参数类型2, ...)>
```

它可以保存**任何签名匹配的可调用对象**：

```cpp
#include <functional>
#include <iostream>
#include <map>
#include <string>

int add(int a, int b) { return a + b; }                 // 1. 普通函数

struct Multiply {                                       // 2. 函数对象
    int operator()(int a, int b) const { return a * b; }
};

class Calculator {
public:
    int sub(int a, int b) const { return a - b; }       // 3. 成员函数
};

int main()
{
    // std::function<返回类型(参数类型...)> 可以保存任何签名匹配的可调用对象
    std::map<std::string, std::function<int(int, int)>> ops;
    ops["+"] = add;
    ops["*"] = Multiply();
    ops["/"] = [](int a, int b) { return a / b; };      // 4. lambda
    Calculator calc;
    ops["-"] = [&calc](int a, int b) { return calc.sub(a, b); };

    for (const auto& op : ops)
        std::cout << "12 " << op.first << " 4 = " << op.second(12, 4) << '\n';

    std::function<void()> empty;
    std::cout << "empty 是否为空: " << !empty << '\n';
    try {
        empty();                                        // 调用空的 function 会抛异常
    } catch (const std::bad_function_call& e) {
        std::cout << "捕获异常: " << e.what() << '\n';
    }
    return 0;
}
```

运行结果：

```
12 * 4 = 48
12 + 4 = 16
12 - 4 = 8
12 / 4 = 3
empty 是否为空: 1
捕获异常: std::bad_function_call
```

四种完全不同的东西被存进了同一个 `map`，调用方式完全一样。要点：

- 默认构造的 `std::function` 是**空的**，可以用 `if (f)` 判断，调用空的 `function` 会抛出 `std::bad_function_call` 异常。
- 签名不必完全一致，只要**能调用**就行。例如参数类型可以隐式转换、返回值可以隐式转换。

## 3. 用 std::function 实现回调

`std::function` 最常见的用途是**回调**：一个模块不关心「事件发生后具体做什么」，只负责在事件发生时调用使用者注册进来的函数。

```cpp
#include <functional>
#include <iostream>
#include <string>
#include <vector>

// 一个简单的事件系统：外部注册回调，事件发生时依次调用
class Downloader {
public:
    using ProgressCallback = std::function<void(int)>;
    using DoneCallback = std::function<void(const std::string&)>;

    void onProgress(ProgressCallback cb) { progressCallbacks_.push_back(std::move(cb)); }
    void onDone(DoneCallback cb) { doneCallback_ = std::move(cb); }

    void start(const std::string& file)
    {
        for (int p = 0; p <= 100; p += 50)
            for (const auto& cb : progressCallbacks_) cb(p);
        if (doneCallback_) doneCallback_(file);     // 先判断是否设置了回调
    }

private:
    std::vector<ProgressCallback> progressCallbacks_;
    DoneCallback doneCallback_;
};

void logProgress(int p) { std::cout << "  [日志] 进度 " << p << "%\n"; }

int main()
{
    Downloader d;
    int lastPercent = -1;
    d.onProgress(logProgress);                                    // 普通函数
    d.onProgress([&lastPercent](int p) { lastPercent = p; });     // lambda，修改外部状态
    d.onDone([](const std::string& f) { std::cout << "  下载完成: " << f << '\n'; });
    d.start("lesson.pdf");
    std::cout << "lastPercent = " << lastPercent << '\n';
    return 0;
}
```

运行结果：

```
  [日志] 进度 0%
  [日志] 进度 50%
  [日志] 进度 100%
  下载完成: lesson.pdf
lastPercent = 100
```

`Downloader` 完全不知道使用者会注册什么样的函数，使用者可以传普通函数、lambda、函数对象，都没有问题。图形界面的按钮点击、网络库的数据到达通知、定时器，都是这种模式。

> 回调参数按值接收 `ProgressCallback cb`，再 `std::move` 进成员，这是「按值传递 + 移动」的惯用法：调用者传右值（比如 lambda 临时对象）时只发生移动，传左值时发生一次拷贝，都是最少的开销。

## 4. std::bind

`std::bind` 接收一个可调用对象和一些参数，返回一个**新的可调用对象**。它可以：

- **固定**部分参数的值；
- 用**占位符** `_1`、`_2`…… 表示新函数的第 1、2…… 个参数，从而**调整参数顺序**；
- **绑定成员函数**和对象。

```cpp
#include <functional>
#include <iostream>
#include <string>

using namespace std::placeholders;   // _1, _2, ...

int power(int base, int exp)
{
    int r = 1;
    for (int i = 0; i < exp; ++i) r *= base;
    return r;
}

struct Greeter {
    std::string prefix;
    void greet(const std::string& name) const { std::cout << prefix << ", " << name << "!\n"; }
};

int main()
{
    // 1. 固定部分参数
    auto square = std::bind(power, _1, 2);      // power(x, 2)
    auto powerOf2 = std::bind(power, 2, _1);    // power(2, x)
    std::cout << square(5) << ' ' << powerOf2(10) << '\n';

    // 2. 调整参数顺序
    auto reversed = std::bind(power, _2, _1);   // reversed(a, b) == power(b, a)
    std::cout << reversed(3, 2) << '\n';        // power(2, 3) = 8

    // 3. 绑定成员函数：第一个参数是对象（或对象指针）
    Greeter g{"你好"};
    auto hello = std::bind(&Greeter::greet, &g, _1);
    hello("小明");

    // 同样的功能用 lambda 写，更直观
    auto square2 = [](int x) { return power(x, 2); };
    auto hello2 = [&g](const std::string& name) { g.greet(name); };
    std::cout << square2(5) << '\n';
    hello2("小红");
    return 0;
}
```

运行结果：

```
25 1024
8
你好, 小明!
25
你好, 小红!
```

成员函数需要通过对象调用，所以绑定成员函数时，`&Greeter::greet` 后面的第一个参数必须是对象或对象的指针，它会成为成员函数里的 `this`。

### 4.1 bind 默认按值保存参数

```cpp
#include <functional>
#include <iostream>

void increase(int& n) { ++n; }

int main()
{
    int a = 0, b = 0;
    auto f1 = std::bind(increase, a);             // bind 默认按值保存参数：保存的是 a 的副本
    auto f2 = std::bind(increase, std::ref(b));   // std::ref：保存引用
    f1(); f1();
    f2(); f2();
    std::cout << "a = " << a << ", b = " << b << '\n';
    return 0;
}
```

运行结果：

```
a = 0, b = 2
```

`increase` 的参数明明是引用，`a` 却没有变化，因为 `bind` 把 `a` **复制**了一份保存起来，`increase` 修改的是那个副本。想传引用，必须用 `std::ref`（或只读引用 `std::cref`）包装。这个规则同样适用于 `std::thread` 的参数，后面讲多线程时还会遇到。

## 5. lambda 还是 bind？

`std::bind` 来自 Boost 库，在 C++11 有 lambda 之前，它是实现「部分应用」的主要手段。现在，**几乎所有情况都推荐用 lambda 代替 `std::bind`**：

| | lambda | `std::bind` |
| --- | --- | --- |
| 可读性 | 和普通函数一样直观 | 要在脑子里把 `_1`、`_2` 代入 |
| 按值 / 按引用 | 在捕获列表中写清楚 | 默认按值，引用要用 `std::ref` |
| 重载函数 | 直接调用，编译器自动选 | 需要手动 `static_cast` 指定重载版本 |
| 性能 | 编译器容易内联 | 通过函数指针调用，较难内联 |

所以 `std::bind` 主要出现在老代码中，了解它的用法能看懂即可。

## 6. 常见错误

1. **调用空的 `std::function`**：会抛出 `std::bad_function_call`。保存的回调可能没有被设置时，调用前先用 `if (cb)` 判断。
2. **以为 `std::bind` 会传引用**：见第 4.1 节，需要 `std::ref`。
3. **保存了捕获局部变量引用的 lambda**：`std::function` 只负责保存可调用对象，不会延长 lambda 引用的那些变量的生命周期。见上一篇第 6 节。
4. **滥用 `std::function`**：它有运行时开销（见下一节）。只是把一个函数传给另一个函数立即调用时，用模板参数更好。

## 7. 进阶：std::function 的开销

### 7.1 类型擦除

`std::function<int(int)>` 能保存各种不同类型的对象，这种技术叫做**类型擦除**（type erasure）：它在内部把具体类型「藏」起来，只对外暴露统一的调用接口。实现上大致相当于：

```cpp
// 极简示意，真实实现复杂得多
struct CallableBase {
    virtual int call(int) = 0;
    virtual ~CallableBase() {}
};
template <typename F>
struct CallableImpl : CallableBase {
    F f;
    int call(int x) override { return f(x); }
};
class Function {
    CallableBase* impl_;   // 指向堆上的 CallableImpl<具体类型>
public:
    int operator()(int x) { return impl_->call(x); }   // 虚函数调用
};
```

由此带来两种开销：

1. **间接调用**：每次调用都要经过一次类似虚函数的间接跳转，编译器通常无法内联。
2. **可能的堆分配**：被保存的对象可能需要放到堆上。

### 7.2 小对象优化

为了减少堆分配，标准库实现都会在 `std::function` 内部预留一小块缓冲区，对象足够小就直接放在里面，这叫**小对象优化**（small buffer optimization）。用自定义的 `operator new` 统计堆分配次数（Clang + libc++）：

```cpp
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <new>

static int allocations = 0;
void* operator new(std::size_t n)
{
    ++allocations;
    if (void* p = std::malloc(n)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }

int main()
{
    int a = 1, b = 2, c = 3, d = 4;
    double big[4] = {1, 2, 3, 4};

    allocations = 0;
    std::function<int()> f1 = [a]() { return a; };                    // 捕获 4 字节
    std::printf("捕获 4 字节:  堆分配 %d 次\n", allocations);

    allocations = 0;
    std::function<int()> f2 = [a, b, c, d]() { return a + b + c + d; }; // 捕获 16 字节
    std::printf("捕获 16 字节: 堆分配 %d 次\n", allocations);

    allocations = 0;
    std::function<int()> f3 = [big]() { return (int)big[0]; };        // 捕获 32 字节
    std::printf("捕获 32 字节: 堆分配 %d 次\n", allocations);

    return f1() + f2() + f3() > 0 ? 0 : 1;
}
```

运行结果：

```
捕获 4 字节:  堆分配 0 次
捕获 16 字节: 堆分配 0 次
捕获 32 字节: 堆分配 1 次
```

在 64 位的 libc++ 上，`sizeof(std::function<...>)` 是 32 字节，内部缓冲区放得下 16 字节的 lambda，放不下 32 字节的，只好在堆上分配。缓冲区的大小由标准库实现决定（libstdc++ 是 16 字节，MSVC 更大一些），标准没有规定。

### 7.3 用模板参数代替 std::function

如果可调用对象只是**传进来、立即调用、不保存**，用模板参数可以完全避免上述开销：

```cpp
// 使用 std::function：每次调用都是间接调用，可能有堆分配
void forEachSlow(const std::vector<int>& v, const std::function<void(int)>& f)
{
    for (int x : v) f(x);
}

// 使用模板：F 是 lambda 的真实类型，编译器能把调用内联
template <typename F>
void forEachFast(const std::vector<int>& v, F f)
{
    for (int x : v) f(x);
}
```

标准库的 `std::sort`、`std::for_each`、`std::count_if` 都用的是模板参数。**经验法则：需要把可调用对象保存起来（作为成员、放进容器），或者需要跨越编译单元（函数定义在 .cpp 文件里）时，用 `std::function`；否则用模板。**

## 8. 小结

- 普通函数、函数指针、函数对象、lambda、成员函数都是可调用对象，类型各不相同
- `std::function<R(Args...)>` 能保存任何签名匹配的可调用对象，是实现回调的标准工具；调用空的 `function` 会抛异常
- `std::bind` 可以固定参数、调整参数顺序、绑定成员函数，默认按值保存参数，传引用要用 `std::ref`
- **优先使用 lambda 代替 `std::bind`**
- `std::function` 有间接调用和可能的堆分配开销；不需要保存时，用模板参数接收可调用对象

## 9. 练习

1. 用 `std::map<char, std::function<double(double, double)>>` 实现一个支持 `+ - * /` 的计算器，读入 `3.5 * 2` 这样的表达式并输出结果，除数为 0 时输出错误信息。
2. 把下面的 `bind` 改写成 lambda：

   ```cpp
   auto f = std::bind(&Greeter::greet, &g, "世界");
   auto h = std::bind(std::less<int>(), _2, _1);
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   std::map<char, std::function<double(double, double)>> ops{
       {'+', [](double a, double b) { return a + b; }},
       {'-', [](double a, double b) { return a - b; }},
       {'*', [](double a, double b) { return a * b; }},
       {'/', [](double a, double b) { return a / b; }},
   };
   double a, b;
   char op;
   while (std::cin >> a >> op >> b) {
       auto it = ops.find(op);
       if (it == ops.end()) std::cout << "不支持的运算符\n";
       else if (op == '/' && b == 0) std::cout << "除数不能为 0\n";
       else std::cout << it->second(a, b) << '\n';
   }
   ```

2. `auto f = [&g]() { g.greet("世界"); };` 和 `auto h = [](int a, int b) { return b < a; };`。

</details>
