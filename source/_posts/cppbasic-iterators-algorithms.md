---
title: 迭代器与算法
date: 2026-10-23 13:00:00
updated: 2026-10-23 13:00:00
categories:
  - C/C++
  - C++基础
tags:
  - C++基础
  - STL
  - 算法
order: 13
description: 迭代器把容器和算法连接起来，让一个 sort 可以排序任何容器。本文介绍迭代器的用法和分类，用 AddressSanitizer 复现迭代器失效，讲清楚边遍历边删除的正确写法和 erase-remove 惯用法，并介绍 sort、find_if、count_if、accumulate、transform、lower_bound 等常用算法。
disableNunjucks: true
---

> **本文目标**：会用迭代器遍历和修改容器；理解迭代器的分类，知道为什么 `std::sort` 不能用于 `list`；能识别和避免迭代器失效；掌握边遍历边删除的正确写法和 erase-remove 惯用法；熟练使用常用的 STL 算法。
>
> **前置知识**：《STL 容器》，C++11 章节的《Lambda 表达式》（会写简单的 lambda 即可）。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。

## 1. 迭代器：统一的访问方式

上一篇介绍了很多种容器，它们的内部结构完全不同：`vector` 是数组，`list` 是链表，`map` 是红黑树。但遍历它们的写法却是一样的，靠的就是**迭代器**（iterator）。

迭代器是一个「**指向容器中某个元素**」的对象，用法和指针几乎一样：

| 操作 | 含义 |
|---|---|
| `c.begin()` | 指向第一个元素 |
| `c.end()` | 指向**最后一个元素的后面**（不能解引用） |
| `*it` | 访问指向的元素 |
| `it->member` | 访问元素的成员 |
| `++it` | 移动到下一个元素 |
| `it1 == it2` / `it1 != it2` | 是否指向同一个位置 |

```cpp
#include <iostream>
#include <list>
#include <map>
#include <string>
#include <vector>

int main()
{
    std::vector<int> v{10, 20, 30, 40};
    std::cout << "正向:";
    for (auto it = v.begin(); it != v.end(); ++it) {
        *it += 1;                                   // 通过迭代器修改元素
        std::cout << ' ' << *it;
    }
    std::cout << "\n反向:";
    for (auto it = v.rbegin(); it != v.rend(); ++it) {
        std::cout << ' ' << *it;
    }
    std::cout << "\nvector 迭代器可以跳跃: *(v.begin() + 2) = " << *(v.begin() + 2)
              << "，end - begin = " << (v.end() - v.begin()) << '\n';

    std::list<std::string> names{"Tom", "Jerry", "Spike"};
    std::cout << "list:";
    for (auto it = names.cbegin(); it != names.cend(); ++it) {   // 只读迭代器
        std::cout << ' ' << *it << "(" << it->size() << ")";
    }

    std::map<std::string, int> ages{{"Tom", 3}, {"Jerry", 2}};
    std::cout << "\nmap:";
    for (auto it = ages.begin(); it != ages.end(); ++it) {
        std::cout << ' ' << it->first << '=' << it->second;
    }
    std::cout << '\n';
    return 0;
}
```

运行结果：

```
正向: 11 21 31 41
反向: 41 31 21 11
vector 迭代器可以跳跃: *(v.begin() + 2) = 31，end - begin = 4
list: Tom(3) Jerry(5) Spike(5)
map: Jerry=2 Tom=3
```

- **`[begin, end)` 是一个左闭右开的区间**。`end()` 不指向任何元素，只是一个「结束」的标记。空容器的 `begin() == end()`。
- **`rbegin()` / `rend()`** 是反向迭代器，`++` 时往前走。
- **`cbegin()` / `cend()`** 返回只读迭代器（`const_iterator`），不能通过它修改元素。
- 迭代器的类型名很长（比如 `std::vector<int>::iterator`），所以通常用 `auto`。

C++11 的范围 `for` 循环 `for (int& x : v)` 其实就是用 `begin()` 和 `end()` 实现的，只是编译器帮我们写好了迭代器的代码。

