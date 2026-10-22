---
title: 范围 for 循环
date: 2026-10-22 16:00:00
updated: 2026-10-22 16:00:00
categories:
  - C/C++
  - C++11
tags:
  - C++11
  - 循环
order: 4
description: "用 for (auto& x : container) 遍历数组和容器。讲清楚按值、引用、const 引用三种写法的区别，范围 for 的展开原理，以及如何让自己的类支持范围 for。"
disableNunjucks: true
---

> **本文目标**：会用范围 for 遍历数组、容器和初始化列表；知道什么时候写 `auto`、`auto&`、`const auto&`；理解范围 for 背后的展开代码；能让自定义类型支持范围 for；避开遍历时修改容器的坑。
>
> **编译环境**：`g++ -std=c++11` / `clang++ -std=c++11`。

## 1. 基本用法

遍历容器是最常见的操作之一。C++98 只能用下标或者迭代器：

```cpp
for (std::size_t i = 0; i < v.size(); ++i) std::cout << v[i];
for (std::vector<int>::iterator it = v.begin(); it != v.end(); ++it) std::cout << *it;
```

C++11 的**范围 for**（range-based for）让遍历变得非常简洁：

```cpp
for (元素声明 : 要遍历的范围) {
    循环体
}
```

完整示例：

```cpp
#include <iostream>
#include <map>
#include <string>
#include <vector>

int main()
{
    int arr[] = {1, 2, 3, 4, 5};
    for (int x : arr)                       // 数组
        std::cout << x << ' ';
    std::cout << '\n';

    std::vector<std::string> names{"Alice", "Bob", "Carol"};
    for (const auto& name : names)          // 只读，不拷贝
        std::cout << name << ' ';
    std::cout << '\n';

    for (auto& name : names)                // 修改元素需要引用
        name += "!";
    for (const auto& name : names)
        std::cout << name << ' ';
    std::cout << '\n';

    for (auto x : arr)                      // 按值：修改的是副本
        x *= 10;
    std::cout << "arr[0] 仍然是 " << arr[0] << '\n';

    std::map<std::string, int> ages{{"Alice", 20}, {"Bob", 22}};
    for (const auto& kv : ages)             // map 的元素是 pair<const string, int>
        std::cout << kv.first << " -> " << kv.second << '\n';

    for (int n : {2, 3, 5, 7})              // 初始化列表
        std::cout << n << ' ';
    std::cout << '\n';
    return 0;
}
```

运行结果：

```
1 2 3 4 5 
Alice Bob Carol 
Alice! Bob! Carol! 
arr[0] 仍然是 1
Alice -> 20
Bob -> 22
2 3 5 7 
```

编译时 Clang 还会给出一个警告：

```
warning: variable 'x' set but not used [-Wunused-but-set-variable]
    for (auto x : arr)                      // 按值：修改的是副本
```

编译器发现你修改了 `x`，却从来没有用过修改后的值，这说明代码很可能写错了。这正是下一节要讲的内容。

## 2. 三种写法怎么选

| 写法 | 含义 | 适用场景 |
| --- | --- | --- |
| `for (auto x : c)` | 每个元素**拷贝**一份 | 元素是 `int`、`double`、指针等小类型，且不需要修改 |
| `for (auto& x : c)` | 元素的**引用** | 需要**修改**元素 |
| `for (const auto& x : c)` | 元素的**只读引用** | 元素是 `string`、`vector`、自定义类等大对象，只读 |

**默认就写 `const auto&`**，需要修改时改成 `auto&`，元素是基本类型时用 `auto` 也可以。最需要避免的是对大对象写 `auto`，比如 `for (auto s : names)`，每次循环都会拷贝一个 `string`，既浪费性能，修改它也不会影响原容器。

遍历 `map` 时，元素类型是 `std::pair<const Key, Value>`。注意 **key 是 const 的**，不能通过 `kv.first = ...` 修改，只能修改 `kv.second`。

## 3. 范围 for 是怎么实现的

范围 for 只是一种**语法糖**，编译器会把它展开成普通的迭代器循环。

```cpp
for (元素声明 : 范围表达式) 循环体
```

大致等价于：

```cpp
{
    auto&& __range = 范围表达式;
    for (auto __begin = begin(__range), __end = end(__range);
         __begin != __end; ++__begin) {
        元素声明 = *__begin;
        循环体
    }
}
```

其中 `begin` 和 `end` 的查找规则是：

