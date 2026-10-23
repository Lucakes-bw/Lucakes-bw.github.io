---
title: 引用：变量的别名
date: 2026-10-23 09:20:00
updated: 2026-10-23 09:20:00
categories:
  - C/C++
  - C++基础
tags:
  - C++基础
  - 引用
order: 2
description: 引用是 C++ 相对于 C 最重要的改进之一。本文讲清楚引用的本质、引用与指针的区别、用引用传参避免拷贝（实测 1000 次调用省下 4MB 拷贝）、const 引用、返回引用，以及返回局部变量引用和容器元素引用失效两大陷阱。
disableNunjucks: true
---

> **本文目标**：理解引用是变量的别名；会用引用作函数参数修改实参、用 `const` 引用避免拷贝；知道 `const` 引用能绑定临时对象；会用引用作返回值；分清引用和指针；避开悬空引用的陷阱。
>
> **前置知识**：上一篇《从 C 到 C++》，C 语言的指针。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。

## 1. 引用是变量的别名

```cpp
#include <iostream>

int main()
{
    int a = 10;
    int& r = a;            // r 是 a 的引用（别名），必须在定义时初始化

    r = 20;                // 通过 r 修改的就是 a
    std::cout << "a = " << a << ", r = " << r << '\n';
    std::cout << "&a = " << &a << "\n&r = " << &r << '\n';   // 地址完全相同

    int b = 99;
    r = b;                 // 注意：这不是让 r 改为引用 b，而是把 b 的值赋给 a
    std::cout << "r = b 之后: a = " << a << ", b = " << b << '\n';
    return 0;
}
```

运行结果：

```
a = 20, r = 20
&a = 0x16f306548
&r = 0x16f306548
r = b 之后: a = 99, b = 99
```

`int& r = a;` 定义了一个**引用** `r`，它是变量 `a` 的**别名**。`r` 和 `a` 是同一块内存的两个名字，所以：

- 修改 `r` 就是修改 `a`；
- `&r` 和 `&a` 的结果完全相同。

引用有两条铁律：

1. **定义时必须初始化**，指明它是谁的别名。`int& r;` 无法编译。
2. **一旦绑定，就不能再改为引用别的变量**。`r = b;` 并不是让 `r` 改为引用 `b`，而是把 `b` 的值**赋给** `r` 所引用的 `a`，所以 `a` 变成了 99。

## 2. 引用作函数参数

### 2.1 修改实参

C 语言中，函数要修改调用者的变量，只能传指针。C++ 中可以传引用：

```cpp
#include <iostream>

void swapByValue(int a, int b)    { int t = a; a = b; b = t; }      // 交换的是副本
void swapByPointer(int* a, int* b) { int t = *a; *a = *b; *b = t; } // C 语言的写法
void swapByRef(int& a, int& b)    { int t = a; a = b; b = t; }      // C++ 的写法

int main()
{
    int x = 1, y = 2;
    swapByValue(x, y);
    std::cout << "按值:   x = " << x << ", y = " << y << '\n';
    x = 1; y = 2;
    swapByPointer(&x, &y);
    std::cout << "按指针: x = " << x << ", y = " << y << '\n';
    x = 1; y = 2;
    swapByRef(x, y);
    std::cout << "按引用: x = " << x << ", y = " << y << '\n';
    return 0;
}
```

运行结果：

```
按值:   x = 1, y = 2
按指针: x = 2, y = 1
按引用: x = 2, y = 1
```

- **按值传递**：参数 `a`、`b` 是实参的**副本**，函数内交换的是副本，`x`、`y` 不变。
- **按指针传递**：能交换成功，但调用时要写 `&x`，函数内要写 `*a`，而且指针可能是空指针，严谨的代码还要检查。
- **按引用传递**：参数 `a`、`b` 就是 `x`、`y` 的别名，函数体和按值传递一样简洁，调用时也不用写 `&`，而且**引用不可能为空**。

上一篇中 `std::cin >> x` 不需要写 `&x`，原因就在这里：`operator>>` 的参数是引用。

### 2.2 const 引用：避免拷贝

按值传递还有一个问题：**拷贝的开销**。传递一个大对象时，每次调用都要完整地拷贝一份：

```cpp
#include <iostream>
#include <string>

struct Big {
    char data[4096];
    Big() = default;
    Big(const Big&) { ++copies; }          // 统计拷贝次数
    static int copies;
};
int Big::copies = 0;

int sumByValue(Big b)           { return b.data[0]; }       // 每次调用都拷贝 4KB
int sumByConstRef(const Big& b) { return b.data[0]; }       // 不拷贝，也不能修改

void greet(const std::string& name) { std::cout << "你好，" << name << '\n'; }

int main()
{
    Big big{};
    for (int i = 0; i < 1000; ++i) sumByValue(big);
    std::cout << "按值传递 1000 次: 拷贝 " << Big::copies << " 次，共 " << Big::copies * 4 << " KB\n";
    Big::copies = 0;
    for (int i = 0; i < 1000; ++i) sumByConstRef(big);
    std::cout << "按 const 引用传递 1000 次: 拷贝 " << Big::copies << " 次\n";

    greet("Alice");                  // const 引用可以绑定到临时对象
    std::string s = "Bob";
    greet(s);
    return 0;
}
```

