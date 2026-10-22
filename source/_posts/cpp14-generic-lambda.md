---
title: 泛型 lambda 与初始化捕获
date: 2026-10-24 13:00:00
updated: 2026-10-24 13:00:00
categories:
  - C/C++
  - C++14
tags:
  - C++14
  - Lambda
order: 1
description: C++14 让 lambda 的参数可以写 auto，一个 lambda 就能处理多种类型；初始化捕获可以在捕获列表里定义新变量，从而把 unique_ptr 等只能移动的对象移进 lambda，也能安全地复制成员变量。
disableNunjucks: true
---

> **本文目标**：会写参数为 `auto` 的泛型 lambda；理解泛型 lambda 背后的模板 `operator()`；会用初始化捕获实现移动捕获和成员变量的复制捕获；知道移动捕获的 lambda 不能放进 `std::function`。
>
> **前置知识**：C++11 章节的《Lambda 表达式》《万能引用与完美转发》。
>
> **编译环境**：`g++ -std=c++14` / `clang++ -std=c++14`，GCC 5+ / Clang 3.4+ / VS2015+ 支持。

## 1. C++14 概述

C++14 是一个「小版本」，主要是对 C++11 的完善和补充：C++11 中很多用起来别扭的地方，C++14 都让它们变得更自然了。本章 4 篇文章覆盖 C++14 的主要内容：

| 文章 | 内容 |
| --- | --- |
| 本篇 | 泛型 lambda、初始化捕获 |
| 返回类型推导与 decltype(auto) | 函数返回类型可以写 `auto`；`decltype(auto)` |
| 放宽的 constexpr 与变量模板 | `constexpr` 函数可以有循环和局部变量；变量模板；二进制字面量和数字分隔符 |
| 标准库的补充 | `make_unique`、`std::exchange`、`integer_sequence`、读写锁、标准字面量等 |

## 2. 泛型 lambda

C++11 的 lambda 参数必须写出具体类型。如果想让同一个 lambda 同时处理 `int` 和 `double`，就做不到。C++14 允许 lambda 的参数类型写 **`auto`**，这样的 lambda 叫做**泛型 lambda**（generic lambda）：

```cpp
#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <vector>

int main()
{
    // 1. 参数写 auto，同一个 lambda 可以接受不同类型
    auto print = [](const auto& x) { std::cout << x << ' '; };
    print(42);
    print(3.14);
    print("hello");
    print(std::string("world"));
    std::cout << '\n';

    // 2. 多个 auto 参数，类型可以各不相同
    auto add = [](auto a, auto b) { return a + b; };
    std::cout << add(1, 2) << ' ' << add(1.5, 2) << ' ' << add(std::string("ab"), "cd") << '\n';

    // 3. 一个比较器，适用于任何有 size() 的容器
    auto bySize = [](const auto& a, const auto& b) { return a.size() < b.size(); };
    std::vector<std::string> words{"banana", "fig", "apple"};
    std::vector<std::vector<int>> groups{{1, 2, 3}, {4}, {5, 6}};
    std::sort(words.begin(), words.end(), bySize);
    std::sort(groups.begin(), groups.end(), bySize);
    for (const auto& w : words) print(w);
    std::cout << "| 第一组大小 " << groups[0].size() << '\n';

    // 4. 遍历 map 时不用写出 pair 的完整类型
    std::map<std::string, std::vector<int>> scores{{"Alice", {90, 85}}, {"Bob", {70}}};
    std::for_each(scores.begin(), scores.end(), [](const auto& kv) {
        std::cout << kv.first << " 有 " << kv.second.size() << " 个成绩\n";
    });
    return 0;
}
```

运行结果：

```
42 3.14 hello world 
3 3.5 abcd
fig apple banana | 第一组大小 1
Alice 有 2 个成绩
Bob 有 1 个成绩
```

- `print` 一个 lambda 就能打印四种不同类型的值。
- `bySize` 同时用于给 `vector<string>` 和 `vector<vector<int>>` 排序，只要元素有 `size()` 就行。
- 第 4 个例子中，用 C++11 写参数类型得写成 `const std::pair<const std::string, std::vector<int>>&`，很长而且容易漏掉 `const`（漏掉 `const` 会导致每次都拷贝一个 pair）。`const auto&` 简单又不会出错。

用 C++11 编译会报错：

```
error: 'auto' not allowed in lambda parameter before C++14
```

### 2.1 泛型 lambda 的本质

上一章讲过，lambda 是编译器生成的闭包类。泛型 lambda 的区别只在于：它的 `operator()` 是一个**成员函数模板**。

