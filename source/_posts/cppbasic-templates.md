---
title: 模板入门
date: 2026-10-23 12:20:00
updated: 2026-10-23 12:20:00
categories:
  - C/C++
  - C++基础
tags:
  - C++基础
  - 模板
  - 泛型编程
order: 11
description: 模板让同一份代码适用于多种类型，是 STL 的基础。本文从重复的 max 函数讲起，介绍函数模板、类模板、非类型模板参数和模板特化，用 nm 看清编译器为每种类型生成的函数，并解释为什么模板的定义必须放在头文件中。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会写函数模板和类模板；理解模板参数推导和实例化；会用非类型模板参数和全特化；知道模板为什么要写在头文件中；能读懂常见的模板编译错误。
>
> **前置知识**：《运算符重载》、《异常处理》。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。

## 1. 问题：同样的逻辑，每种类型写一遍

求两个数中较大的那个：

```cpp
int    maxOf(int a, int b)       { return a < b ? b : a; }
double maxOf(double a, double b) { return a < b ? b : a; }
std::string maxOf(const std::string& a, const std::string& b) { return a < b ? b : a; }
```

用《从 C 到 C++》中介绍的函数重载，可以让它们同名，但**函数体完全一样**，只有类型不同。每增加一种类型，就要复制一遍；发现了 bug，要改好几处。

**模板**（template）让我们只写一份代码，把类型当作参数，由编译器为每种用到的类型生成对应的版本。这种编程方式叫**泛型编程**。STL 中的 `std::vector<int>`、`std::sort` 全部都是模板。

## 2. 函数模板

```cpp
#include <iostream>
#include <string>

template <typename T>
T maxOf(T a, T b)
{
    return a < b ? b : a;
}

int main()
{
    std::cout << maxOf(3, 7) << '\n';                                   // T = int
    std::cout << maxOf(2.5, 1.5) << '\n';                               // T = double
    std::cout << maxOf(std::string("apple"), std::string("banana")) << '\n';  // T = std::string
    std::cout << maxOf<double>(1, 2.5) << '\n';                         // 显式指定 T = double
    return 0;
}
```

运行结果：

```
7
2.5
banana
2.5
```

- **`template <typename T>`** 声明一个模板，`T` 是**模板参数**，代表「某种类型」。`typename` 也可以写成 `class`，两者在这里完全等价。
- 调用 `maxOf(3, 7)` 时，编译器根据实参的类型**推导**出 `T = int`，这叫**模板参数推导**。
- 也可以用 `maxOf<double>(...)` **显式指定**模板参数。

### 2.1 模板是「生成代码的模具」

模板本身不是函数，它是生成函数的**模具**。编译器看到 `maxOf(3, 7)` 时，才用 `int` 替换 `T`，生成一个真正的函数 `maxOf<int>`，这叫**实例化**（instantiation）。用 `nm` 查看编译出的程序中有哪些函数：

```bash
nm -C func | grep maxOf
```

```
0000000100000674 T int maxOf<int>(int, int)
00000001000006b8 T double maxOf<double>(double, double)
0000000100000748 T std::__1::basic_string<...> maxOf<std::__1::basic_string<...>>(...)
```

（第三行很长，这里用 `...` 省略了 `std::string` 的完整类型名。）

源代码中只有一个 `maxOf`，程序中却有**三个**函数，分别对应三种类型。这说明：

- 模板的类型替换**在编译期完成**，运行时没有任何额外开销，和手写三个函数一模一样。
- 用到的类型越多，生成的代码越多。这就是模板的代价：**编译时间变长、程序体积变大**。

### 2.2 推导失败

```cpp
template <typename T>
T maxOf(T a, T b) { return a < b ? b : a; }
int main() { return maxOf(1, 2.5) > 0; }
```

```
error: no matching function for call to 'maxOf'
note: candidate template ignored: deduced conflicting types for parameter 'T' ('int' vs. 'double')
```