运行结果：

```
按值传递 1000 次: 拷贝 1000 次，共 4000 KB
按 const 引用传递 1000 次: 拷贝 0 次
你好，Alice
你好，Bob
```

（`Big(const Big&)` 是「拷贝构造函数」，对象被拷贝时调用，这里用它来计数，会在《构造与析构》中详细介绍。）

按值传递调用 1000 次，拷贝了约 4MB 数据；按 **`const` 引用**传递，一次都没有拷贝。`const` 保证函数不会修改实参，调用者可以放心地传入。

**传参的选择规则：**

| 场景 | 参数写法 |
| --- | --- |
| `int`、`double`、指针等小类型，只读 | 按值：`int x` |
| `string`、`vector`、自定义类等大对象，只读 | **`const T&`** |
| 需要修改实参 | `T&` |

### 2.3 const 引用可以绑定临时对象

`greet("Alice")` 中，`"Alice"` 是一个字符串字面量，不是 `std::string`。编译器会创建一个临时的 `std::string` 对象，`const std::string&` 参数绑定到这个临时对象上。但**非 const 引用不能绑定临时对象**：

```cpp
#include <string>
void modify(std::string& s);
void read(const std::string& s);
int main()
{
    int& r1 = 10;                    // 错误：非 const 引用不能绑定字面量
    const int& r2 = 10;              // OK
    modify("text");                  // 错误：非 const 引用不能绑定临时对象
    read("text");                    // OK
    int& r3;                         // 错误：引用必须初始化
    return r1 + r2 + r3;
}
```

```
error: non-const lvalue reference to type 'int' cannot bind to a temporary of type 'int'
error: no matching function for call to 'modify'
error: declaration of reference variable 'r3' requires an initializer
```

道理很简单：`modify` 想要修改参数，但如果参数是一个马上就消失的临时对象，修改它毫无意义，几乎一定是写错了，所以编译器禁止这样做。这也是为什么只读参数一定要写 `const`：写了 `const`，调用者才能传入字面量和临时对象。

## 3. 引用作返回值

```cpp
#include <iostream>
#include <string>
#include <vector>

class Scores {
public:
    int& at(std::size_t i) { return data_[i]; }                 // 返回引用：调用者可以修改元素
    const std::string& name() const { return name_; }           // 返回 const 引用：避免拷贝，只读
private:
    std::vector<int> data_{80, 90, 70};
    std::string name_ = "期中考试";
};

int& bad()
{
    int local = 42;
    return local;                    // 错误：返回局部变量的引用
}

int main()
{
    Scores s;
    s.at(1) = 100;                   // 函数调用出现在等号左边
    std::cout << s.name() << ": " << s.at(0) << ' ' << s.at(1) << ' ' << s.at(2) << '\n';
    int& r = bad();
    std::cout << r << '\n';
    return 0;
}
```

（`class` 的写法会在《类与对象》中介绍，这里只需关注两个成员函数的返回类型。）

- `at` 返回 `int&`，是元素的别名，所以 **`s.at(1) = 100;` 能直接修改元素**，函数调用出现在了赋值号的左边。`std::vector` 的 `operator[]`、`std::map` 的 `operator[]` 都是这样实现的。
- `name` 返回 `const std::string&`，调用者可以读取，但不会拷贝字符串，也不能修改它。

### 3.1 陷阱：返回局部变量的引用

`bad()` 返回了局部变量 `local` 的引用。函数返回时 `local` 就被销毁了，调用者拿到的是一个指向已经销毁的变量的引用。编译器给出了警告：

```
warning: reference to stack memory associated with local variable 'local' returned [-Wreturn-stack-address]
```

用 AddressSanitizer 运行：

```
ERROR: AddressSanitizer: stack-use-after-return
```

**规则：只能返回「函数结束后仍然存在」的对象的引用**：成员变量、全局变量、`static` 变量、通过参数传进来的引用。局部变量必须按值返回。

### 3.2 陷阱：容器元素的引用失效

```cpp
#include <iostream>
#include <vector>
int main()
{
    std::vector<int> v{1, 2, 3};
    int& first = v[0];               // 引用 vector 的元素
    v.push_back(4);                  // 容量不够，vector 搬到新内存，first 悬空
    std::cout << first << '\n';
    return 0;
}
```

用 AddressSanitizer 运行：

```
ERROR: AddressSanitizer: heap-use-after-free
```

`vector` 的元素存放在一块连续的堆内存中。`push_back` 时如果容量不够，`vector` 会申请一块更大的内存，把元素搬过去，然后**释放旧内存**。`first` 引用的仍然是旧内存中的元素。这类 bug 在不开 ASan 时往往能「正常运行」，输出看似正确的结果，非常隐蔽。**持有容器元素的引用（或指针、迭代器）时，不要修改容器的大小。**

