---
title: 右值引用与移动语义
date: 2026-10-22 17:00:00
updated: 2026-10-22 17:00:00
categories:
  - C/C++
  - C++11
tags:
  - C++11
  - 移动语义
  - 右值引用
order: 5
description: C++11 最重要的特性之一。从「拷贝太贵」的问题出发，讲清楚左值与右值、右值引用、移动构造与移动赋值、std::move 的本质、noexcept 对 vector 性能的影响，以及返回值优化。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：分清左值和右值；理解右值引用 `T&&`；会为管理资源的类编写移动构造函数和移动赋值运算符；明白 `std::move` 其实什么都不移动；知道为什么移动构造函数要加 `noexcept`；知道什么时候**不该**写 `std::move`。
>
> **前置知识**：类、构造函数、析构函数、拷贝构造函数、`new`/`delete`。
>
> **编译环境**：`g++ -std=c++11` / `clang++ -std=c++11`。

## 1. 问题：拷贝太贵了

假设有一个管理动态数组的类 `Buffer`，它的拷贝构造函数需要**深拷贝**：分配一块同样大小的新内存，再把数据逐个复制过去。

```cpp
Buffer(const Buffer& other) : size_(other.size_), data_(new int[other.size_])
{
    std::memcpy(data_, other.data_, size_ * sizeof(int));
}
```

如果数组有一百万个元素，每次拷贝都要分配 4MB 内存并复制 4MB 数据。拷贝本身没有错，但很多拷贝是**完全没必要的**：

```cpp
Buffer makeBuffer() { Buffer tmp(1000000); /* 填充数据 */ return tmp; }

Buffer b = makeBuffer();              // 返回的临时对象马上就要销毁
std::vector<Buffer> v;
v.push_back(Buffer(1000000));         // 这个临时对象也马上就要销毁
```

这些临时对象**马上就要被销毁**了，没有人会再使用它们。与其辛辛苦苦复制一份，再把原来的销毁，不如**直接把它的内存「拿」过来**：

<figure class="diagram">
<svg viewBox="0 0 700 230" role="img" aria-label="拷贝与移动的内存对比：拷贝时 b 新分配内存并复制数据；移动时 b 直接接管 tmp 的内存，tmp 的指针置空">
  <defs>
    <marker id="mv-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/>
    </marker>
  </defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="170" y="22" text-anchor="middle" style="fill:var(--fg);font-weight:700">拷贝（深拷贝）</text>
    <text x="530" y="22" text-anchor="middle" style="fill:var(--accent);font-weight:700">移动</text>
    <line x1="350" y1="10" x2="350" y2="220" style="stroke:var(--border-2);stroke-dasharray:4 4"/>
    <!-- 拷贝 -->
    <rect x="30" y="45" width="120" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2)"/>
    <text x="90" y="70" text-anchor="middle" style="fill:var(--fg)">tmp.data</text>
    <rect x="190" y="45" width="120" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2)"/>
    <text x="250" y="70" text-anchor="middle" style="fill:var(--fg)">b.data</text>
    <line x1="90" y1="85" x2="90" y2="133" style="stroke:var(--fg-2)" marker-end="url(#mv-arrow)"/>
    <line x1="250" y1="85" x2="250" y2="133" style="stroke:var(--fg-2)" marker-end="url(#mv-arrow)"/>
    <rect x="30" y="135" width="120" height="40" rx="6" style="fill:var(--code-bg);stroke:var(--border-2)"/>
    <text x="90" y="160" text-anchor="middle" style="fill:var(--fg)">[1, 2, 3, …]</text>
    <rect x="190" y="135" width="120" height="40" rx="6" style="fill:var(--code-bg);stroke:var(--warn);stroke-width:1.5"/>
    <text x="250" y="160" text-anchor="middle" style="fill:var(--fg)">[1, 2, 3, …]</text>
    <text x="90" y="200" text-anchor="middle" style="fill:var(--muted)">原数据</text>
    <text x="250" y="200" text-anchor="middle" style="fill:var(--warn)">新分配 + 逐个复制</text>
    <!-- 移动 -->
    <rect x="390" y="45" width="120" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--border-2)"/>
    <text x="450" y="70" text-anchor="middle" style="fill:var(--muted)">tmp.data</text>
    <text x="450" y="112" text-anchor="middle" style="fill:var(--muted)">= nullptr</text>
    <rect x="550" y="45" width="120" height="40" rx="6" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="610" y="70" text-anchor="middle" style="fill:var(--fg)">b.data</text>
    <line x1="610" y1="85" x2="610" y2="133" style="stroke:var(--accent)" marker-end="url(#mv-arrow)"/>
    <rect x="550" y="135" width="120" height="40" rx="6" style="fill:var(--code-bg);stroke:var(--border-2)"/>
    <text x="610" y="160" text-anchor="middle" style="fill:var(--fg)">[1, 2, 3, …]</text>
    <text x="610" y="200" text-anchor="middle" style="fill:var(--accent)">原来那块内存，被 b 接管</text>
  </g>
