---
title: Ranges：管道式的算法组合
date: 2026-10-22 18:40:00
updated: 2026-10-22 18:40:00
categories:
  - C/C++
  - C++20
tags:
  - C++20
  - 标准库
  - Ranges
order: 2
description: C++20 的 Ranges 让算法直接接收容器，用投影代替比较函数，还能用管道符 | 把 filter、transform、take 等视图串起来，惰性求值、没有中间容器。本文通过实验观察惰性求值的过程，并讲清楚悬空迭代器、filter 视图不能 const 遍历等陷阱。
disableNunjucks: true
---

> **本文目标**：会用 `std::ranges` 中的算法直接操作容器，会用投影；会用 `|` 组合 `filter`、`transform`、`take` 等视图；理解视图是惰性求值的；认识常用的视图；知道 `ranges::dangling` 和 `filter_view` 的陷阱。
>
> **前置知识**：本章《Concepts》，STL 算法和迭代器，Lambda 表达式。
>
> **编译环境**：`g++ -std=c++20` / `clang++ -std=c++20`，GCC 10+ / Clang 16+ / VS2019 16.10+，需要包含头文件 `<ranges>` 和 `<algorithm>`。

## 1. 问题：STL 算法组合起来很啰嗦

任务：从一组数中取出偶数，求平方，只要前 3 个。用 C++17 的 STL 算法：

```cpp
#include <algorithm>
#include <iostream>
#include <ranges>
#include <vector>

int main()
{
    std::vector<int> nums{5, 2, 8, 1, 9, 4, 7, 6, 3};

    // 任务：取出偶数，求平方，只要前 3 个

    // C++17 写法：每一步都要一个中间容器
    std::vector<int> evens;
    std::copy_if(nums.begin(), nums.end(), std::back_inserter(evens), [](int x) { return x % 2 == 0; });
    std::vector<int> squares;
    std::transform(evens.begin(), evens.end(), std::back_inserter(squares), [](int x) { return x * x; });
    squares.resize(std::min<std::size_t>(squares.size(), 3));
    for (int x : squares) std::cout << x << ' ';
    std::cout << '\n';

    // C++20 写法：用管道把视图串起来，没有中间容器
    auto result = nums
                | std::views::filter([](int x) { return x % 2 == 0; })
                | std::views::transform([](int x) { return x * x; })
                | std::views::take(3);
    for (int x : result) std::cout << x << ' ';
    std::cout << '\n';
    return 0;
}
```

运行结果：

```
4 64 16 
4 64 16 
```

C++17 的写法有三个问题：

1. 每个算法都要写 `begin()`、`end()`；
2. 每一步都需要一个**中间容器**，浪费内存；
3. 算法之间无法直接组合，代码的阅读顺序和数据流动的方向不一致。

C++20 的写法用 `|`（管道符）把三个**视图**串起来，读起来就像描述数据的处理流程：「过滤出偶数 → 求平方 → 取前 3 个」。整个过程**没有创建任何中间容器**。

## 2. Ranges 算法

`<algorithm>` 中的大部分算法，在 `std::ranges` 命名空间中都有一个新版本：

```cpp
#include <algorithm>
#include <iostream>
#include <ranges>
#include <string>
#include <vector>

struct Student {
    std::string name;
    int score;
};

int main()
{
    std::vector<int> v{5, 3, 8, 1};
    std::ranges::sort(v);                                  // 直接传容器，不用 begin()/end()
    for (int x : v) std::cout << x << ' ';
    std::cout << '\n';

    std::vector<Student> students{{"Alice", 85}, {"Bob", 92}, {"Carol", 78}};

    // 投影：按 score 排序，不用写比较 lambda
    std::ranges::sort(students, std::ranges::greater{}, &Student::score);
    for (const auto& s : students) std::cout << s.name << ' ' << s.score << "  ";
    std::cout << '\n';

    // 投影同样适用于查找、求最值
    auto it = std::ranges::find(students, "Carol", &Student::name);
    std::cout << "找到 " << it->name << '\n';
    auto best = std::ranges::max_element(students, {}, &Student::score);
    std::cout << "最高分 " << best->name << '\n';

    std::cout << std::boolalpha << "全部及格: "
              << std::ranges::all_of(students, [](int s) { return s >= 60; }, &Student::score) << '\n';
    return 0;
}
```

