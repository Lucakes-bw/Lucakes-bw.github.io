---
title: Lambda 表达式
date: 2026-10-22 11:00:00
updated: 2026-10-22 11:00:00
categories:
  - C/C++
  - C++11
tags:
  - C++11
  - Lambda
order: 7
description: Lambda 让你在需要函数的地方直接写一个匿名函数。本文讲清楚 lambda 的语法、按值和按引用捕获、mutable，lambda 背后的闭包类，以及悬空引用和 this 捕获这两个最危险的坑。
disableNunjucks: true
---

> **本文目标**：会写 lambda 并传给 `sort`、`count_if` 等算法；分清按值捕获和按引用捕获；知道 `mutable` 的作用；理解 lambda 是编译器生成的一个类；避开悬空引用和 `this` 捕获的陷阱。
>
> **编译环境**：`g++ -std=c++11` / `clang++ -std=c++11`。

## 1. 为什么需要 lambda

要把学生按分数从高到低排序，C++98 需要先在别处定义一个比较函数，或者一个重载了 `operator()` 的类（函数对象）：

```cpp
bool byScoreDesc(const Student& a, const Student& b) { return a.score > b.score; }
// ……很多行之后……
std::sort(students.begin(), students.end(), byScoreDesc);
```

比较逻辑只用一次，却要起个名字、写在远离使用位置的地方。如果比较时还要用到某个局部变量（比如及格线），普通函数就无能为力了，只能写一个带成员变量的函数对象类。

C++11 的 **lambda 表达式**让你在需要函数的地方**就地**写一个匿名函数，还能直接使用周围的局部变量。

## 2. 基本语法

```
[捕获列表](参数列表) -> 返回类型 { 函数体 }
```

- **捕获列表** `[]`：lambda 要使用哪些外部的局部变量，以及怎么使用。这是 lambda 和普通函数最大的区别。
- **参数列表** `()`：和普通函数一样。没有参数时可以连括号一起省略，例如 `[] { ... }`。
- **返回类型** `-> T`：通常可以省略，由编译器推导。
- **函数体** `{}`：和普通函数一样。

```cpp
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

struct Student {
    std::string name;
    int score;
};

int main()
{
    // 1. 最简单的 lambda：定义后立即调用
    auto hello = []() { std::cout << "Hello, lambda!\n"; };
    hello();

    // 2. 带参数和返回值
    auto add = [](int a, int b) { return a + b; };
    std::cout << "add(3, 4) = " << add(3, 4) << '\n';

    // 3. 作为算法的参数：按分数从高到低排序
    std::vector<Student> students{{"Alice", 85}, {"Bob", 92}, {"Carol", 78}};
    std::sort(students.begin(), students.end(),
              [](const Student& a, const Student& b) { return a.score > b.score; });
    for (const auto& s : students)
        std::cout << s.name << ' ' << s.score << '\n';

    // 4. 统计及格人数
    int passLine = 80;
    auto passed = std::count_if(students.begin(), students.end(),
                                [passLine](const Student& s) { return s.score >= passLine; });
    std::cout << "及格人数: " << passed << '\n';
    return 0;
}
```

运行结果：

```
Hello, lambda!
add(3, 4) = 7
Bob 92
Alice 85
Carol 78
及格人数: 2
```

第 4 个例子里，`[passLine]` 表示把局部变量 `passLine` **捕获**进 lambda，这样函数体里才能使用它。如果不写，编译器会报错：lambda 默认看不到外面的局部变量。

> 全局变量、静态变量不需要捕获，lambda 可以直接使用。捕获只针对**局部变量**（包括函数参数）。

## 3. 捕获列表

### 3.1 按值捕获与按引用捕获