第一个参数推导出 `T = int`，第二个推导出 `T = double`，产生了冲突。**模板参数推导时不会做隐式类型转换**，这和普通函数不同。解决方法是显式指定 `maxOf<double>(1, 2.5)`，或者让两个实参类型一致，比如 `maxOf(1.0, 2.5)`。

### 2.3 模板对类型的要求

`maxOf` 的函数体中用到了 `a < b`，所以 `T` 必须支持 `<` 运算符。如果传入一个不支持的类型：

```cpp
struct Point { int x, y; };
int main() { Point p{1, 2}, q{3, 4}; Point r = maxOf(p, q); }
```

```
error: invalid operands to binary expression ('Point' and 'Point')
note: in instantiation of function template specialization 'maxOf<Point>' requested here
```

错误发生在**实例化**的时候：编译器用 `Point` 替换 `T` 之后，发现 `a < b` 无法编译。读模板错误信息的技巧是：**先看 `error` 那一行是什么错，再找 `in instantiation of ... requested here` 那一行，它指向你自己的代码中出问题的调用**。真实项目中的模板错误可能有几十行，大部分是中间经过的模板，可以先跳过。

要让 `Point` 能用 `maxOf`，为它重载 `operator<` 就行（见《运算符重载》）。C++20 的 Concepts 可以把这种要求直接写在模板的声明中，让错误信息清楚得多，详见 C++20 章节的《Concepts：给模板参数加上约束》。

## 3. 类模板

函数可以是模板，类也可以。下面实现一个可以存放任意类型的栈：

```cpp
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

template <typename T>
class Stack {
public:
    void push(const T& value) { items_.push_back(value); }
    void pop()
    {
        if (items_.empty()) throw std::out_of_range("Stack::pop: 栈为空");
        items_.pop_back();
    }
    const T& top() const
    {
        if (items_.empty()) throw std::out_of_range("Stack::top: 栈为空");
        return items_.back();
    }
    bool empty() const { return items_.empty(); }
    std::size_t size() const { return items_.size(); }
private:
    std::vector<T> items_;
};

int main()
{
    Stack<int> numbers;
    for (int i = 1; i <= 3; ++i) numbers.push(i * 10);
    std::cout << "numbers: size = " << numbers.size() << ", top = " << numbers.top() << '\n';

    Stack<std::string> words;
    words.push("hello");
    words.push("template");
    while (!words.empty()) {
        std::cout << words.top() << ' ';
        words.pop();
    }
    std::cout << '\n';

    try {
        words.pop();
    } catch (const std::out_of_range& e) {
        std::cout << e.what() << '\n';
    }
    return 0;
}
```

运行结果：

```
numbers: size = 3, top = 30
template hello 
Stack::pop: 栈为空
```

- 使用类模板时，在类名后面用尖括号给出类型：`Stack<int>`、`Stack<std::string>`。**`Stack` 本身不是一个类型**，`Stack<int>` 才是。`Stack<int>` 和 `Stack<std::string>` 是两个完全不同的类。
- 类模板的成员函数**只有被调用时才会实例化**。如果某个 `T` 不支持某个成员函数中的操作，只要不调用那个函数，就不会报错。
- `push` 的参数用 `const T&`，因为 `T` 可能是 `std::string` 这样拷贝开销大的类型。写模板时要假设 `T` 可能是任何类型。

> C++17 起，类模板也可以根据构造函数的参数推导类型，比如 `std::vector v{1, 2, 3};` 可以省略 `<int>`，详见 C++17 章节的《折叠表达式与类模板参数推导》。

如果把成员函数写在类的外面，每个函数前都要重复模板声明：

```cpp
template <typename T>
void Stack<T>::push(const T& value)
{
    items_.push_back(value);
}
```

## 4. 非类型模板参数

模板参数除了类型，还可以是**整数常量**：