</svg>
</figure>

移动只需要复制一个指针，再把原来的指针置空，无论数组多大都是常数时间。

问题是：**编译器怎么知道一个对象「马上就要销毁、可以被拿走」？** 这就需要区分左值和右值。

## 2. 左值与右值

一个简单的判断方法：**能对它取地址（`&`）的就是左值，不能的就是右值。**

| | 左值（lvalue） | 右值（rvalue） |
| --- | --- | --- |
| 特点 | 有名字、有固定的内存位置，可以多次使用 | 临时的，表达式结束后就消失 |
| 例子 | 变量 `x`、`arr[0]`、`*p`、返回引用的函数调用 | 字面量 `10`、`x + 1`、返回值的函数调用 `makeBuffer()` |
| 能否取地址 | `&x` 可以 | `&10`、`&(x + 1)` 不可以 |

右值的特点正是我们要找的：**它是临时的，没有别人会再用它，所以可以放心地把它的资源拿走。**

## 3. 右值引用

C++98 只有一种引用 `T&`，C++11 把它叫做**左值引用**，并新增了**右值引用** `T&&`，**专门用来绑定右值**。

```cpp
#include <string>
std::string make() { return "tmp"; }
int main()
{
    int x = 10;
    int& r1 = x;              // OK：左值引用绑定左值
    int& r2 = 10;             // 错误：左值引用不能绑定右值
    const int& r3 = 10;       // OK：const 左值引用可以绑定右值
    int&& r4 = 10;            // OK：右值引用绑定右值
    int&& r5 = x;             // 错误：右值引用不能绑定左值
    std::string&& r6 = make();// OK：函数返回的临时对象是右值
    return r1 + r2 + r3 + r4 + r5 + (int)r6.size();
}
```

Clang 的报错：

```
error: non-const lvalue reference to type 'int' cannot bind to a temporary of type 'int'
error: rvalue reference to type 'int' cannot bind to lvalue of type 'int'
```

整理成表格：

| | 绑定左值 | 绑定右值 |
| --- | --- | --- |
| `T&` | ✓ | ✗ |
| `const T&` | ✓ | ✓ |
| `T&&` | ✗ | ✓ |

有了这个区别，就可以**重载**：同一个函数写两个版本，参数分别是 `const T&` 和 `T&&`。传入左值时调用前者（老老实实拷贝），传入右值时调用后者（直接把资源拿走）。编译器会根据实参是左值还是右值自动选择，这就是移动语义的核心机制。

## 4. 移动构造函数与移动赋值运算符

下面是完整的 `Buffer` 类，每个函数被调用时都会打印一行，方便观察：