### 1.1 迭代器的分类

不同的容器，迭代器能做的事情不同：

| 类别 | 支持的操作 | 容器 |
|---|---|---|
| 随机访问迭代器 | `++`、`--`、`it + n`、`it2 - it1`、`<` | `vector`、`deque`、`array`、`string` |
| 双向迭代器 | `++`、`--` | `list`、`set`、`map` |
| 前向迭代器 | `++` | `unordered_set`、`unordered_map`、`forward_list` |

`vector` 的元素是连续的，迭代器可以直接跳到第 n 个元素；而链表只能一个一个地走。所以对 `list` 的迭代器做加法会编译失败：

```cpp
std::list<int> l{1, 2, 3};
auto it = l.begin() + 1;
```

```
error: invalid operands to binary expression ('iterator' (aka '__list_iterator<int, void *>') and 'int')
```

需要移动多步时，可以用 `std::next(it, n)` 或 `std::advance(it, n)`，它们对任何迭代器都能用（对于链表是 O(n) 的）。

## 2. 迭代器失效

迭代器指向容器中的元素。如果容器的结构改变了，迭代器可能就**指向了已经不存在的位置**，这叫**迭代器失效**。

### 2.1 vector 扩容

```cpp
#include <iostream>
#include <vector>

int main()
{
    std::vector<int> v{1, 2, 3};
    int& first = v[0];                  // 引用第一个元素
    auto it = v.begin();
    std::cout << "capacity = " << v.capacity() << '\n';
    v.push_back(4);                     // 容量不够，扩容：元素搬到了新内存
    std::cout << "capacity = " << v.capacity() << '\n';
    std::cout << first << ' ' << *it << '\n';   // 访问已经释放的旧内存
    return 0;
}
```

直接运行：

```
capacity = 3
capacity = 6
2 2
```

第一个元素明明是 1，输出却是 `2 2`。用 AddressSanitizer 检查：

```bash
clang++ -std=c++17 -g -fsanitize=address invalid.cpp -o invalid && ./invalid
```

```
ERROR: AddressSanitizer: heap-use-after-free on address 0x602000000950
READ of size 4 at 0x602000000950 thread T0
    #0 0x000102478bdc in main invalid.cpp:12
freed by thread T0 here:
    ...
    #10 0x000102479268 in std::__1::vector<int>::push_back(int&&) vector.h:466
```

报告说得很清楚：第 12 行读取了一块**已经被 `push_back` 释放的内存**。上一篇讲过，`vector` 扩容时会分配新内存、搬移元素、释放旧内存，所以**扩容之后，之前获取的指针、引用和迭代器全部失效**。没有扩容时，在中间 `insert` 或 `erase` 也会让该位置之后的迭代器失效。

`list`、`map`、`set` 的元素是单独的节点，插入元素不会使其他迭代器失效，删除元素只会使指向被删元素的迭代器失效。

### 2.2 边遍历边删除

删除 `vector` 中所有的偶数。一种常见的错误写法：

```cpp
std::vector<int> v{1, 2, 2, 3, 4, 4, 5};
for (auto it = v.begin(); it != v.end(); ++it) {
    if (*it % 2 == 0) {
        v.erase(it);                // 错误：it 已经失效，还继续 ++it
    }
}
```

```
1 2 3 4 5
```

还剩下两个偶数。`erase(it)` 之后，`it` 已经失效（在 libc++ 的实现中，它恰好指向了被删除元素后面的那个元素），接着 `++it` 又跳过了一个元素，所以两个相邻的偶数只删除了一个。如果最后一个元素是偶数，`++it` 还会越过 `end()`，导致访问越界。这是**未定义行为**，在不同的实现上可能表现得完全不同。

正确的写法是**使用 `erase` 的返回值**，它指向被删除元素的下一个元素：

```cpp
for (auto it = v.begin(); it != v.end(); ) {     // 注意：这里没有 ++it
    if (*it % 2 == 0) {
        it = v.erase(it);           // erase 返回被删除元素的下一个位置
    } else {
        ++it;
    }
}
```