## 4. 引用与指针

```cpp
#include <iostream>
struct WithRef { int& r; };
struct WithPtr { int* p; };
int main()
{
    double d = 3.14;
    double& rd = d;
    std::cout << "sizeof(rd) = " << sizeof(rd) << "（就是 double 的大小）\n";
    std::cout << "sizeof(WithRef) = " << sizeof(WithRef) << ", sizeof(WithPtr) = " << sizeof(WithPtr) << '\n';
    return 0;
}
```

运行结果（64 位系统）：

```
sizeof(rd) = 8（就是 double 的大小）
sizeof(WithRef) = 8, sizeof(WithPtr) = 8
```

`sizeof(rd)` 得到的是 `d` 的大小，因为引用就是 `d` 本身的另一个名字。但在结构体中，引用成员和指针成员一样占 8 个字节：**在底层，编译器通常就是用指针来实现引用的**。区别在于语法和规则：

| | 引用 `int&` | 指针 `int*` |
| --- | --- | --- |
| 必须初始化 | 是 | 否 |
| 可以为空 | 否 | 可以是 `nullptr` |
| 可以改为指向别的对象 | 否 | 可以 |
| 访问对象 | 直接用名字 | 需要 `*p` 或 `p->` |
| 有「引用的引用」「引用的数组」 | 没有 | 有指针的指针、指针数组 |

**经验：能用引用就用引用；需要「可能为空」或「可以改变指向」时，才用指针。**

## 5. 常见错误

1. **以为 `r = b;` 会让引用改为引用 `b`**：它只是赋值。
2. **只读参数忘记写 `const`**：调用者就无法传入字面量和临时对象。
3. **返回局部变量的引用**：见第 3.1 节。
4. **持有容器元素的引用时修改容器**：见第 3.2 节。
5. **对小类型也用 `const` 引用**：`const int&` 并不比 `int` 快，反而多了一次间接访问。小类型按值传递即可。

## 6. 进阶：引用与范围 for

C++11 的范围 for 循环中，引用的用法和函数参数完全一致：

```cpp
std::vector<std::string> names{"Alice", "Bob"};
for (std::string s : names)        { }   // 每次拷贝一个 string
for (const std::string& s : names) { }   // 只读，不拷贝
for (std::string& s : names)       { s += "!"; }   // 修改元素
```

C++11 还引入了**右值引用** `T&&`，它是移动语义的基础，相关内容见 C++11 章节的《右值引用与移动语义》。本文讨论的 `T&` 在那里被称为「左值引用」。

## 7. 小结

- 引用是变量的别名，与原变量共享同一块内存；必须初始化，绑定后不能改变
- 引用作参数可以修改实参，比指针简洁，并且不可能为空
- 大对象只读时用 **`const T&`** 传参，避免拷贝；`const` 引用可以绑定字面量和临时对象，非 const 引用不行
- 引用作返回值可以让函数调用出现在赋值号左边；**不能返回局部变量的引用**
- 持有容器元素的引用时修改容器大小，引用会失效
- 底层通常用指针实现；能用引用就用引用，需要可空或可改变指向时用指针

## 8. 练习

1. 写一个函数 `void minMax(const std::vector<int>& v, int& mn, int& mx)`，通过引用参数同时返回最小值和最大值。
2. 下面三个函数，哪个的写法有问题？

   ```cpp
   const std::string& longer(const std::string& a, const std::string& b) { return a.size() >= b.size() ? a : b; }
   std::string& makeGreeting(const std::string& name) { std::string s = "Hi, " + name; return s; }
   int& counter() { static int c = 0; return ++c; }
   ```

3. 调用 `longer(std::string("abc"), std::string("de"))` 并把结果保存为 `const std::string& r = ...;`，之后使用 `r` 安全吗？

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   void minMax(const std::vector<int>& v, int& mn, int& mx)
   {
       mn = mx = v.at(0);
       for (int x : v) {
           if (x < mn) mn = x;
           if (x > mx) mx = x;
       }
   }
   ```

   C++17 中也可以返回一个 `std::pair<int, int>`，配合结构化绑定 `auto [mn, mx] = minMax(v);` 使用，可读性更好。

2. `makeGreeting` 有问题：返回了局部变量 `s` 的引用，应该改为按值返回 `std::string`。`longer` 返回的是参数之一的引用，`counter` 返回的是 `static` 变量的引用，都是合法的。

3. 不安全。两个临时 `string` 在这条语句结束时就被销毁了，`r` 引用的是其中一个已经销毁的临时对象。「`const` 引用延长临时对象的生命周期」只在**直接**用临时对象初始化 `const` 引用时才有效（如 `const std::string& r = std::string("abc");`），通过函数参数和返回值「转手」之后就不再延长了。

</details>
