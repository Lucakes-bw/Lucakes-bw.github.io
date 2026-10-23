---
title: STL 容器
date: 2026-10-23 12:40:00
updated: 2026-10-23 12:40:00
categories:
  - C/C++
  - C++基础
tags:
  - C++基础
  - STL
  - 容器
order: 12
description: STL 提供了一组现成的数据结构。本文介绍 vector、deque、list、set、map、unordered_map 和三个容器适配器的用法，并用实测数据比较它们的插入、查找和遍历速度，最后给出一张选择容器的速查表。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：熟练使用 `vector`、`map`、`unordered_map`、`set`；了解 `deque`、`list` 和 `stack`、`queue`、`priority_queue`；理解各容器操作的时间复杂度，并通过实测感受差距；能根据需求选择合适的容器。
>
> **前置知识**：《模板入门》。会用范围 `for` 循环（C++11 章节的《范围 for 循环》）。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。性能测试使用 `-O2` 编译，测试机器为 Apple M 系列芯片，数据在不同的机器上会不同，但量级关系是一致的。

## 1. STL 是什么

**STL**（Standard Template Library，标准模板库）是 C++ 标准库中最常用的部分，由三部分组成：

- **容器**（container）：存放数据的数据结构，比如动态数组、链表、树、哈希表。本文的主题。
- **迭代器**（iterator）：访问容器中元素的统一方式。
- **算法**（algorithm）：排序、查找、统计等通用操作，通过迭代器作用于任何容器。

后两部分是下一篇的主题。上一篇讲的模板就是 STL 的基础：`std::vector<int>` 是类模板 `std::vector` 用 `int` 实例化得到的类。

**时间复杂度**是比较容器的标准语言，本文会用到这几种：

| 记号 | 含义 | 元素个数从 1 万变成 10 万时，耗时大约 |
|---|---|---|
| O(1) | 常数时间，与元素个数无关 | 不变 |
| O(log n) | 对数时间，比如二分查找 | 增加一点点 |
| O(n) | 线性时间，比如逐个检查 | 变成 10 倍 |

## 2. vector：首选的容器

`std::vector` 是**动态数组**：元素在内存中连续存放，大小可以自动增长。**如果不知道该用什么容器，就用 `vector`。**

```cpp
#include <vector>

std::vector<int> v;                 // 空的
std::vector<int> a(5);              // 5 个 0
std::vector<int> b(5, 7);           // 5 个 7
std::vector<int> c{1, 2, 3};        // 1, 2, 3

v.push_back(10);                    // 在末尾添加
v.pop_back();                       // 删除末尾元素
c[0] = 100;                         // 下标访问，不检查越界
c.at(1) = 200;                      // 检查越界，越界时抛出 std::out_of_range
c.size();                           // 元素个数
c.empty();                          // 是否为空
c.front(); c.back();                // 第一个、最后一个元素
c.insert(c.begin(), 0);             // 在开头插入（慢，见下文）
c.erase(c.begin());                 // 删除第一个元素（慢）
c.clear();                          // 清空
```

### 2.1 自动增长的秘密：容量

```cpp
#include <iostream>
#include <vector>

int main()
{
    std::vector<int> v;
    std::size_t lastCap = v.capacity();
    std::cout << "size  capacity\n";
    for (int i = 0; i < 100; ++i) {
        v.push_back(i);
        if (v.capacity() != lastCap) {                  // 只在容量变化时输出
            lastCap = v.capacity();
            std::cout << v.size() << "\t" << v.capacity() << '\n';
        }
    }

    std::vector<int> r;
    r.reserve(100);                                     // 预先分配 100 个元素的空间
    std::cout << "reserve(100) 后: size = " << r.size() << ", capacity = " << r.capacity() << '\n';
    return 0;
}
```

运行结果（libc++）：

```
size  capacity
1	1
2	2
3	4
5	8
9	16
17	32
33	64
65	128
reserve(100) 后: size = 0, capacity = 100
```