```cpp
#include <iostream>

int main()
{
    int a = 1, b = 2;

    auto byValue = [a]() { return a; };          // 按值捕获：创建 lambda 时复制一份
    auto byRef   = [&b]() { return b; };         // 按引用捕获：使用时读取 b 的当前值
    a = 100;
    b = 200;
    std::cout << "byValue() = " << byValue() << ", byRef() = " << byRef() << '\n';

    auto incRef = [&a]() { ++a; };               // 引用捕获可以修改外部变量
    incRef();
    std::cout << "a = " << a << '\n';

    int counter = 0;
    auto incCopy = [counter]() mutable {         // mutable：允许修改按值捕获的副本
        ++counter;
        return counter;
    };
    int r1 = incCopy();
    int r2 = incCopy();
    int r3 = incCopy();
    std::cout << r1 << ' ' << r2 << ' ' << r3 << "，外部 counter = " << counter << '\n';

    int x = 10, y = 20;
    auto all = [=]() { return x + y; };          // [=]：用到的变量全部按值捕获
    auto allRef = [&]() { x = 0; y = 0; };       // [&]：用到的变量全部按引用捕获
    auto mixed = [=, &y]() { y = x; };           // 默认按值，y 按引用
    std::cout << "all() = " << all() << '\n';
    mixed();
    std::cout << "mixed 后 y = " << y << '\n';
    allRef();
    std::cout << "allRef 后 x = " << x << ", y = " << y << '\n';
    return 0;
}
```

运行结果：

```
byValue() = 1, byRef() = 200
a = 101
1 2 3，外部 counter = 0
all() = 30
mixed 后 y = 10
allRef 后 x = 0, y = 0
```

重点看第一行：

- `byValue` 在**创建时**就把 `a` 的值 1 复制了进去，之后 `a` 变成 100，它也不知道。
- `byRef` 保存的是 `b` 的引用，**调用时**才去读 `b`，读到的是 200。

### 3.2 mutable

按值捕获的变量在 lambda 里**默认是只读的**，写 `++counter` 会编译失败。加上 `mutable` 后可以修改，但修改的是 lambda 内部的**副本**：这个副本在多次调用之间会保留，所以输出是 `1 2 3`；而外部的 `counter` 始终是 0。

### 3.3 捕获方式一览

| 写法 | 含义 |
| --- | --- |
| `[]` | 不捕获任何变量 |
| `[x]` | `x` 按值捕获 |
| `[&x]` | `x` 按引用捕获 |
| `[=]` | 用到的所有局部变量都按值捕获 |
| `[&]` | 用到的所有局部变量都按引用捕获 |
| `[=, &x]` | 默认按值，`x` 按引用 |
| `[&, x]` | 默认按引用，`x` 按值 |
| `[this]` | 捕获当前对象的指针，可以访问成员变量和成员函数 |

**建议：尽量显式列出要捕获的变量**，少用 `[=]` 和 `[&]`。显式列出让读代码的人一眼看出 lambda 依赖哪些外部变量，也能避免第 6 节的陷阱。

## 4. 返回类型

函数体只有一条 `return` 语句，或者多条 `return` 返回同一类型时，编译器能自动推导返回类型。多条 `return` 的类型不同时，需要用 `->` 显式指定：

```cpp
#include <iostream>
int main()
{
    auto ok = [](int x) { return x * 2.5; };                    // 单条 return，推导为 double
    auto multi = [](int x) -> double {                          // 多条 return 类型不同，显式指定
        if (x > 0) return x;
        return 0.5;
    };
    std::cout << ok(2) << ' ' << multi(3) << ' ' << multi(-1) << '\n';

    // 立即调用的 lambda：用复杂逻辑初始化 const 变量
    int mode = 2;
    const char* const name = [&]() {
        switch (mode) {
            case 1: return "快速";
            case 2: return "标准";
            default: return "未知";
        }
    }();
    std::cout << "模式: " << name << '\n';
    return 0;
}
```

运行结果：

```
5 3 0.5
模式: 标准
```

第二个例子是一个实用的技巧：**定义完立即调用**（注意结尾的 `()`）。当一个变量的初始值需要用 `switch`、循环等复杂逻辑计算，又希望它是 `const` 时，就可以这样写。

## 5. lambda 的本质：闭包类

lambda 并不是什么魔法。编译器遇到 lambda 表达式时，会**生成一个匿名的类**，捕获的变量成为它的成员，函数体成为它的 `operator()`，然后创建这个类的一个对象。这个对象叫做**闭包**（closure）。