```
删除偶数后: 1 3 5
```

这个写法对所有容器（包括 `list` 和 `map`）都适用。对于 `vector`，第 4 节的 erase-remove 惯用法更高效。C++20 还提供了更简单的 `std::erase_if(v, 条件)`。

## 3. 常用算法

STL 的算法在头文件 **`<algorithm>`** 和 **`<numeric>`** 中，它们都接受一对迭代器 `[first, last)`，表示要处理的范围。**算法不知道容器的存在，它只通过迭代器访问元素**，所以同一个算法可以用于任何容器。

```cpp
#include <algorithm>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

void print(const char* label, const std::vector<int>& v)
{
    std::cout << label;
    for (int x : v) std::cout << ' ' << x;
    std::cout << '\n';
}

int main()
{
    std::vector<int> v{5, 3, 8, 1, 9, 2, 7};
    print("原始:        ", v);

    std::sort(v.begin(), v.end());                                  // 升序排序
    print("sort:        ", v);
    std::sort(v.begin(), v.end(), [](int a, int b) { return a > b; });  // 自定义比较：降序
    print("sort 降序:   ", v);

    auto it = std::find(v.begin(), v.end(), 8);                     // 查找值
    std::cout << "find(8):      位置 " << (it - v.begin()) << '\n';
    auto small = std::find_if(v.begin(), v.end(), [](int x) { return x < 3; });   // 按条件查找
    std::cout << "find_if(<3):  " << *small << '\n';
    std::cout << "count_if(奇数): "
              << std::count_if(v.begin(), v.end(), [](int x) { return x % 2 == 1; }) << '\n';
    std::cout << "accumulate:   " << std::accumulate(v.begin(), v.end(), 0) << '\n';
    std::cout << "max_element:  " << *std::max_element(v.begin(), v.end()) << '\n';

    std::vector<int> squares(v.size());
    std::transform(v.begin(), v.end(), squares.begin(), [](int x) { return x * x; });  // 逐个变换
    print("transform 平方:", squares);

    std::reverse(v.begin(), v.end());
    print("reverse:     ", v);
    return 0;
}
```

运行结果：

```
原始:         5 3 8 1 9 2 7
sort:         1 2 3 5 7 8 9
sort 降序:    9 8 7 5 3 2 1
find(8):      位置 1
find_if(<3):  2
count_if(奇数): 5
accumulate:   35
max_element:  9
transform 平方: 81 64 49 25 9 4 1
reverse:      1 2 3 5 7 8 9
```

| 算法 | 作用 | 复杂度 |
|---|---|---|
| `sort(first, last[, comp])` | 排序，可以传入比较函数 | O(n log n) |
| `stable_sort` | 稳定排序：相等的元素保持原来的相对顺序 | O(n log n) |
| `find(first, last, value)` | 查找值，返回迭代器，找不到时返回 `last` | O(n) |
| `find_if(first, last, pred)` | 查找第一个满足条件的元素 | O(n) |
| `count` / `count_if` | 统计等于某个值 / 满足条件的元素个数 | O(n) |
| `accumulate(first, last, init)` | 求和（`<numeric>`），`init` 是初始值 | O(n) |
| `min_element` / `max_element` | 返回指向最小 / 最大元素的迭代器 | O(n) |
| `transform(first, last, out, f)` | 对每个元素调用 `f`，结果写到 `out` | O(n) |
| `reverse(first, last)` | 反转 | O(n) |

几点说明：

- 「条件」和「比较函数」通常用 **lambda 表达式**传入。比较函数 `comp(a, b)` 的含义是「`a` 是否应该排在 `b` 的前面」。
- `accumulate` 的返回类型由初始值决定。对 `double` 求和时要写 `0.0`，写成 `0` 会把每一步的结果截断为整数。
- `transform` 的目标位置要**有足够的空间**，所以先创建了 `squares(v.size())`。