运行结果：

```
1 3 5 8 
Bob 92  Alice 85  Carol 78  
找到 Carol
最高分 Bob
全部及格: true
```

和旧版本相比，ranges 算法有三个改进：

1. **直接接收范围**：`std::ranges::sort(v)`，不用再写 `v.begin(), v.end()`。
2. **投影**（projection）：最后一个参数可以是一个「从元素中取出用于比较的值」的函数，通常直接写**成员指针** `&Student::score`。按成绩降序排序，只需要 `std::ranges::sort(students, std::ranges::greater{}, &Student::score)`，不用写 `[](const Student& a, const Student& b) { return a.score > b.score; }`。查找也一样：`find(students, "Carol", &Student::name)` 表示「在 name 中查找 Carol」。
3. **有 Concepts 约束**：用错时错误信息清晰得多，见上一篇《Concepts》开头的例子。

第二个参数位置上的 `{}` 表示使用默认的比较器 `std::ranges::less`。

## 3. 视图与惰性求值

**视图**（view）是一种轻量的范围：它**不拥有元素**，而是描述「如何从另一个范围得到元素」。视图的关键特性是**惰性求值**：创建视图时不做任何计算，直到遍历时才逐个计算元素。

在每个 lambda 中加上打印，观察计算是什么时候发生的：

```cpp
#include <iostream>
#include <ranges>
#include <vector>

int main()
{
    std::vector<int> nums{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    auto view = nums
              | std::views::filter([](int x) { std::cout << "  filter(" << x << ")\n"; return x % 2 == 0; })
              | std::views::transform([](int x) { std::cout << "  transform(" << x << ")\n"; return x * 10; })
              | std::views::take(2);

    std::cout << "视图已经创建，但还没有做任何计算\n";
    for (int x : view)
        std::cout << "得到 " << x << '\n';
    std::cout << "7~10 根本没有被检查\n";

    // 无限序列：iota(1) 是 1, 2, 3, ... 无穷无尽，配合 take 才能用
    for (int x : std::views::iota(1) | std::views::transform([](int n) { return n * n; })
                                     | std::views::take_while([](int n) { return n < 50; }))
        std::cout << x << ' ';
    std::cout << '\n';
    return 0;
}
```

运行结果：

```
视图已经创建，但还没有做任何计算
  filter(1)
  filter(2)
  transform(2)
得到 20
  filter(3)
  filter(4)
  transform(4)
得到 40
  filter(5)
  filter(6)
7~10 根本没有被检查
1 4 9 16 25 36 49 
```

从输出可以看出：

1. **创建视图时没有调用任何 lambda**，第一行输出在所有计算之前。
2. 元素是**一个一个流过整条管道**的：`filter(2)` 通过后，立刻 `transform(2)`，然后输出 20，再去处理下一个元素。而 C++17 的写法是先把**所有**元素过滤一遍，再把结果**全部**变换一遍。
3. **只计算需要的元素**：拿到 2 个元素后，`take(2)` 就结束了，7 到 10 完全没有被检查。注意 5 和 6 还是被检查了：拿到第 2 个元素后，for 循环还会把迭代器向前推进一次，过滤视图会一直找到下一个偶数 6 才停下，然后 `take` 才发现已经够 2 个了。
4. 因为是惰性的，视图可以表示**无限序列**：`views::iota(1)` 是 1, 2, 3, …，永不结束，配合 `take` 或 `take_while` 使用即可。

视图本身非常轻量，通常只保存对底层范围的引用和几个函数对象，复制视图的开销是 O(1)。

## 4. 常用视图