1. 如果是**数组**，`begin` 是数组首地址，`end` 是首地址加元素个数（所以范围 for 能遍历数组，但**不能遍历指针**，指针丢失了长度信息）。
2. 如果类有成员函数 `begin()` 和 `end()`，就调用它们。
3. 否则，查找非成员函数 `begin(x)` 和 `end(x)`。

从展开代码可以看出，范围 for 只需要「迭代器」支持三种操作：`!=` 比较、`*` 解引用、前置 `++`。

## 4. 让自己的类型支持范围 for

明白了原理，就能让任何类型支持范围 for。下面实现一个类似 Python 中 `range(1, 6)` 的类：

```cpp
#include <iostream>

// 让自定义类型支持范围 for：提供 begin() / end()，返回的“迭代器”支持 != 、* 、++
class Range {
public:
    class Iterator {
    public:
        explicit Iterator(int v) : value_(v) {}
        int operator*() const { return value_; }
        Iterator& operator++() { ++value_; return *this; }
        bool operator!=(const Iterator& other) const { return value_ != other.value_; }
    private:
        int value_;
    };

    Range(int first, int last) : first_(first), last_(last) {}
    Iterator begin() const { return Iterator(first_); }
    Iterator end() const { return Iterator(last_); }
private:
    int first_, last_;
};

int main()
{
    for (int i : Range(1, 6))
        std::cout << i << ' ';
    std::cout << '\n';

    // 编译器实际生成的代码（简化）
    {
        auto&& range = Range(1, 6);
        for (auto it = range.begin(), end = range.end(); it != end; ++it) {
            int i = *it;
            std::cout << i << ' ';
        }
        std::cout << '\n';
    }
    return 0;
}
```

运行结果：

```
1 2 3 4 5 
1 2 3 4 5 
```

两种写法的输出完全一样，因为第一种会被编译器展开成第二种。注意这里的「迭代器」并不对应任何真实的容器，它只是一个会自增的整数，这说明范围 for 的适用范围远不止容器。

## 5. 常见错误

### 5.1 遍历时修改容器的大小

```cpp
#include <iostream>
#include <vector>

int main()
{
    std::vector<int> v{1, 2, 3};
    std::cout << "capacity 之前: " << v.capacity() << '\n';
    for (int x : v) {
        if (x == 2)
            v.push_back(4);   // 扩容后旧的迭代器全部失效，属于未定义行为
        std::cout << x << ' ';
    }
    std::cout << "\ncapacity 之后: " << v.capacity() << '\n';
    return 0;
}
```

这段代码看起来没问题，实际上是**未定义行为**。`v` 的容量是 3，`push_back` 第 4 个元素时，vector 会申请一块更大的内存，把元素搬过去，再释放旧内存。而范围 for 展开后保存的 `__begin`、`__end` 仍然指向旧内存。

用 AddressSanitizer（`-fsanitize=address`）编译运行，可以直接看到问题：

```
capacity 之前: 3
1 2
==22050==ERROR: AddressSanitizer: heap-use-after-free on address 0x602000000958
READ of size 4 at 0x602000000958 thread T0
```

`heap-use-after-free` 的意思是**读取了已经释放的堆内存**。不开 AddressSanitizer 时，程序可能输出看似正常的结果，也可能输出乱码或崩溃，这正是未定义行为最危险的地方。

> **AddressSanitizer** 是 GCC 和 Clang 自带的内存错误检测工具，编译时加上 `-fsanitize=address -g` 即可。调试内存问题时非常有用，强烈建议学习 C/C++ 时经常开着它。

**规则：在范围 for 里，不要增删容器的元素。** 需要删除元素时，改用迭代器循环，利用 `erase` 的返回值：

```cpp
#include <iostream>
#include <vector>

int main()
{
    std::vector<int> v{10, 20, 30, 40};

    // 需要下标：用普通 for，注意 size() 是无符号的 size_t
    for (std::size_t i = 0; i < v.size(); ++i)
        std::cout << i << ':' << v[i] << ' ';
    std::cout << '\n';

    // 或者自己维护一个计数器
    std::size_t idx = 0;
    for (int x : v)
        std::cout << idx++ << ':' << x << ' ';
    std::cout << '\n';

    // 遍历时删除元素：用迭代器，erase 返回下一个有效位置
    for (auto it = v.begin(); it != v.end(); ) {
        if (*it % 20 == 0) it = v.erase(it);
        else ++it;
    }
    for (int x : v) std::cout << x << ' ';
    std::cout << '\n';
    return 0;
}
```