- **`size()`** 是元素的个数，**`capacity()`** 是已经分配的空间能容纳的元素个数。
- 空间不够时，`vector` 会**分配一块更大的内存（这里是原来的 2 倍），把所有元素搬过去，再释放旧内存**。添加 100 个元素只发生了 8 次扩容。因为每次扩容都翻倍，平均到每次 `push_back` 上的代价是常数，所以说 `push_back` 是**均摊 O(1)** 的。（GCC 的 libstdc++ 也是 2 倍，MSVC 是 1.5 倍。）
- 如果事先知道大约有多少个元素，用 **`reserve(n)`** 预先分配空间，可以避免多次扩容。注意 `reserve` 不改变 `size()`。
- 扩容会搬移元素，所以扩容后，**之前获取的指向元素的指针、引用和迭代器全部失效**。这个陷阱在下一篇会详细讲。

## 3. deque 和 list

### 3.1 实测：在开头插入

`vector` 的元素是连续存放的，在开头插入一个元素，后面的所有元素都要向后移动一位，所以是 **O(n)** 的。另外两种序列容器可以快速地在开头插入：

- **`std::deque`**（双端队列）：分段连续的数组，在**两端**插入和删除都是 O(1)，也支持下标访问。
- **`std::list`**（双向链表）：每个元素是一个单独分配的节点，在**任何位置**插入和删除都是 O(1)（前提是已经有指向那个位置的迭代器），但**不支持下标访问**。

```cpp
template <typename Container>
double insertFront(int n)                       // 在开头插入 n 个元素，返回耗时（毫秒）
{
    auto start = std::chrono::steady_clock::now();
    Container c;
    for (int i = 0; i < n; ++i) c.insert(c.begin(), i);
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

template <typename Container>
double pushBack(int n)                          // 在末尾插入 n 个元素
{
    // 同上，循环体换成 c.push_back(i);
}

// main 中分别用 std::vector<int>、std::deque<int>、std::list<int> 调用
```

运行结果：

```
在开头插入 10000 个元素:
  vector 9.57 ms
  deque  0.08 ms
  list   0.26 ms
在开头插入 100000 个元素:
  vector 293.10 ms
  deque  0.27 ms
  list   0.76 ms
在末尾插入 1000000 个元素:
  vector 1.00 ms
  deque  0.74 ms
  list   7.57 ms
```

- 在开头插入时，`vector` 慢了**上千倍**，而且元素从 1 万增加到 10 万（10 倍）时，耗时增加了 30 倍：每次插入都是 O(n)，n 次插入就是 O(n²)。
- 在末尾插入时，三者都是 O(1)，但 `list` 比 `vector` 慢了 7 倍：每个节点都要单独分配一次内存。

### 3.2 实测：遍历

```cpp
template <typename Container>
void sumAll(const char* name, const Container& c)
{
    auto start = std::chrono::steady_clock::now();
    long long total = std::accumulate(c.begin(), c.end(), 0LL);    // 求和，下一篇介绍
    auto end = std::chrono::steady_clock::now();
    std::cout << "  " << name << std::chrono::duration<double, std::milli>(end - start).count()
              << " ms（和 = " << total << "）\n";
}
```

```
遍历 10000000 个元素求和:
  vector 1.09 ms
  list   21.84 ms
```

两者都是 O(n)，`list` 却慢了 10 到 20 倍。原因是 **CPU 缓存**：`vector` 的元素是连续的，CPU 一次从内存中读取一整块（通常 64 字节，即 16 个 `int`）放进缓存，接下来的元素直接从缓存中读取；而 `list` 的节点分散在内存各处，每访问一个节点都可能要重新从内存读取。**复杂度相同时，连续存储的容器通常快得多**。

所以，**只有确实需要频繁在中间或开头插入删除时，才考虑 `deque` 或 `list`**。在实际项目中，`list` 很少是最好的选择。

## 4. 关联容器：set 和 map

### 4.1 map：键值对

`std::map<Key, Value>` 存放「键 → 值」的映射，**按键自动排序**，键不能重复。下面统计每个单词出现的次数：