```cpp
#include <iostream>
#include <map>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

template <typename R>
void show(const char* title, R&& r)
{
    std::cout << title << ':';
    for (auto&& x : r) std::cout << ' ' << x;
    std::cout << '\n';
}

int main()
{
    std::vector<int> v{1, 2, 3, 4, 5, 6, 7, 8};
    namespace views = std::views;

    show("iota(1, 6)    ", views::iota(1, 6));
    show("reverse       ", v | views::reverse);
    show("drop(5)       ", v | views::drop(5));
    show("take_while<4  ", v | views::take_while([](int x) { return x < 4; }));
    show("drop_while<4  ", v | views::drop_while([](int x) { return x < 4; }));

    std::map<std::string, int> ages{{"Alice", 20}, {"Bob", 22}};
    show("keys          ", ages | views::keys);
    show("values        ", ages | views::values);

    std::string_view csv = "red,green,blue";
    std::cout << "split         :";
    for (auto part : csv | views::split(','))
        std::cout << " [" << std::string_view(part.begin(), part.end()) << ']';
    std::cout << '\n';

    // 视图可以先保存起来，再组合
    auto evens = views::filter([](int x) { return x % 2 == 0; });
    auto doubled = views::transform([](int x) { return x * 2; });
    show("evens|doubled ", v | evens | doubled);
    return 0;
}
```

运行结果：

```
iota(1, 6)    : 1 2 3 4 5
reverse       : 8 7 6 5 4 3 2 1
drop(5)       : 6 7 8
take_while<4  : 1 2 3
drop_while<4  : 4 5 6 7 8
keys          : Alice Bob
values        : 20 22
split         : [red] [green] [blue]
evens|doubled : 4 8 12 16
```

| 视图 | 作用 |
| --- | --- |
| `views::filter(谓词)` | 只保留满足条件的元素 |
| `views::transform(函数)` | 对每个元素做变换 |
| `views::take(n)` / `views::drop(n)` | 取前 n 个 / 跳过前 n 个 |
| `views::take_while(谓词)` / `views::drop_while(谓词)` | 取 / 跳过开头满足条件的元素 |
| `views::reverse` | 反向 |
| `views::iota(a, b)` / `views::iota(a)` | 整数序列 `[a, b)` / 从 a 开始的无限序列 |
| `views::keys` / `views::values` | 取 pair 或 tuple 的第一个 / 第二个元素，常用于 map |
| `views::elements<N>` | 取 tuple 的第 N 个元素 |
| `views::split(分隔符)` | 按分隔符切分 |
| `views::join` | 把「范围的范围」展平成一个范围 |

视图适配器本身也是对象，可以先保存成变量（`evens`、`doubled`），再用 `|` 组合，代码可以像搭积木一样复用。

## 5. 常见错误

### 5.1 对临时容器调用算法：ranges::dangling

```cpp
#include <algorithm>
#include <vector>
std::vector<int> makeData() { return {3, 1, 2}; }
int main()
{
    auto it = std::ranges::find(makeData(), 2);   // 临时 vector 在这一行结束时销毁
    return *it;                                   // 解引用一个悬空的迭代器？
}
```

```
error: indirection requires pointer operand ('borrowed_iterator_t<std::vector<int>>' (aka 'std::ranges::dangling') invalid)
```

`makeData()` 返回的临时 `vector` 在语句结束时就销毁了，返回指向它的迭代器必然悬空。ranges 算法发现传入的是**临时容器**时，不返回迭代器，而是返回一个特殊的空类型 **`std::ranges::dangling`**，对它解引用会编译失败。旧版的 `std::find(makeData().begin(), ...)` 就没有这个保护，会悄悄产生悬空迭代器。解决办法：先把容器保存到变量中。

### 5.2 filter_view 不能 const 遍历

```cpp
#include <iostream>
#include <ranges>
#include <vector>
void print(const auto& r)                 // 按 const& 接收视图
{
    for (int x : r) std::cout << x << ' ';
}
int main()
{
    std::vector<int> v{1, 2, 3, 4};
    print(v | std::views::transform([](int x) { return x * 2; }));   // OK
    print(v | std::views::filter([](int x) { return x % 2 == 0; })); // 错误：filter_view 不能 const 遍历
}
```

```
error: 'this' argument to member function 'begin' has type 'const std::ranges::filter_view<...>', but function is not marked const
```

`filter_view` 为了让多次遍历时 `begin()` 保持 O(1)，会在第一次调用 `begin()` 时**缓存**找到的第一个元素的位置，这需要修改视图自身，所以它的 `begin()` 不是 `const` 的。`drop_while_view`、某些 `reverse_view` 也有同样的问题。**接收视图的函数参数，应该写成 `auto&&`（万能引用）或按值传递**（视图复制开销很小），不要写成 `const auto&`。