```cpp
#include <cstring>
#include <iostream>
#include <utility>

class Buffer {
public:
    explicit Buffer(std::size_t size) : size_(size), data_(new int[size]())
    {
        std::cout << "  构造      size=" << size_ << '\n';
    }

    ~Buffer() { delete[] data_; }

    // 拷贝构造：深拷贝，分配新内存并逐个复制
    Buffer(const Buffer& other) : size_(other.size_), data_(new int[other.size_])
    {
        std::memcpy(data_, other.data_, size_ * sizeof(int));
        std::cout << "  拷贝构造  复制了 " << size_ << " 个元素\n";
    }

    // 移动构造：直接“偷走”对方的内存，再把对方置空
    Buffer(Buffer&& other) noexcept : size_(other.size_), data_(other.data_)
    {
        other.size_ = 0;
        other.data_ = nullptr;
        std::cout << "  移动构造  只复制了一个指针\n";
    }

    // 拷贝赋值
    Buffer& operator=(const Buffer& other)
    {
        std::cout << "  拷贝赋值\n";
        if (this != &other) {
            int* fresh = new int[other.size_];
            std::memcpy(fresh, other.data_, other.size_ * sizeof(int));
            delete[] data_;
            data_ = fresh;
            size_ = other.size_;
        }
        return *this;
    }

    // 移动赋值
    Buffer& operator=(Buffer&& other) noexcept
    {
        std::cout << "  移动赋值\n";
        if (this != &other) {
            delete[] data_;            // 先释放自己的资源
            data_ = other.data_;
            size_ = other.size_;
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    std::size_t size() const { return size_; }

private:
    std::size_t size_;
    int* data_;
};

Buffer makeBuffer(std::size_t n)
{
    return Buffer(n);
}

int main()
{
    std::cout << "1. Buffer a(1000000)\n";
    Buffer a(1000000);

    std::cout << "2. Buffer b = a          // a 是左值\n";
    Buffer b = a;

    std::cout << "3. Buffer c = std::move(a)\n";
    Buffer c = std::move(a);
    std::cout << "  移动后 a.size() = " << a.size() << ", c.size() = " << c.size() << '\n';

    std::cout << "4. b = makeBuffer(10)    // 右边是临时对象\n";
    b = makeBuffer(10);

    std::cout << "5. c = b\n";
    c = b;
    return 0;
}
```

运行结果：

```
1. Buffer a(1000000)
  构造      size=1000000
2. Buffer b = a          // a 是左值
  拷贝构造  复制了 1000000 个元素
3. Buffer c = std::move(a)
  移动构造  只复制了一个指针
  移动后 a.size() = 0, c.size() = 1000000
4. b = makeBuffer(10)    // 右边是临时对象
  构造      size=10
  移动赋值
5. c = b
  拷贝赋值
```

逐条分析：

- **第 2 步**：`a` 是左值，匹配 `const Buffer&`，调用拷贝构造，复制了一百万个元素。
- **第 3 步**：`std::move(a)` 把 `a` 变成右值（下一节详细解释），匹配 `Buffer&&`，调用移动构造。移动后 `a` 变成了空的，内存被 `c` 接管了。
- **第 4 步**：`makeBuffer(10)` 的返回值是临时对象，是右值，**自动**调用移动赋值，不需要写 `std::move`。
- **第 5 步**：`b` 是左值，调用拷贝赋值。

### 4.1 编写移动操作的要点

1. **参数是非 const 的右值引用 `T&&`**，因为要修改对方（把它的指针置空）。
2. **把对方留在「有效」状态**。移动后对方仍然会被析构，所以必须把 `other.data_` 置为 `nullptr`，否则两个对象会 `delete[]` 同一块内存（double free）。
3. **移动赋值要先释放自己原有的资源**，否则会内存泄漏。还要处理自赋值 `a = std::move(a)`。
4. **加上 `noexcept`**，原因见第 6 节。

## 5. std::move 其实什么都不移动

`std::move` 这个名字非常有误导性。它**不移动任何东西**，只做一件事：**把参数无条件地转换成右值引用**。它的实现大致是：

```cpp
template <typename T>
typename std::remove_reference<T>::type&& move(T&& t) noexcept
{
    return static_cast<typename std::remove_reference<T>::type&&>(t);
}
```

就是一个 `static_cast`。真正的「移动」发生在**移动构造函数或移动赋值运算符**里。`std::move(a)` 的意思是：「我承诺以后不再使用 `a` 的值了，你可以把它的资源拿走。」

所以使用 `std::move` 后要注意：

- **被移动的对象处于「有效但未指定」的状态**。可以对它重新赋值、可以析构，但不要依赖它的值。标准库容器被移动后，最好当作内容未知。
- **对 `const` 对象使用 `std::move` 不会移动**。