```cpp
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>

int main()
{
    std::string text = "the quick brown fox jumps over the lazy dog the fox";
    std::map<std::string, int> counts;              // 按键自动排序
    std::istringstream in(text);
    std::string word;
    while (in >> word) {
        ++counts[word];                             // 不存在时自动插入 0，再加 1
    }
    std::cout << "map（按字母顺序）:\n";
    for (const auto& [w, c] : counts) {
        std::cout << "  " << w << ": " << c << '\n';
    }

    std::cout << "查找:\n";
    if (auto it = counts.find("fox"); it != counts.end()) {
        std::cout << "  fox 出现了 " << it->second << " 次\n";
    }
    std::cout << "  size = " << counts.size() << '\n';
    std::cout << "  counts[\"cat\"] = " << counts["cat"] << '\n';   // 陷阱：插入了 "cat"
    std::cout << "  size = " << counts.size() << "（多了一个 cat）\n";
    std::cout << "  contains(\"cow\")? " << (counts.count("cow") ? "是" : "否")
              << "，size = " << counts.size() << '\n';

    std::unordered_map<std::string, int> u(counts.begin(), counts.end());
    std::cout << "unordered_map 的遍历顺序:";
    for (const auto& [w, c] : u) std::cout << ' ' << w;
    std::cout << '\n';
    return 0;
}
```

运行结果：

```
map（按字母顺序）:
  brown: 1
  dog: 1
  fox: 2
  jumps: 1
  lazy: 1
  over: 1
  quick: 1
  the: 3
查找:
  fox 出现了 2 次
  size = 8
  counts["cat"] = 0
  size = 9（多了一个 cat）
  contains("cow")? 否，size = 9
unordered_map 的遍历顺序: quick over lazy jumps the dog fox cat brown
```

（`std::istringstream` 把字符串当作输入流，用 `>>` 逐个读取单词，见本章最后一篇。）

- **`m[key]`**：如果键不存在，**会自动插入一个值为默认值（`int` 是 0）的元素**，然后返回它的引用。这让 `++counts[word]` 写起来非常方便，但也是一个陷阱：只是想「看一看」`counts["cat"]`，结果却插入了一个元素。
- **只查找、不插入，用 `find` 或 `count`**：`find` 返回一个迭代器，找不到时等于 `end()`；`count` 返回 0 或 1。（C++20 增加了更直观的 `contains`。）`at(key)` 也不会插入，键不存在时抛出 `std::out_of_range`。
- 遍历 `map` 时，每个元素是一个 `std::pair<const Key, Value>`，`first` 是键，`second` 是值。上面用 C++17 的结构化绑定 `auto& [w, c]` 直接拆成两个变量（见 C++17 章节的《结构化绑定》）。
- **`std::unordered_map`** 的用法几乎完全一样，但它是**哈希表**，遍历的顺序是不确定的（看上面最后一行）。

`map` 的底层是**红黑树**（一种自平衡的二叉搜索树），插入、删除、查找都是 **O(log n)**；`unordered_map` 的底层是**哈希表**，平均 **O(1)**。

### 4.2 set：不重复的集合

`std::set` 只有键没有值，可以看作「自动排序、自动去重」的集合。`std::unordered_set` 是它的哈希表版本：

```cpp
std::set<int> s{3, 1, 4, 1, 5};
for (int x : s) std::cout << x << ' ';      // 1 3 4 5：去掉了重复的 1，并且排好了序
s.insert(2);
s.erase(4);
bool has3 = s.count(3) > 0;
```

### 4.3 实测：查找

在 10 万个随机整数中查找 10 万次：

```cpp
std::vector<int> sorted = data;
std::sort(sorted.begin(), sorted.end());
std::set<int> s(data.begin(), data.end());
std::unordered_set<int> us(data.begin(), data.end());

time("vector 顺序查找: ", [&](int k) { return std::find(data.begin(), data.end(), k) != data.end(); });
time("vector 二分查找: ", [&](int k) { return std::binary_search(sorted.begin(), sorted.end(), k); });
time("set:             ", [&](int k) { return s.count(k) > 0; });
time("unordered_set:   ", [&](int k) { return us.count(k) > 0; });
```