```cpp
#include <iostream>

// 编译器为 [factor](int x) { return x * factor; } 生成的类，大致相当于：
class __Lambda_Multiply {
public:
    explicit __Lambda_Multiply(int factor) : factor_(factor) {}
    int operator()(int x) const { return x * factor_; }   // 注意默认是 const 成员函数
private:
    int factor_;                                            // 捕获的变量变成了成员
};

int main()
{
    int factor = 3;
    auto lam = [factor](int x) { return x * factor; };
    __Lambda_Multiply obj(factor);
    std::cout << lam(5) << ' ' << obj(5) << '\n';

    int a = 1;
    double d = 2.0;
    auto empty = []() {};
    auto oneInt = [a]() { return a; };
    auto intAndDouble = [a, d]() { return a + d; };
    auto byRef = [&a, &d]() { return a + d; };
    std::cout << "sizeof: 无捕获 " << sizeof(empty)
              << ", 捕获 int " << sizeof(oneInt)
              << ", 捕获 int+double " << sizeof(intAndDouble)
              << ", 引用捕获两个 " << sizeof(byRef) << '\n';

    // 每个 lambda 都有自己独一无二的类型
    auto l1 = []() { return 1; };
    auto l2 = []() { return 1; };
    // l1 = l2;   // 错误：类型不同
    (void)l1; (void)l2;

    // 没有捕获的 lambda 可以转换成函数指针
    int (*fp)(int, int) = [](int x, int y) { return x * y; };
    std::cout << "fp(6, 7) = " << fp(6, 7) << '\n';
    return 0;
}
```

运行结果（64 位系统）：

```
15 15
sizeof: 无捕获 1, 捕获 int 4, 捕获 int+double 16, 引用捕获两个 16
fp(6, 7) = 42
```

从闭包类的角度，前面的规则都能解释通：

- **按值捕获** = 构造时把变量**复制**成成员，所以创建后外部变量再变化也不影响它。
- **按引用捕获** = 成员是一个引用（实现上通常是指针），所以引用捕获两个变量占 16 字节，正好是两个指针。
- **`operator()` 默认是 `const` 成员函数**，所以不能修改按值捕获的成员；`mutable` 的作用就是去掉这个 `const`。
- **无捕获的 lambda** 是一个空类，`sizeof` 为 1（C++ 中对象至少占 1 字节）。它不依赖任何状态，所以可以转换成普通的**函数指针**，这在调用 C 语言 API（例如 `qsort`、`pthread_create`）时很有用。
- **每个 lambda 表达式都会生成一个不同的类**，即使两个 lambda 一模一样，类型也不同。所以 lambda 只能用 `auto` 保存，或者用下一篇介绍的 `std::function`。

## 6. 常见错误

### 6.1 引用捕获的变量已经销毁

```cpp
#include <functional>
#include <iostream>

std::function<int()> makeCounter()
{
    int count = 0;
    return [&count]() { return ++count; };   // 返回后 count 已经销毁！
}

int main()
{
    auto counter = makeCounter();
    std::cout << counter() << '\n';
    return 0;
}
```

`count` 是 `makeCounter` 的局部变量，函数返回时就销毁了，而 lambda 里保存的引用还指向它。用 AddressSanitizer 运行（需要设置 `ASAN_OPTIONS=detect_stack_use_after_return=1`）：

```
ERROR: AddressSanitizer: stack-use-after-return
```

**规则：如果 lambda 的生命周期比它捕获的局部变量更长（被返回、被保存起来、交给另一个线程），就必须按值捕获。** 这里改成 `[count]() mutable { return ++count; }` 即可。

### 6.2 [=] 并没有复制成员变量

这是最隐蔽的坑：

```cpp
#include <functional>
#include <iostream>
#include <vector>

class Button {
public:
    explicit Button(int id) : id_(id) {}
    std::function<void()> makeHandler()
    {
        return [=]() { std::cout << "按钮 " << id_ << " 被点击\n"; };   // [=] 实际捕获的是 this 指针
    }
    std::function<void()> makeSafeHandler()
    {
        int id = id_;
        return [id]() { std::cout << "按钮 " << id << " 被点击\n"; };   // 复制一份需要的成员
    }
private:
    int id_;
};

int main()
{
    std::function<void()> h1, h2;
    {
        Button b(7);
        h1 = b.makeHandler();
        h2 = b.makeSafeHandler();
    }               // b 在这里销毁
    h2();           // 安全
    h1();           // 通过悬空的 this 访问 id_，未定义行为
    return 0;
}
```