```cpp
#include <iostream>
#include <type_traits>
#include <utility>

// 泛型 lambda 生成的闭包类，大致相当于：
struct __GenericLambda {
    template <typename T>
    auto operator()(const T& x) const { return x * 2; }
};

int main()
{
    auto twice = [](const auto& x) { return x * 2; };
    __GenericLambda same;
    std::cout << twice(21) << ' ' << twice(1.25) << ' ' << same(21) << ' ' << same(1.25) << '\n';
    static_assert(std::is_same<decltype(twice(1)), int>::value, "");
    static_assert(std::is_same<decltype(twice(1.0)), double>::value, "");

    // 泛型 lambda 也可以完美转发
    auto call = [](auto&& f, auto&&... args) {
        return std::forward<decltype(f)>(f)(std::forward<decltype(args)>(args)...);
    };
    std::cout << call([](int a, int b) { return a * b; }, 6, 7) << '\n';

    // 递归：把自己作为参数传进去
    auto fib = [](auto self, int n) -> long long {
        return n < 2 ? n : self(self, n - 1) + self(self, n - 2);
    };
    std::cout << "fib(30) = " << fib(fib, 30) << '\n';
    return 0;
}
```

运行结果：

```
42 2.5 42 2.5
42
fib(30) = 832040
```

几个要点：

1. **每个 `auto` 参数对应一个模板参数**。`twice(1)` 和 `twice(1.0)` 实例化出两个不同的 `operator()`，返回类型分别是 `int` 和 `double`。
2. **`auto&&` 是万能引用**，所以泛型 lambda 也能完美转发。lambda 没有模板参数名，转发时用 `decltype(f)` 代替 `T`：`std::forward<decltype(f)>(f)`。`auto&&...` 还能接受任意个参数。
3. **递归 lambda**：C++11 中递归 lambda 需要借助 `std::function`，有额外开销。泛型 lambda 可以把自己作为参数传进去，`self(self, n - 1)` 就是递归调用，没有 `std::function` 的开销。C++23 提供了更直接的写法 `[](this auto self, int n)`。

## 3. 初始化捕获

C++11 的捕获列表只能捕获**已有的变量**。C++14 允许在捕获列表中**定义一个新变量并初始化它**，叫做**初始化捕获**（init capture），也叫**广义 lambda 捕获**：

```
[名字 = 表达式]     // 按值：用表达式的值初始化一个新成员
[&名字 = 变量]      // 按引用：新名字绑定到变量
```

```cpp
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

int main()
{
    // 1. 初始化捕获：在捕获列表里定义一个新变量
    int base = 10;
    auto f = [offset = base * 2](int x) { return x + offset; };   // offset = 20
    std::cout << "f(1) = " << f(1) << '\n';

    // 2. 移动捕获：把只能移动的对象“搬进” lambda
    auto ptr = std::make_unique<std::string>("只能移动的资源");
    auto g = [p = std::move(ptr)]() { std::cout << *p << '\n'; };
    g();
    std::cout << "ptr 已被移走: " << (ptr == nullptr) << '\n';

    // 3. 移动捕获大对象，避免拷贝
    std::vector<int> big(1000000, 7);
    auto sum = [v = std::move(big)]() {
        long long s = 0;
        for (int x : v) s += x;
        return s;
    };
    std::cout << "sum = " << sum() << ", big.size() = " << big.size() << '\n';

    // 4. 捕获引用并改名
    int counter = 0;
    auto inc = [&c = counter]() { ++c; };
    inc(); inc();
    std::cout << "counter = " << counter << '\n';

    // 5. mutable + 初始化捕获：lambda 自带状态
    auto nextId = [id = 0]() mutable { return ++id; };
    int a = nextId();
    int b = nextId();
    int c = nextId();
    std::cout << "id: " << a << ' ' << b << ' ' << c << '\n';
    return 0;
}
```

运行结果：

```
f(1) = 21
只能移动的资源
ptr 已被移走: 1
sum = 7000000, big.size() = 0
counter = 2
id: 1 2 3
```

逐个说明：

1. **任意表达式**：`offset = base * 2` 把计算结果保存为 lambda 的成员，`offset` 的类型由初始值推导（规则和 `auto` 一样）。
2. **移动捕获**：这是初始化捕获最重要的用途。C++11 的按值捕获只能**拷贝**，而 `unique_ptr` 不能拷贝，所以无法捕获。`[p = std::move(ptr)]` 用 `ptr` **移动构造**了成员 `p`，资源的所有权转移到了 lambda 中。
3. **避免拷贝大对象**：要把一个大容器交给另一个线程处理时，移动捕获可以避免拷贝一百万个元素。`big` 被移动后处于「有效但未指定」的状态，这里 libc++ 把它变成了空容器，但不应该依赖这一点。
4. **引用捕获并改名**：`[&c = counter]` 中 `c` 是 `counter` 的引用。
5. **自带状态的 lambda**：`[id = 0]` 不需要在外面定义变量，配合 `mutable`，lambda 就成了一个简单的 ID 生成器。

## 4. 安全地捕获成员变量

上一章《Lambda 表达式》中讲过一个陷阱：在成员函数中写 `[=]`，捕获的其实是 `this` 指针，对象销毁后 lambda 就会访问悬空指针。当时的解决办法是先把成员复制到局部变量里再捕获。有了初始化捕获，可以一步完成：