```
在 100000 个元素中查找 100000 次:
  vector 顺序查找: 2030.19 ms（找到 9298 个）
  vector 二分查找: 2.34 ms（找到 9298 个）
  set:             7.67 ms（找到 9298 个）
  unordered_set:   1.36 ms（找到 9298 个）
```

- 在无序的 `vector` 中**逐个比较**是 O(n)，花了 2 秒；其他三种方法都在 10 毫秒以内，差距接近 **1000 倍**。这就是 O(n) 和 O(log n) / O(1) 的差别。
- `unordered_set` 最快，O(1)。
- **排好序的 `vector` + 二分查找**也是 O(log n)，而且因为内存连续，比同样是 O(log n) 的 `set` 快了 3 倍。如果数据先一次性准备好，之后只查找不修改，这是一个很好的选择。
- `set` 的优势是**插入和删除后仍然保持有序**，而且可以按顺序遍历、查找某个范围内的元素。

## 5. 容器适配器：stack、queue、priority_queue

这三个类不是独立的容器，而是在其他容器的基础上**限制了接口**，只提供特定的操作：

```cpp
#include <iostream>
#include <queue>
#include <set>
#include <stack>
#include <vector>

int main()
{
    std::stack<int> st;
    std::queue<int> q;
    std::priority_queue<int> pq;                    // 默认最大的在顶部
    for (int x : {3, 1, 4, 1, 5}) {
        st.push(x);
        q.push(x);
        pq.push(x);
    }
    std::cout << "stack:          ";
    while (!st.empty()) { std::cout << st.top() << ' '; st.pop(); }
    std::cout << "\nqueue:          ";
    while (!q.empty()) { std::cout << q.front() << ' '; q.pop(); }
    std::cout << "\npriority_queue: ";
    while (!pq.empty()) { std::cout << pq.top() << ' '; pq.pop(); }

    std::set<int> s{3, 1, 4, 1, 5};
    std::cout << "\nset:            ";
    for (int x : s) std::cout << x << ' ';
    std::cout << '\n';
    return 0;
}
```

运行结果：

```
stack:          5 1 4 1 3 
queue:          3 1 4 1 5 
priority_queue: 5 4 3 1 1 
set:            1 3 4 5 
```

| 适配器 | 规则 | 主要操作 |
|---|---|---|
| `stack` | 后进先出（LIFO） | `push`、`pop`、`top` |
| `queue` | 先进先出（FIFO） | `push`、`pop`、`front`、`back` |
| `priority_queue` | 每次取出最大的元素（堆） | `push`、`pop`、`top`，都是 O(log n) |

注意：`pop()` **只删除元素，不返回它**。要先用 `top()` 或 `front()` 读取，再 `pop()`。如果想让 `priority_queue` 每次取出最小的，写成 `std::priority_queue<int, std::vector<int>, std::greater<int>>`。

这三种结构在数据结构专栏中还会详细讲它们的实现。

## 6. 选择容器速查表

| 容器 | 底层结构 | 下标访问 | 末尾插入 | 开头插入 | 中间插入 | 查找 | 有序 |
|---|---|---|---|---|---|---|---|
| `vector` | 动态数组 | O(1) | 均摊 O(1) | O(n) | O(n) | O(n) | 否 |
| `deque` | 分段数组 | O(1) | O(1) | O(1) | O(n) | O(n) | 否 |
| `list` | 双向链表 | 不支持 | O(1) | O(1) | O(1)* | O(n) | 否 |
| `set` / `map` | 红黑树 | — | — | — | O(log n) | O(log n) | 是 |
| `unordered_set` / `unordered_map` | 哈希表 | — | — | — | 平均 O(1) | 平均 O(1) | 否 |

\* 需要已有指向插入位置的迭代器。

选择的思路：