### 3.1 排序自定义类型

```cpp
struct Student {
    std::string name;
    int score;
};

std::vector<Student> students{{"Alice", 90}, {"Bob", 85}, {"Carol", 90}, {"Dave", 70}};
std::stable_sort(students.begin(), students.end(),
                 [](const Student& a, const Student& b) { return a.score > b.score; });
```

```
Alice 90
Carol 90
Bob 85
Dave 70
```

按分数从高到低排序。Alice 和 Carol 的分数相同，`stable_sort` 保证这两条记录保持原来的顺序；用 `sort` 则不保证。

### 3.2 二分查找

对于**已经排好序**的范围，可以用二分查找，复杂度 O(log n)：

```cpp
std::vector<int> v{1, 3, 5, 7, 9, 11};
std::binary_search(v.begin(), v.end(), 7);        // true
std::binary_search(v.begin(), v.end(), 4);        // false
auto it = std::lower_bound(v.begin(), v.end(), 4);       // 第一个 >= 4 的位置
// 位置 2，值 5
v.insert(it, 4);                                          // 插入后仍然有序：1 3 4 5 7 9 11
```

`binary_search` 只返回是否找到；**`lower_bound`** 返回第一个**不小于**给定值的位置，既可以用来查找，也可以用来找到插入位置以保持有序。对无序的范围使用它们，结果是错误的。

## 4. erase-remove 惯用法

`std::remove_if` 的名字有误导性：它**并不真正删除元素**。

```cpp
#include <algorithm>
#include <iostream>
#include <vector>

void print(const char* label, const std::vector<int>& v)
{
    std::cout << label;
    for (int x : v) std::cout << ' ' << x;
    std::cout << "（size = " << v.size() << "）\n";
}

int main()
{
    std::vector<int> v{1, 2, 2, 3, 4, 4, 5};
    auto newEnd = std::remove_if(v.begin(), v.end(), [](int x) { return x % 2 == 0; });
    print("remove_if 之后:", v);
    std::cout << "newEnd 的位置: " << (newEnd - v.begin()) << '\n';
    v.erase(newEnd, v.end());                                   // 真正删除尾部的元素
    print("erase 之后:    ", v);

    std::vector<int> w{3, 1, 3, 2, 1, 2, 3};
    std::sort(w.begin(), w.end());
    w.erase(std::unique(w.begin(), w.end()), w.end());          // 排序后去重
    print("sort + unique: ", w);
    return 0;
}
```

运行结果：

```
remove_if 之后: 1 3 5 3 4 4 5（size = 7）
newEnd 的位置: 3
erase 之后:     1 3 5（size = 3）
sort + unique:  1 2 3（size = 3）
```

`remove_if` 之后，`size` 仍然是 7。它做的事情是：**把要保留的元素依次移到前面**，然后返回「新的结尾」`newEnd`。`newEnd` 后面的元素是剩下的旧值，内容没有意义。

为什么不直接删除？因为**算法只能通过迭代器访问元素，它无法改变容器的大小**，真正删除元素必须调用容器自己的 `erase`。所以要两步合在一起：

```cpp
v.erase(std::remove_if(v.begin(), v.end(), 条件), v.end());
```

这就是 **erase-remove 惯用法**。它只遍历一次，复杂度 O(n)；而第 2.2 节逐个 `erase` 的写法，每次删除都要移动后面的所有元素，最坏是 O(n²)。`std::unique`（删除**相邻的**重复元素）也是同样的用法，所以去重之前要先排序。

C++20 中可以直接写 `std::erase_if(v, 条件)`，效果相同。

## 5. 算法与容器的匹配

不是所有的算法都能用于所有的容器。`std::sort` 需要随机访问迭代器，用在 `list` 上：

```cpp
std::list<int> l{3, 1, 2};
std::sort(l.begin(), l.end());
```

编译器输出了 **138 行**错误信息，其中有 4 个 `error`，都发生在标准库的内部文件中：