```cpp
#include <iostream>

template <typename T, std::size_t N>
class FixedArray {
public:
    T& operator[](std::size_t i) { return data_[i]; }
    const T& operator[](std::size_t i) const { return data_[i]; }
    constexpr std::size_t size() const { return N; }
private:
    T data_[N];
};

template <typename T, std::size_t N>
T sum(const FixedArray<T, N>& a)                 // T 和 N 都能自动推导
{
    T total{};
    for (std::size_t i = 0; i < N; ++i) total += a[i];
    return total;
}

int main()
{
    FixedArray<int, 5> a{};
    for (std::size_t i = 0; i < a.size(); ++i) a[i] = static_cast<int>(i + 1);
    std::cout << "size = " << a.size() << ", sum = " << sum(a) << ", sizeof = " << sizeof(a) << '\n';

    FixedArray<double, 3> b{};
    b[0] = 1.5; b[2] = 2.5;
    std::cout << "size = " << b.size() << ", sum = " << sum(b) << ", sizeof = " << sizeof(b) << '\n';
    return 0;
}
```

运行结果：

```
size = 5, sum = 15, sizeof = 20
size = 3, sum = 4, sizeof = 24
```

- `N` 必须是**编译期常量**，它被直接用作数组的长度。`sizeof(a)` 正好是 5 个 `int` 的 20 字节，对象中**没有额外保存长度**，`size()` 返回的 `N` 是编译期就确定的常量。
- `T total{};` 是**值初始化**：`int` 和 `double` 会被初始化为 0。写模板时用这种写法，可以让任何类型都得到一个合理的初值。
- 标准库的 **`std::array<T, N>`** 就是这样实现的，实际使用时直接用它即可。

## 5. 模板特化

通用的模板不一定对所有类型都正确。用 `maxOf` 比较两个 C 风格的字符串：

```cpp
const char* s1 = "banana";
const char* s2 = "apple";
std::cout << maxOf(s1, s2) << '\n';                      // 比较的是两个指针的地址
std::cout << (void*)s1 << ' ' << (void*)s2 << '\n';
```

```
apple
0x1009b50e4 0x1009b50eb
```

`T` 被推导为 `const char*`，`a < b` 比较的是**两个指针的地址**，而不是字符串的内容。`"apple"` 恰好存放在较高的地址，所以「较大」的是它。换一个编译器或者调整代码，结果都可能不同。

可以为 `const char*` 提供一个**全特化**版本：

```cpp
template <>
const char* maxOf<const char*>(const char* a, const char* b)     // 全特化：为 const char* 单独实现
{
    return std::strcmp(a, b) < 0 ? b : a;
}
```

再运行，输出就是正确的 `banana` 了。`template <>` 表示「这是某个模板的特化版本」，当 `T` 是 `const char*` 时，编译器使用这个版本而不是通用版本。

类模板也可以特化，最著名的例子是标准库的 `std::vector<bool>`，它为了节省空间把每个元素压缩成 1 个二进制位。

> 对于函数，用普通的重载 `const char* maxOf(const char* a, const char* b)` 往往比特化更简单，效果也一样。特化更常用于类模板。当然，最好的办法是直接用 `std::string`。

## 6. 模板的定义要放在头文件中

按照《static 成员、友元与内联函数》中的习惯，我们会把函数的声明放在 `.h` 中，定义放在 `.cpp` 中。对模板这样做：

```cpp
// maxof.h
template <typename T>
T maxOf(T a, T b);                 // 只有声明

// maxof.cpp
#include "maxof.h"
template <typename T>
T maxOf(T a, T b) { return a < b ? b : a; }   // 定义放在 .cpp 中

// main.cpp
#include "maxof.h"
int main() { std::cout << maxOf(3, 7) << '\n'; }
```

```bash
clang++ -std=c++17 main.cpp maxof.cpp -o main
```

```
Undefined symbols for architecture arm64:
  "int maxOf<int>(int, int)", referenced from:
      _main in main-66d44d.o
ld: symbol(s) not found for architecture arm64
```

**链接错误**：找不到 `maxOf<int>`。原因是：