运行结果：

```
0:10 1:20 2:30 3:40 
0:10 1:20 2:30 3:40 
10 30 
```

### 5.2 需要下标的时候

范围 for 不提供下标。需要下标时，用传统 for 循环，或者像上面那样自己维护计数器。

### 5.3 vector&lt;bool&gt; 和 auto&

```cpp
std::vector<bool> flags{true, false};
for (auto& f : flags) { }   // 编译错误
```

`vector<bool>` 的 `*it` 返回的是一个临时的代理对象，不能绑定到 `auto&`。这里要写 `auto`（代理对象，修改它会影响容器）或 `auto&&`。

## 6. 进阶

### 6.1 为什么展开代码里是 auto&&

展开代码第一行是 `auto&& __range = 范围表达式;`。`auto&&` 是**万能引用**，既能绑定左值（比如一个已有的容器变量），也能绑定右值（比如函数返回的临时容器），而且会**延长临时对象的生命周期**直到循环结束。所以下面的代码是安全的：

```cpp
std::vector<int> makeVector();
for (int x : makeVector()) { }   // 临时 vector 活到循环结束
```

万能引用会在《万能引用与完美转发》中详细讲解。

### 6.2 临时对象的陷阱

生命周期延长**只对范围表达式的最终结果有效**。如果范围表达式是「临时对象的成员」，就会出问题：

```cpp
struct Data {
    std::vector<int> items;
    const std::vector<int>& getItems() const { return items; }
};
Data makeData();

for (int x : makeData().getItems()) { }   // 悬空引用！
```

`__range` 绑定的是 `getItems()` 返回的引用，而 `makeData()` 返回的临时 `Data` 在这一行结束时就被销毁了，`items` 也随之销毁。这个问题在 C++23 中才被修复（范围表达式中所有临时对象的生命周期都会延长到循环结束）。在此之前，先用一个变量保存：

```cpp
Data d = makeData();
for (int x : d.getItems()) { }
```

### 6.3 C++17 和 C++20 的改进

- **C++17**：`__begin` 和 `__end` 的类型可以不同。这让「哨兵」成为可能：`end` 不必是真正的迭代器，只要能和迭代器比较即可，C++20 的 Ranges 库大量使用了这一点。
- **C++17**：配合结构化绑定，遍历 map 可以写成 `for (const auto& [key, value] : ages)`，不用再写 `kv.first`、`kv.second`。
- **C++20**：范围 for 可以带初始化语句：`for (auto v = getData(); auto x : v)`。

## 7. 小结

- 范围 for：`for (声明 : 范围)`，能遍历数组、容器、初始化列表，以及任何提供 `begin()`/`end()` 的类型
- 默认写 `const auto&`，要修改元素写 `auto&`，小类型可以写 `auto`
- 范围 for 会被展开成迭代器循环，迭代器只需要支持 `!=`、`*`、`++`
- **循环中不要增删元素**，否则迭代器失效，导致未定义行为
- 范围表达式是「临时对象的成员」时，C++23 之前会产生悬空引用

## 8. 练习

1. 下面的代码想把所有偶数乘以 2，哪里有问题？

   ```cpp
   std::vector<int> v{1, 2, 3, 4};
   for (auto x : v)
       if (x % 2 == 0) x *= 2;
   ```

2. 给第 4 节的 `Range` 类增加步长参数，使 `for (int i : Range(0, 10, 3))` 输出 `0 3 6 9`。提示：`operator!=` 的判断方式需要修改。

<details>
<summary>点击查看答案</summary>

1. `auto x` 是元素的拷贝，修改不会影响 `v`。改成 `for (auto& x : v)`。
2. 步长不为 1 时，`value_` 可能跳过 `last`，用 `!=` 判断会死循环。可以把 `operator!=` 改成「当前值小于结束值」：

   ```cpp
   class Range {
   public:
       class Iterator {
       public:
           Iterator(int v, int step) : value_(v), step_(step) {}
           int operator*() const { return value_; }
           Iterator& operator++() { value_ += step_; return *this; }
           bool operator!=(const Iterator& end) const { return value_ < end.value_; }
       private:
           int value_, step_;
       };
       Range(int first, int last, int step = 1) : first_(first), last_(last), step_(step) {}
       Iterator begin() const { return Iterator(first_, step_); }
       Iterator end() const { return Iterator(last_, step_); }
   private:
       int first_, last_, step_;
   };
   ```

</details>