```
.../c++/v1/__algorithm/make_heap.h:37:31: error: invalid operands to binary expression
    ('std::__list_iterator<int, void *>' and 'std::__list_iterator<int, void *>')
...
ex2.cpp:3:46: note: in instantiation of function template specialization
    'std::sort<std::__list_iterator<int, void *>>' requested here
```

按照《模板入门》中的方法，找到指向自己代码的 `requested here`：第 3 行的 `std::sort`。错误的原因是 `sort` 内部要对两个迭代器做减法，而链表的迭代器不支持。`list` 有自己的成员函数 **`l.sort()`**。同样，`map` 和 `set` 本身就是有序的，应该用成员函数 `find`（O(log n)），而不是 `std::find`（O(n)）。

**规则：容器有同名的成员函数时，优先使用成员函数。**

## 6. 常见错误

1. **解引用 `end()`**：`end()` 不指向任何元素。`find` 的结果要先和 `end()` 比较再使用。
2. **扩容后使用旧的迭代器、指针、引用**：堆内存释放后被使用（heap-use-after-free）。
3. **边遍历边删除时继续使用失效的迭代器**：用 `it = c.erase(it)`。
4. **以为 `remove_if` 会删除元素**：容器的大小没有变，要配合 `erase`。
5. **`unique` 之前没有排序**：它只删除相邻的重复元素。
6. **对无序的范围使用 `binary_search` / `lower_bound`**：结果错误。
7. **`transform` 的目标空间不够**：越界写入。先分配好大小，或者使用 `std::back_inserter(out)`。
8. **对 `list` 用 `std::sort`，对 `map` 用 `std::find`**：前者编译错误，后者能用但慢。

## 7. 小结

- 迭代器是指向容器元素的对象，用法像指针；`[begin, end)` 是左闭右开区间
- 迭代器分为随机访问、双向、前向等类别，决定了能对它做哪些操作，以及能用于哪些算法
- 容器的结构改变后迭代器可能失效：`vector` 扩容后所有迭代器失效（ASan 报告 heap-use-after-free）
- 边遍历边删除要用 `it = c.erase(it)`
- 算法通过迭代器处理 `[first, last)` 范围，与具体的容器无关；条件和比较函数用 lambda 传入
- `remove_if` 不改变容器大小，要用 erase-remove 惯用法，一次遍历 O(n)
- 二分查找只能用于有序范围；容器有同名成员函数时优先用成员函数

## 8. 练习

1. 给定 `std::vector<std::string> words{"apple", "fig", "banana", "kiwi", "pear", "plum"}`，用算法统计长度大于 3 的单词个数，然后按长度从短到长排序（长度相同的保持原来的顺序）。
2. 用 erase-remove 惯用法删除 `std::vector<int> v{3, -1, 4, -1, -5, 9}` 中所有的负数。
3. 下面的代码想要找到第一个大于 100 的数并输出它，有什么问题？

   ```cpp
   auto it = std::find_if(v.begin(), v.end(), [](int x) { return x > 100; });
   std::cout << *it << '\n';
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   auto longCount = std::count_if(words.begin(), words.end(),
                                  [](const std::string& w) { return w.size() > 3; });
   std::stable_sort(words.begin(), words.end(),
                    [](const std::string& a, const std::string& b) { return a.size() < b.size(); });
   ```

   输出：

   ```
   长度大于 3 的单词: 5 个
   按长度排序: fig kiwi pear plum apple banana
   ```

   `kiwi`、`pear`、`plum` 长度都是 4，`stable_sort` 保持了它们原来的顺序。

2. 参考代码：

   ```cpp
   v.erase(std::remove_if(v.begin(), v.end(), [](int x) { return x < 0; }), v.end());
   ```

   结果是 `3 4 9`。

3. 如果没有大于 100 的数，`find_if` 返回 `v.end()`，解引用它是未定义行为。应该先检查：

   ```cpp
   if (it != v.end()) {
       std::cout << *it << '\n';
   } else {
       std::cout << "没有找到\n";
   }
   ```

</details>