1. **默认用 `vector`**。
2. 需要**按键查找**：要求有序或需要按顺序遍历用 `map` / `set`，否则用 `unordered_map` / `unordered_set`。
3. 需要在**两端**频繁插入删除：`deque`。
4. 只需要后进先出、先进先出或者取最大值：`stack`、`queue`、`priority_queue`。
5. `list`：只有在需要频繁在中间插入删除，而且已经测出 `vector` 是瓶颈时才考虑。

## 7. 常见错误

1. **用 `[]` 查询 `map`**：键不存在时会插入元素。只查询用 `find`、`count` 或 `at`。
2. **在 `const map&` 上用 `[]`**：编译错误，因为 `[]` 可能插入元素，不是 `const` 成员函数。
3. **用 `vector` 频繁在开头插入**：O(n²)，数据量大时极慢。
4. **在无序的 `vector` 中反复查找**：每次 O(n)。改用 `unordered_set`，或者排序后二分查找。
5. **以为 `unordered_map` 的遍历顺序是插入顺序**：顺序是不确定的。
6. **`pop()` 之后才读取元素**：`pop()` 不返回值，元素已经被删除了。
7. **持有 `vector` 元素的指针或引用，之后又 `push_back`**：扩容后指针失效。

## 8. 小结

- STL 由容器、迭代器、算法组成，都是模板
- `vector` 是首选容器：连续存储，下标访问 O(1)，末尾插入均摊 O(1)；容量不够时按倍数扩容，可以用 `reserve` 预先分配
- `deque` 两端插入快，`list` 任意位置插入快，但 `list` 的遍历因为缓存不友好而慢 10 倍以上
- `map` / `set` 是有序的红黑树，O(log n)；`unordered_map` / `unordered_set` 是哈希表，平均 O(1)
- `map[key]` 在键不存在时会插入元素，只查找用 `find` / `count` / `at`
- 实测在 10 万个元素中查找 10 万次：顺序查找 2 秒，哈希表 1.4 毫秒
- `stack`、`queue`、`priority_queue` 是容器适配器，`pop()` 不返回值

## 9. 练习

1. 为下面的场景选择容器：
   - (a) 存放学生的成绩，按学号查询，并且经常需要按学号从小到大输出；
   - (b) 判断一个单词是否在一个有 10 万个单词的词典中；
   - (c) 打印机按照任务到达的顺序依次处理；
   - (d) 读入 100 万个数据后，多次按位置访问。
2. 写一个函数，删除 `std::vector<int>` 中重复的元素，**保持原来的顺序**。比如 `{3, 1, 3, 2, 1, 5}` 变成 `{3, 1, 2, 5}`。
3. 下面的函数为什么编译不通过？怎么修改？

   ```cpp
   int lookup(const std::map<std::string, int>& m) { return m["x"]; }
   ```

<details>
<summary>点击查看答案</summary>

1. (a) `std::map<int, Score>`，按键有序；(b) `std::unordered_set<std::string>`；(c) `std::queue`；(d) `std::vector`。

2. 用一个 `unordered_set` 记录已经出现过的元素：

   ```cpp
   std::vector<int> unique_keep_order(const std::vector<int>& in)
   {
       std::unordered_set<int> seen;
       std::vector<int> out;
       for (int x : in) {
           if (seen.insert(x).second) {        // insert 返回 pair，second 表示是否真的插入了
               out.push_back(x);
           }
       }
       return out;
   }
   ```

   输出 `3 1 2 5`。时间复杂度平均 O(n)。如果不要求保持顺序，也可以直接把元素放进 `std::set`。

3. 编译错误：

   ```
   error: no viable overloaded operator[] for type 'const std::map<std::string, int>'
   ```

   `operator[]` 在键不存在时要插入元素，会修改 `map`，所以它不是 `const` 成员函数，不能用在 `const map&` 上。改用 `find`：

   ```cpp
   int lookup(const std::map<std::string, int>& m)
   {
       auto it = m.find("x");
       return it != m.end() ? it->second : 0;
   }
   ```

   或者用 `m.at("x")`，键不存在时抛出 `std::out_of_range`。

</details>