```cpp
#include <iostream>
#include <string>
#include <utility>
#include <vector>

int main()
{
    const std::string s = "一个很长很长的字符串，长到不能使用短字符串优化";
    std::string t = std::move(s);       // s 是 const，无法移动，实际调用的是拷贝构造
    std::cout << "s 仍然有内容: " << !s.empty() << '\n';

    std::vector<int> v1{1, 2, 3};
    std::vector<int> v2 = std::move(v1);
    // v1 处于“有效但未指定”的状态，可以重新赋值后继续使用
    v1 = {7, 8};
    std::cout << "v1.size() = " << v1.size() << ", v2.size() = " << v2.size() << '\n';
    return 0;
}
```

运行结果：

```
s 仍然有内容: 1
v1.size() = 2, v2.size() = 3
```

`std::move(s)` 的类型是 `const std::string&&`，它不能绑定到移动构造函数的参数 `std::string&&` 上（会丢掉 const），却能绑定到拷贝构造函数的参数 `const std::string&` 上，于是悄悄地变成了拷贝，**编译器不会给任何提示**。

## 6. noexcept：一个关键字带来上千次拷贝的差别

`std::vector` 的元素是连续存储的。容量不够时，它会申请一块更大的内存，把旧元素搬过去。「搬」的时候用拷贝还是移动？

看一个实验：两个类型几乎一模一样，唯一的区别是移动构造函数有没有 `noexcept`。各往 `vector` 里插入 1000 个元素，统计拷贝和移动的次数：

```cpp
#include <iostream>
#include <vector>

static int copies = 0, moves = 0;

struct Safe {
    Safe() = default;
    Safe(const Safe&) { ++copies; }
    Safe(Safe&&) noexcept { ++moves; }       // 声明 noexcept
};

struct Unsafe {
    Unsafe() = default;
    Unsafe(const Unsafe&) { ++copies; }
    Unsafe(Unsafe&&) { ++moves; }            // 没有 noexcept
};

template <typename T>
void test(const char* name)
{
    copies = moves = 0;
    std::vector<T> v;
    for (int i = 0; i < 1000; ++i)
        v.push_back(T());                    // 插入临时对象，会触发多次扩容
    std::cout << name << ": 拷贝 " << copies << " 次, 移动 " << moves << " 次\n";
}

int main()
{
    test<Safe>("有 noexcept  ");
    test<Unsafe>("没有 noexcept");
    return 0;
}
```

运行结果（Clang + libc++）：

```
有 noexcept  : 拷贝 0 次, 移动 2023 次
没有 noexcept: 拷贝 1023 次, 移动 1000 次
```

两种情况下，`push_back(T())` 插入临时对象本身都是移动，共 1000 次。区别在于**扩容时搬运旧元素**：

- 有 `noexcept`：用移动搬运，额外 1023 次移动。
- 没有 `noexcept`：**用拷贝搬运**，额外 1023 次拷贝。

为什么？因为 `push_back` 提供**强异常安全保证**：如果插入失败，vector 必须保持原样。用拷贝搬运时，万一中途抛出异常，旧内存里的元素还完好无损，丢掉新内存就能恢复原状；而用移动搬运时，旧元素已经被「掏空」了，一旦中途出错就无法恢复。所以只有当移动构造函数**承诺不抛异常**（`noexcept`）时，vector 才敢用移动。

> 1023 这个数字取决于 vector 的扩容策略。libc++ 每次扩容为原来的 2 倍，容量依次是 1、2、4、……、512、1024，扩容时需要搬运 1 + 2 + 4 + … + 512 = 1023 个元素。GCC 的 libstdc++ 也是 2 倍，MSVC 是 1.5 倍，次数会有所不同，但结论相同。

**规则：移动构造函数和移动赋值运算符，只要不会抛异常（几乎总是如此），就一定要加 `noexcept`。**

## 7. 什么时候不该写 std::move

### 7.1 不要 return std::move(局部变量)

```cpp
#include <iostream>
#include <utility>

struct Tracer {
    Tracer() { std::cout << "  构造\n"; }
    Tracer(const Tracer&) { std::cout << "  拷贝\n"; }
    Tracer(Tracer&&) noexcept { std::cout << "  移动\n"; }
};

Tracer good()
{
    Tracer t;
    return t;               // NRVO：直接在调用方的位置构造，没有拷贝也没有移动
}

Tracer bad()
{
    Tracer t;
    return std::move(t);    // 画蛇添足：阻止了 NRVO，反而多一次移动
}

int main()
{
    std::cout << "good():\n";
    Tracer a = good();
    std::cout << "bad():\n";
    Tracer b = bad();
    (void)a; (void)b;
    return 0;
}
```