用 AddressSanitizer 运行：

```
按钮 7 被点击
ERROR: AddressSanitizer: stack-use-after-scope
```

看起来 `[=]` 是「全部按值捕获」，`id_` 应该被复制了一份。其实**成员变量不是局部变量，不能被捕获**。lambda 里写 `id_`，实际上是 `this->id_`，`[=]` 捕获的是 **`this` 指针**。对象 `b` 销毁后，`this` 就成了悬空指针。

正确的做法是像 `makeSafeHandler` 那样，先把需要的成员复制到局部变量里再捕获。C++14 可以写成 `[id = id_]`，C++17 可以用 `[*this]` 复制整个对象，C++20 则不再允许 `[=]` 隐式捕获 `this`（会给出弃用警告），要求必须明确写出 `[=, this]`。

## 7. 进阶

### 7.1 lambda 与函数对象的性能

lambda 的类型是编译器生成的具体类，`operator()` 的定义在编译期完全可见，所以传给 `std::sort` 这样的模板时，**编译器可以把它内联**，性能和手写循环一样。相比之下，传函数指针时，编译器通常要通过指针间接调用。这也是现代 C++ 推荐用 lambda 代替函数指针的原因之一。

### 7.2 捕获发生在什么时候

捕获发生在 **lambda 表达式求值时**（即创建闭包对象时），而不是调用时。所以在循环中创建 lambda 时，每次迭代都会捕获当时的值：

```cpp
std::vector<std::function<int()>> fs;
for (int i = 0; i < 3; ++i)
    fs.push_back([i]() { return i; });   // 分别捕获 0、1、2
```

如果这里写成 `[&i]`，三个 lambda 都引用同一个循环变量 `i`，而 `i` 在循环结束后就销毁了。

### 7.3 递归 lambda

lambda 没有名字，不能直接调用自己。C++11 中可以借助 `std::function`：

```cpp
std::function<int(int)> fib = [&fib](int n) {
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
};
```

这里必须写出 `std::function<int(int)>` 而不能用 `auto`，因为 lambda 体内用到 `fib` 时，它的类型必须已知。C++14 的泛型 lambda 和 C++23 的 `this auto` 提供了更好的写法，会在后续文章中介绍。

## 8. 小结

- lambda 语法：`[捕获](参数) -> 返回类型 { 函数体 }`，适合就地定义只用一次的小函数
- **按值捕获**在创建时复制，**按引用捕获**在调用时读取当前值
- 按值捕获的变量默认只读，`mutable` 允许修改内部副本
- lambda 是编译器生成的**闭包类**的对象：捕获的变量是成员，函数体是 `const` 的 `operator()`
- 无捕获的 lambda 可以转换成函数指针
- lambda 比局部变量活得久时，**必须按值捕获**；`[=]` 捕获的是 `this` 而不是成员变量的副本

## 9. 练习

1. 用 `std::sort` 和 lambda，把 `std::vector<std::string>` 按字符串长度从短到长排序，长度相同的按字典序排序。
2. 下面的代码输出什么？

   ```cpp
   int x = 1;
   auto f = [x]() mutable { return ++x; };
   auto g = [&x]() { return ++x; };
   f(); f(); g();
   std::cout << x << ' ' << f() << '\n';
   ```

3. 写一个函数 `makeMultiplier(int n)`，返回一个 lambda，这个 lambda 接收 `int x` 并返回 `x * n`。捕获方式应该怎么选？

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   std::sort(words.begin(), words.end(), [](const std::string& a, const std::string& b) {
       if (a.size() != b.size()) return a.size() < b.size();
       return a < b;
   });
   ```

2. 输出 `2 4`。`f` 内部的副本从 1 开始，调用两次后变为 3，不影响外部的 `x`；`g()` 把外部的 `x` 从 1 变成 2，所以先输出 `x` 的值 2；最后再调用 `f()`，副本从 3 变成 4。`f()` 只修改它内部的副本，不影响外部的 `x`，所以同一条语句里先读 `x` 还是先调用 `f()` 都不影响结果。
3. 必须按值捕获，因为返回后 `n` 就销毁了：

   ```cpp
   std::function<int(int)> makeMultiplier(int n)
   {
       return [n](int x) { return x * n; };
   }
   ```

</details>