```cpp
#include <functional>
#include <iostream>

class Button {
public:
    explicit Button(int id) : id_(id) {}
    std::function<void()> makeHandler()
    {
        return [id = id_]() { std::cout << "按钮 " << id << " 被点击\n"; };   // 复制成员，不依赖 this
    }
private:
    int id_;
};

int main()
{
    std::function<void()> h;
    {
        Button b(7);
        h = b.makeHandler();
    }               // b 已销毁
    h();            // 安全：lambda 里保存的是 id 的副本
    return 0;
}
```

运行结果：

```
按钮 7 被点击
```

`[id = id_]` 把成员变量 `id_` 的**值**复制到 lambda 的成员 `id` 中，lambda 不再依赖 `this`，对象销毁后调用也是安全的。

## 5. 常见错误

### 5.1 移动捕获的 lambda 不能放进 std::function

```cpp
#include <functional>
#include <memory>
int main()
{
    auto p = std::make_unique<int>(1);
    auto lam = [p = std::move(p)]() { return *p; };
    std::function<int()> f = std::move(lam);   // std::function 要求可拷贝
    return f();
}
```

```
error: call to implicitly-deleted copy constructor of '(lambda at main.cpp:6:16)'
```

lambda 的成员 `p` 是 `unique_ptr`，所以整个闭包也只能移动、不能拷贝。而 **`std::function` 要求保存的对象必须可以拷贝**，即使你只是移动它进去。解决办法：

- 用 `auto` 保存 lambda，或者用模板参数传递它；
- 必须要类型擦除时，把资源换成 `shared_ptr`；
- C++23 提供了只要求可移动的 `std::move_only_function`。

### 5.2 初始化捕获的名字会遮蔽外部变量

```cpp
int x = 1;
auto f = [x = x + 1]() { return x; };   // 左边的 x 是新成员，右边的 x 是外部变量
```

`[x = x + 1]` 是合法的：等号右边在 lambda **外部**的作用域中求值，用的是外面的 `x`；左边定义的是 lambda 的新成员。这种写法容易让人困惑，建议给新成员起一个不同的名字。

### 5.3 泛型 lambda 的错误在调用时才暴露

泛型 lambda 的 `operator()` 是模板，只有在被调用、实例化时才检查函数体。`[](auto x) { return x.size(); }` 用 `int` 调用时才会报错，而且错误信息会指向 lambda 内部。

## 6. 进阶

### 6.1 泛型 lambda 也能转换成函数指针

无捕获的泛型 lambda 同样可以转换成函数指针，转换时根据函数指针的类型实例化对应的 `operator()`：

```cpp
int (*fp)(int) = [](auto x) { return x + 1; };   // 实例化 operator()<int>
fp(41);   // 42
```

### 6.2 在线程中使用移动捕获

把数据交给另一个线程处理，是移动捕获的典型场景：

```cpp
std::vector<int> data = loadData();
std::thread worker([d = std::move(data)]() {
    process(d);          // 线程独占这份数据，不需要加锁，也没有拷贝
});
worker.join();
```

数据的所有权明确地转移给了工作线程，主线程不再持有它，既避免了拷贝，也从根本上避免了数据竞争。

## 7. 小结

- **泛型 lambda**：参数写 `auto`，一个 lambda 能处理多种类型；本质是闭包类的 `operator()` 变成了模板
- 泛型 lambda 用 `auto&&` + `std::forward<decltype(x)>(x)` 完美转发；可以通过把自己作为参数来实现递归
- **初始化捕获** `[name = expr]`：在捕获列表中定义新成员，可以是任意表达式
- **移动捕获** `[p = std::move(ptr)]`：把只能移动的对象或大对象移进 lambda
- `[id = id_]` 复制成员变量，避免 `[=]` 隐式捕获 `this` 的悬空问题
- 移动捕获了 `unique_ptr` 的 lambda 不能放进 `std::function`

## 8. 练习

1. 写一个泛型 lambda `maxOf`，接受两个参数，返回较大的那个，能同时用于 `int`、`double` 和 `std::string`。
2. 下面的代码能否编译？如果不能，怎么修改？

   ```cpp
   auto res = std::make_unique<Resource>();
   std::thread t([res]() { res->use(); });
   ```

3. 用初始化捕获写一个 lambda，每次调用返回斐波那契数列的下一项：第一次调用返回 0，然后是 1、1、2、3、5……

<details>
<summary>点击查看答案</summary>

1. `auto maxOf = [](const auto& a, const auto& b) { return a < b ? b : a; };`。注意两个参数的类型要相同，否则三目运算符两个分支的类型不同，返回类型可能不是你想要的。
2. 不能。`[res]` 按值捕获需要拷贝 `unique_ptr`。改成移动捕获：`std::thread t([res = std::move(res)]() { res->use(); });`。
3. 参考代码：

   ```cpp
   auto nextFib = [a = 0LL, b = 1LL]() mutable {
       long long result = a;
       a = b;
       b = result + b;
       return result;
   };
   ```

   一个初始化捕获列表中可以有多个新成员，用逗号分隔。

</details>