- 编译 `main.cpp` 时，编译器只看到了声明，没法实例化，只能留下一个「需要 `maxOf<int>`」的记录，等链接时去找。
- 编译 `maxof.cpp` 时，编译器看到了模板的定义，但这个文件中**没有人使用 `maxOf<int>`**，所以不会生成它。
- 链接时，谁都没有生成 `maxOf<int>`，于是报错。

**规则：模板的完整定义要放在头文件中**，让每个使用它的 `.cpp` 都能看到定义并实例化。多个 `.cpp` 实例化出的同一个函数不会引起「重复定义」的错误，链接器会自动只保留一份。标准库的头文件 `<vector>`、`<algorithm>` 中都是完整的模板代码，原因就在这里。

## 7. 常见错误

1. **混用类型导致推导失败**：`maxOf(1, 2.5)`。显式指定模板参数，或者统一实参类型。
2. **把模板的定义放在 `.cpp` 中**：链接时报 `Undefined symbols`。
3. **把 `Stack` 当作类型使用**：`void f(Stack s)` 是错误的，要写 `Stack<int>`，或者把 `f` 也写成模板。
4. **假设 `T` 一定是简单类型**：参数按值传递，对 `std::string` 等类型产生不必要的拷贝；用 `T total = 0;` 初始化，对 `std::string` 无法编译。分别用 `const T&` 和 `T total{};`。
5. **被很长的错误信息吓到**：先找 `error:` 那一行，再找指向自己代码的 `requested here`。

## 8. 小结

- 模板把类型当作参数，只写一份代码，由编译器为每种类型生成对应的版本（**实例化**）
- 函数模板可以根据实参**推导**模板参数，推导时不做隐式转换；也可以显式指定
- 模板在编译期展开，没有运行时开销，代价是编译时间和程序体积（`nm` 能看到每种类型各一个函数）
- 类模板使用时要给出类型参数，`Stack<int>` 才是类型；成员函数只在被调用时实例化
- 非类型模板参数可以是编译期整数，`std::array<T, N>` 就是这样实现的
- 全特化 `template <>` 为特定类型提供单独的实现
- **模板的定义要放在头文件中**，否则链接时找不到实例

## 9. 练习

1. 写一个函数模板 `sumOf`，接受一个 `std::vector<T>`，返回所有元素的和。用 `std::vector<int>{1, 2, 3, 4}` 和 `std::vector<std::string>{"tem", "pla", "te"}` 测试。
2. 写一个类模板 `Pair<K, V>`，有两个公有成员 `first` 和 `second`，以及一个成员函数 `swapped()`，返回一个交换了两个成员的新 `Pair`（注意它的类型）。
3. 对于第 2 节的 `maxOf`，`maxOf("abc", "abd")` 中的 `T` 会被推导成什么？结果可靠吗？

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   template <typename T>
   T sumOf(const std::vector<T>& v)
   {
       T total{};                       // int 是 0，std::string 是空字符串
       for (const T& x : v) total += x;
       return total;
   }
   ```

   分别输出 `10` 和 `template`。注意两个细节：参数是 `const std::vector<T>&`，避免拷贝整个容器；`T total{}` 对两种类型都能得到正确的初值，而 `T total = 0` 对 `std::string` 就不对了。

2. 参考代码：

   ```cpp
   template <typename K, typename V>
   struct Pair {
       K first;
       V second;
       Pair<V, K> swapped() const { return {second, first}; }
   };

   Pair<std::string, int> p{"age", 18};
   Pair<int, std::string> q = p.swapped();      // q.first = 18, q.second = "age"
   ```

   `swapped()` 的返回类型是 `Pair<V, K>`，和 `Pair<K, V>` 是不同的类型（除非 `K` 和 `V` 相同）。标准库中对应的是 `std::pair`。

3. 字符串字面量 `"abc"` 的类型是 `const char[4]`，按值传递时退化为指针，所以 `T` 被推导为 `const char*`。比较的是两个字面量的地址，结果不可靠。如果提供了第 5 节的全特化版本，就会调用它，得到正确的结果 `"abd"`。

</details>