编译时 Clang 就会警告：

```
warning: moving a local object in a return statement prevents copy elision [-Wpessimizing-move]
note: remove std::move call here
```

运行结果：

```
good():
  构造
bad():
  构造
  移动
```

`good()` 里只有一次构造，连移动都没有。这是编译器的**返回值优化**（RVO / NRVO）：直接在调用方 `a` 的内存位置上构造 `t`，根本不需要拷贝或移动。`return std::move(t);` 的返回表达式不再是局部变量名，编译器无法做这个优化，反而多了一次移动。

而且，即使编译器因为某些原因没能做 NRVO，`return 局部变量;` 也会**自动**把局部变量当作右值，优先调用移动构造。所以**返回局部变量时，直接写 `return t;` 就是最优的**。

### 7.2 不要移动之后继续使用

```cpp
std::string name = "Alice";
names.push_back(std::move(name));
std::cout << name;     // 逻辑错误：name 的值是未指定的
```

### 7.3 什么时候需要 std::move

当你有一个**左值**，并且**确定以后不再需要它的值**时：

```cpp
std::vector<std::string> names;
std::string line;
while (std::getline(std::cin, line))
    names.push_back(std::move(line));   // line 下一轮会被重新赋值，可以移动

class Person {
public:
    Person(std::string name) : name_(std::move(name)) {}   // 参数是副本，移进成员
private:
    std::string name_;
};
```

## 8. 进阶

### 8.1 三五法则与零法则

如果一个类需要自定义下面五个函数中的任何一个，通常五个都需要自定义：

1. 析构函数
2. 拷贝构造函数
3. 拷贝赋值运算符
4. 移动构造函数
5. 移动赋值运算符

这叫**三五法则**（rule of three/five，C++98 时是前三个）。原因很简单：需要自定义析构函数，说明类直接管理着某种资源（内存、文件、锁），那么拷贝和移动也必须正确处理这个资源。

编译器自动生成移动操作的规则：**只有当类没有自定义拷贝构造、拷贝赋值、移动操作和析构函数时，编译器才会自动生成移动构造和移动赋值**。如果你只写了析构函数，编译器就不会生成移动操作，所有「移动」都会退化成拷贝，而且不会有任何提示：

```cpp
struct T {
    T() {}
    T(const T&) { std::cout << "拷贝\n"; }
    T(T&&) noexcept { std::cout << "移动\n"; }
};
struct WithDtor { T t; ~WithDtor() {} };   // 只多写了一个空的析构函数
struct NoDtor   { T t; };

WithDtor a; WithDtor b = std::move(a);     // 输出：拷贝
NoDtor   c; NoDtor   d = std::move(c);     // 输出：移动
```

`WithDtor` 只是多了一个什么都不做的析构函数，`std::move` 就变成了拷贝。

更好的做法是**零法则**（rule of zero）：**不要让普通的类直接管理资源**，而是用标准库组件（`std::string`、`std::vector`、`std::unique_ptr`）作为成员，它们已经正确实现了五个函数。这样你的类一个都不用写，编译器生成的版本就是正确的：

```cpp
class Person {             // 零法则：什么都不用写，拷贝和移动都是正确且高效的
    std::string name_;
    std::vector<std::string> tags_;
};
```

上面的 `Buffer` 只是为了演示原理。实际项目中，`int* data_` 应该换成 `std::vector<int>` 或 `std::unique_ptr<int[]>`。

### 8.2 值类别：lvalue、xvalue、prvalue

本文把表达式简单分成了左值和右值。C++11 标准实际上把表达式分成三类：

```
              表达式
            /       \
       glvalue      rvalue
       /     \      /     \
   lvalue    xvalue     prvalue
```

| 类别 | 含义 | 例子 |
| --- | --- | --- |
| lvalue（左值） | 有身份，不能被移动 | 变量名 `x`、`*p`、`arr[0]` |
| xvalue（将亡值） | 有身份，**可以被移动** | `std::move(x)`、返回 `T&&` 的函数调用 |
| prvalue（纯右值） | 没有身份的临时值 | `10`、`x + 1`、返回 `T` 的函数调用 |