### 5.3 遍历过程中修改底层容器

视图只是引用底层容器。遍历视图时修改底层容器（比如 `push_back` 导致扩容），和直接遍历容器时修改它一样，会使迭代器失效。另外，由于 `filter_view` 缓存了第一个元素的位置，**遍历过一次之后再修改底层元素，使第一个元素不再满足条件**，第二次遍历的结果是未定义的。

### 5.4 视图的生命周期

视图引用底层容器，**容器必须比视图活得久**。下面的函数返回了一个引用局部变量的视图，调用者使用时容器已经销毁：

```cpp
auto evensOf()
{
    std::vector<int> v{1, 2, 3, 4};
    return v | std::views::filter([](int x) { return x % 2 == 0; });   // 悬空！
}
```

## 6. 进阶

### 6.1 把视图收集成容器（C++23）

C++20 没有提供把视图转换成容器的方法，只能手动遍历再 `push_back`。C++23 增加了 `std::ranges::to`：

```cpp
#include <iostream>
#include <ranges>
#include <vector>
int main()
{
    auto v = std::views::iota(1, 6)
           | std::views::transform([](int x) { return x * x; })
           | std::ranges::to<std::vector>();                    // C++23：把视图收集成容器
    std::cout << "v.size() = " << v.size() << ", v.back() = " << v.back() << '\n';
    return 0;
}
```

用 `-std=c++23` 编译，运行结果：

```
v.size() = 5, v.back() = 25
```

C++23 还增加了 `views::zip`、`views::enumerate`（带下标遍历）、`views::chunk`、`views::slide`、`views::cartesian_product` 等大量视图。

### 6.2 视图与性能

惰性求值、没有中间容器，并不意味着视图一定更快。每一层视图都会增加一点迭代器的开销，编译器通常能把它们优化掉，但组合非常复杂时（尤其是 `filter`、`join` 嵌套很多层），生成的代码可能不如手写循环高效。性能关键的代码，同样需要测量。

## 7. 小结

- `std::ranges::sort(v)` 等算法直接接收容器，支持**投影**（如 `&Student::score`），并带有 Concepts 约束
- **视图**不拥有元素，**惰性求值**：创建时不计算，遍历时元素逐个流过整条管道，只计算需要的部分
- 用 `|` 组合 `filter`、`transform`、`take`、`drop`、`reverse`、`keys`、`split` 等视图，没有中间容器
- `views::iota(1)` 是无限序列，配合 `take` / `take_while` 使用
- 对临时容器调用算法会返回 `ranges::dangling`，防止悬空迭代器
- `filter_view` 不能 const 遍历，接收视图的参数写 `auto&&`
- 视图引用底层容器，容器必须比视图活得久；C++23 用 `ranges::to` 把视图收集成容器

## 8. 练习

1. 用视图输出 1 到 100 中所有能被 3 整除、但不能被 5 整除的数的平方，只输出前 5 个。
2. 给定 `std::vector<std::string> words`，用 ranges 算法和投影找出最长的单词。
3. 有一个 `std::map<std::string, int> scores`，用视图输出所有分数大于 80 的学生名字。

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   for (int x : std::views::iota(1, 101)
                | std::views::filter([](int n) { return n % 3 == 0 && n % 5 != 0; })
                | std::views::transform([](int n) { return n * n; })
                | std::views::take(5))
       std::cout << x << ' ';
   // 9 36 81 144 324
   ```

2. 投影取每个字符串的长度：

   ```cpp
   auto it = std::ranges::max_element(words, {}, &std::string::size);
   ```

   `&std::string::size` 是成员函数指针，ranges 算法也支持把它作为投影。（严格来说标准不保证可以对标准库的成员函数取地址，更稳妥的写法是 `[](const std::string& s) { return s.size(); }`。）

3. 参考代码：

   ```cpp
   for (const auto& name : scores
                         | std::views::filter([](const auto& kv) { return kv.second > 80; })
                         | std::views::keys)
       std::cout << name << '\n';
   ```

</details>