- **glvalue**（泛左值）= lvalue + xvalue，「有身份」，即对应一个具体的对象。
- **rvalue**（右值）= xvalue + prvalue，「可以被移动」。

`std::move(x)` 的结果就是一个 **xvalue**：它仍然指向 `x` 这个具体的对象，但被标记成了「可以移动」。

### 8.3 右值引用变量本身是左值

这是最容易混淆的一点：

```cpp
void process(Buffer&& b)
{
    Buffer other = b;              // 调用的是拷贝构造！
    Buffer another = std::move(b); // 这才是移动构造
}
```

`b` 的**类型**是右值引用，但 `b` 是一个**有名字的变量**，所以表达式 `b` 是左值。规则就是那句话：**有名字的就是左值**。在函数内部想继续移动它，需要再写一次 `std::move`。

这个现象引出了下一篇文章的主题：在模板中如何把参数**原样**（保持左值或右值的属性）传递给另一个函数，也就是**完美转发**。

### 8.4 C++17 的保证复制消除

第 7.1 节中的 RVO 在 C++11/14 里只是**允许**编译器做的优化。C++17 把其中一种情况变成了**强制要求**：用纯右值初始化同类型对象时，一定不会调用拷贝或移动构造函数，比如 `return Buffer(n);` 和 `Buffer b = Buffer(10);`。因此在 C++17 中，即使类型的拷贝和移动构造函数都被删除了，也可以从函数中按值返回它。具名对象的 NRVO（`return t;`）仍然只是允许，但主流编译器在开启优化后基本都会做。

## 9. 小结

- **右值**是临时的、马上消失的值，它的资源可以被安全地拿走
- **右值引用 `T&&`** 只能绑定右值，用来重载出「移动版本」的函数
- **移动构造/移动赋值**：把对方的资源拿过来，再把对方置于有效的空状态
- **`std::move` 只是类型转换**，真正的移动在移动构造/移动赋值里完成；对 const 对象使用会退化为拷贝
- 移动操作要加 **`noexcept`**，否则 `vector` 扩容时会用拷贝
- **返回局部变量时不要写 `std::move`**，会阻止返回值优化
- 优先使用**零法则**：用标准库组件管理资源，不必自己写五个特殊函数

## 10. 练习

1. 下面每一行调用的是拷贝构造还是移动构造？

   ```cpp
   std::string a = "hello";
   std::string b = a;
   std::string c = std::move(a);
   std::string d = a + b;
   std::string e = std::string("hi");
   const std::string f = "x";
   std::string g = std::move(f);
   ```

2. 下面的类有什么问题？

   ```cpp
   class Image {
   public:
       Image(int w, int h) : pixels_(new int[w * h]) {}
       ~Image() { delete[] pixels_; }
   private:
       int* pixels_;
   };

   Image a(100, 100);
   Image b = a;
   ```

3. 把第 2 题的 `Image` 按「零法则」改写，使它能被正确地拷贝和移动。

<details>
<summary>点击查看答案</summary>

1. `b`：拷贝（`a` 是左值）；`c`：移动；`d`：`a + b` 返回临时对象（纯右值），移动或直接构造（C++17 起保证直接构造）；`e`：同上；`g`：拷贝，`f` 是 const，`std::move(f)` 是 `const std::string&&`，只能匹配拷贝构造。
2. 类有析构函数却没有自定义拷贝构造，编译器生成的拷贝构造只复制指针（浅拷贝）。`b` 和 `a` 指向同一块内存，两个对象析构时都会 `delete[]` 它，造成 double free。用 `-fsanitize=address` 编译运行会看到 `ERROR: AddressSanitizer: attempting double-free`。由于自定义了析构函数，编译器也不会生成移动操作。
3. 用 `std::vector<int>` 管理像素，删掉析构函数：

   ```cpp
   class Image {
   public:
       Image(int w, int h) : pixels_(w * h) {}
   private:
       std::vector<int> pixels_;
   };
   ```

   拷贝时 `vector` 会深拷贝，移动时 `vector` 会移动，析构时 `vector` 会释放内存，全部正确。

</details>
