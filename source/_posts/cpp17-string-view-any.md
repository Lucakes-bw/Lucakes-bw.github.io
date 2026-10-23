---
title: std::string_view 与 std::any
date: 2026-10-22 16:40:00
updated: 2026-10-22 16:40:00
categories:
  - C/C++
  - C++17
tags:
  - C++17
  - 标准库
  - 字符串
order: 5
description: string_view 是字符串的只读「视图」，传参和截取子串都不需要拷贝和分配内存，实测能省掉上千次堆分配；但它不拥有数据，用错了就会悬空。std::any 则可以保存任意类型的值。本文讲清楚两者的用法、陷阱和适用场景。
disableNunjucks: true
---

> **本文目标**：理解 `string_view` 是「指针 + 长度」的视图；会用 `string_view` 作为只读字符串参数，会用它高效地截取、切分字符串；避开悬空和缺少 `'\0'` 两大陷阱；会用 `std::any` 保存任意类型，知道它和 `variant` 的区别。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`，需要包含头文件 `<string_view>`、`<any>`。

## 1. 问题：只读字符串参数该用什么类型

一个只需要读取字符串的函数，参数类型通常写成 `const std::string&`。问题在于，调用者手里不一定是 `std::string`：

```cpp
std::size_t countSpaces(const std::string& s);

countSpaces("hello world");      // 字面量：先构造一个临时 string，可能要分配堆内存
countSpaces(buffer);             // char 数组：同上
countSpaces(s.substr(0, 10));    // 截取子串：substr 返回新的 string，又是一次分配
```

如果参数写成 `const char*`，`std::string` 又要调用 `.c_str()`，而且失去了长度信息，每次都得用 `strlen` 重新计算。

C++17 的 **`std::string_view`** 解决了这个问题：它只保存**一个指向字符数据的指针和一个长度**，不拥有、不拷贝数据。任何字符串类型都能廉价地转换成它。

## 2. 实测：省掉的堆分配

用自定义的 `operator new` 统计堆分配次数：

```cpp
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <string_view>

static int allocations = 0;
void* operator new(std::size_t n)
{
    ++allocations;
    if (void* p = std::malloc(n)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }

std::size_t countSpacesStr(const std::string& s)      // 参数是 const string&
{
    std::size_t n = 0;
    for (char c : s) n += (c == ' ');
    return n;
}

std::size_t countSpacesSv(std::string_view s)          // 参数是 string_view
{
    std::size_t n = 0;
    for (char c : s) n += (c == ' ');
    return n;
}

int main()
{
    const char* text = "the quick brown fox jumps over the lazy dog, again and again";
    std::size_t total = 0;

    allocations = 0;
    for (int i = 0; i < 1000; ++i) total += countSpacesStr(text);   // 每次都要构造临时 string
    std::printf("const string& 传 const char*: 堆分配 %d 次\n", allocations);

    allocations = 0;
    for (int i = 0; i < 1000; ++i) total += countSpacesSv(text);    // 只记录指针和长度
    std::printf("string_view   传 const char*: 堆分配 %d 次\n", allocations);

    std::string s = text;
    allocations = 0;
    for (int i = 0; i < 1000; ++i) total += s.substr(4, 40).size();                    // substr 返回新的 string
    std::printf("string::substr            : 堆分配 %d 次\n", allocations);

    std::string_view sv = s;
    allocations = 0;
    for (int i = 0; i < 1000; ++i) total += sv.substr(4, 40).size();                   // 返回新的 string_view
    std::printf("string_view::substr       : 堆分配 %d 次\n", allocations);
    return total == 0;
}
```

运行结果（`-O2`）：

```
const string& 传 const char*: 堆分配 1000 次
string_view   传 const char*: 堆分配 0 次
string::substr            : 堆分配 1000 次
string_view::substr       : 堆分配 0 次
```

- 把 `const char*` 传给 `const std::string&` 参数，每次调用都要构造一个临时 `string`，字符串较长时（超过小字符串优化的长度，libc++ 中是 22 个字符）就要分配堆内存。`string_view` 只需要记下指针，再算一次长度。
- `std::string::substr` 返回一个新的 `string`，必须拷贝字符；`string_view::substr` 只是返回一个新的指针和长度，复杂度是 O(1)。

## 3. 基本用法

```cpp
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

// 按分隔符切分字符串，返回的每一段都指向原字符串，不拷贝
std::vector<std::string_view> split(std::string_view s, char sep)
{
    std::vector<std::string_view> parts;
    while (true) {
        std::size_t pos = s.find(sep);
        parts.push_back(s.substr(0, pos));
        if (pos == std::string_view::npos) break;
        s.remove_prefix(pos + 1);          // 只移动起点，O(1)
    }
    return parts;
}

bool startsWith(std::string_view s, std::string_view prefix)
{
    return s.substr(0, prefix.size()) == prefix;
}

int main()
{
    std::string line = "GET /index.html HTTP/1.1";
    for (std::string_view part : split(line, ' '))
        std::cout << '[' << part << "] ";
    std::cout << '\n';

    std::cout << std::boolalpha
              << startsWith(line, "GET") << ' '                  // string 和字面量都能直接传
              << startsWith("POST /api", std::string("POST")) << '\n';

    std::string_view sv = "  hello  ";
    sv.remove_prefix(sv.find_first_not_of(' '));
    sv.remove_suffix(sv.size() - sv.find_last_not_of(' ') - 1);
    std::cout << "去掉首尾空格: [" << sv << "]\n";

    std::string owned(sv);                  // 需要拥有一份数据时，显式构造 string
    std::cout << "sizeof(string_view) = " << sizeof(std::string_view) << '\n';
    return 0;
}
```

运行结果：

```
[GET] [/index.html] [HTTP/1.1] 
true true
去掉首尾空格: [hello]
sizeof(string_view) = 16
```

- `string_view` 提供了 `std::string` 中几乎所有的**只读**操作：`size`、`[]`、`find`、`substr`、`compare`、`==` 等，可以直接用 `<<` 输出，可以用范围 for 遍历。
- **`remove_prefix(n)` / `remove_suffix(n)`**：把视图的起点后移、终点前移，是 O(1) 操作，原字符串不受影响。`split` 和去空格都利用了这一点，整个过程**没有拷贝任何字符**。
- `sizeof(string_view)` 是 16 字节（一个指针加一个长度），**按值传递**即可，不要写成 `const std::string_view&`。
- `string` 可以隐式转换成 `string_view`，反过来则必须**显式**构造：`std::string owned(sv);`。这是故意的设计，因为构造 `string` 需要拷贝和分配内存。

> C++20 起，`string` 和 `string_view` 都增加了 `starts_with` 和 `ends_with` 成员函数。

## 4. 两大陷阱

`string_view` 快，是因为它**不拥有数据**。这也带来了两个很容易踩的坑。

### 4.1 悬空：指向的字符串已经销毁

```cpp
#include <iostream>
#include <string>
#include <string_view>

std::string makeName() { return "a-fairly-long-name-to-avoid-small-string-optimization"; }

int main()
{
    std::string_view name = makeName();          // 临时 string 在这一行结束时就销毁了
    std::cout << name << '\n';                   // 悬空！
    return 0;
}
```

Clang 给出了警告：

```
warning: object backing the pointer will be destroyed at the end of the full-expression [-Wdangling-gsl]
```

用 AddressSanitizer 运行：

```
ERROR: AddressSanitizer: heap-use-after-free
```

`makeName()` 返回的临时 `string` 在这条语句结束时就被销毁了，`name` 指向的是已经释放的内存。这和 C++11 章节中引用捕获、返回局部变量引用的问题本质上一样：**`string_view` 就像一个引用，必须保证它指向的字符串活得比它久**。

使用规则：

- **作为函数参数**：非常安全，调用期间实参一定有效。这是 `string_view` 最主要的用途。
- **作为返回值**：只有当它指向的是参数或全局数据时才安全，不能指向函数内的局部 `string`。
- **作为成员变量或长期保存**：要格外小心，大多数情况下应该保存 `std::string`。
- **不要用临时 `string` 初始化 `string_view` 变量**。

### 4.2 不保证以 '\0' 结尾

```cpp
#include <cstdio>
#include <string_view>
int main()
{
    std::string_view full = "hello world";
    std::string_view hello = full.substr(0, 5);
    std::printf("用 %%s 打印 data(): %s\n", hello.data());                         // 错误：data() 后面没有 '\0'
    std::printf("用 %%.*s 指定长度:  %.*s\n", static_cast<int>(hello.size()), hello.data());   // 正确
    return 0;
}
```

运行结果：

```
用 %s 打印 data(): hello world
用 %.*s 指定长度:  hello
```

`hello` 只是 `"hello world"` 前 5 个字符的视图，它的 `data()` 指向的内存后面紧跟着 `" world"`，而不是 `'\0'`。`printf("%s")`、`strlen`、`fopen` 这些 C 函数会一直读到 `'\0'` 为止，于是读多了。如果视图来自一个不以 `'\0'` 结尾的缓冲区，还会越界读取。

**规则：不要把 `sv.data()` 传给需要 C 字符串的函数。** 需要时先构造 `std::string(sv)`，再调用 `.c_str()`。

## 5. std::any：能装任何类型的容器

`std::any` 可以保存**任意可拷贝类型**的单个值，并在运行时记住它的类型：

```cpp
#include <any>
#include <iostream>
#include <map>
#include <string>
#include <vector>

int main()
{
    std::any a = 42;                         // 可以保存任何可拷贝的类型
    std::cout << "int: " << std::any_cast<int>(a) << ", type = " << a.type().name() << '\n';
    a = std::string("hello");
    std::cout << "string: " << std::any_cast<std::string>(a) << '\n';
    a = std::vector<int>{1, 2, 3};
    std::cout << "vector size: " << std::any_cast<std::vector<int>&>(a).size() << '\n';   // 取引用，避免拷贝

    // 指针版本：类型不对返回 nullptr
    if (auto p = std::any_cast<int>(&a)) std::cout << *p << '\n';
    else std::cout << "当前保存的不是 int\n";

    // 值版本：类型不对抛出异常
    try {
        double d = std::any_cast<double>(a);
        std::cout << d << '\n';
    } catch (const std::bad_any_cast& e) {
        std::cout << "异常: " << e.what() << '\n';
    }

    a.reset();
    std::cout << "reset 后 has_value: " << a.has_value() << '\n';

    // 典型用途：属性表，值的类型各不相同
    std::map<std::string, std::any> props{{"width", 800}, {"title", std::string("窗口")}, {"visible", true}};
    std::cout << std::any_cast<std::string>(props["title"]) << ' '
              << std::any_cast<int>(props["width"]) << ' '
              << std::boolalpha << std::any_cast<bool>(props["visible"]) << '\n';
    return 0;
}
```

运行结果：

```
int: 42, type = i
string: hello
vector size: 3
当前保存的不是 int
异常: bad any cast
reset 后 has_value: 0
窗口 800 true
```

- **`std::any_cast<T>(a)`**：取出值的副本，类型不对时抛出 `std::bad_any_cast`；`any_cast<T&>(a)` 取引用，避免拷贝大对象。
- **`std::any_cast<T>(&a)`**：参数是指针时，类型不对返回 `nullptr`，不抛异常。
- **类型必须完全匹配**：保存的是 `int`，用 `any_cast<long>` 取会失败，不会做任何转换。
- `a.type()` 返回 `std::type_info`，`name()` 的内容由编译器决定（Clang 中 `int` 显示为 `i`），只适合调试。

### 5.1 any 还是 variant

| | `std::variant<T1, T2, ...>` | `std::any` |
| --- | --- | --- |
| 可以保存的类型 | **编译时确定**的有限几种 | **任意**可拷贝类型 |
| 类型检查 | `visit` 漏掉类型会编译失败 | 只能在运行时检查，取错了抛异常 |
| 内存 | 不分配堆内存 | 小对象放在内部缓冲区，大对象需要堆分配 |
| 访问方式 | `visit`、`get` | 只能 `any_cast` 到确切的类型 |

**能用 `variant` 就不要用 `any`**。`any` 适合「事先完全不知道会有哪些类型」的场合，比如通用的属性表、插件之间传递数据、脚本语言绑定。在普通的业务代码中，`any` 往往意味着丢失了本该由类型系统提供的检查。

## 6. 常见错误

1. **用临时对象初始化 `string_view`**：见第 4.1 节。`std::string_view sv = a + b;` 同样会悬空。
2. **把 `sv.data()` 当 C 字符串用**：见第 4.2 节。
3. **用 `string_view` 作为 `map` 的键**：键指向的字符串必须比 `map` 活得久，通常应该用 `std::string` 作键，配合 C++14 介绍的 `std::less<>` 实现用 `string_view` 查找而不构造临时 `string`。
4. **`any_cast` 时类型不完全匹配**：`std::any a = "text";` 保存的是 `const char*`，用 `any_cast<std::string>` 会失败。

## 7. 进阶：参数用 string_view 还是 const string&

| 函数中对字符串的用法 | 推荐的参数类型 |
| --- | --- |
| 只读，用完即走（查找、比较、解析、输出） | `std::string_view` |
| 要保存一份（存成成员、放进容器） | `std::string`（按值传入，再 `std::move` 进成员） |
| 要调用需要 C 字符串的 API（`fopen`、系统调用） | `const std::string&` 或 `const char*` |

第二行的「按值传入再移动」在 C++11 章节《右值引用与移动语义》中介绍过：调用者传临时字符串时只有一次移动，传左值时只有一次拷贝。如果这里用 `string_view` 作参数，函数内部再构造 `string`，无论如何都要拷贝一次，反而不如直接接收 `string`。

## 8. 小结

- `std::string_view` 是「指针 + 长度」的只读视图，不拥有数据；任何字符串都能廉价地转换成它
- 作为只读参数、截取子串、切分字符串时，不需要拷贝和堆分配，实测能省掉大量分配
- `remove_prefix` / `remove_suffix` 是 O(1) 操作；按值传递 `string_view`
- **陷阱一**：它指向的字符串必须比它活得久，不要用临时 `string` 初始化它
- **陷阱二**：不保证以 `'\0'` 结尾，不要把 `data()` 传给 C 字符串函数
- `std::any` 保存任意可拷贝类型，用 `any_cast` 取出，类型必须完全匹配；能用 `variant` 时优先用 `variant`

## 9. 练习

1. 写一个函数 `std::string_view trim(std::string_view s)`，去掉首尾的空白字符（空格、`\t`、`\n`），全是空白时返回空视图。
2. 写一个函数 `std::string_view extension(std::string_view filename)`，返回文件扩展名，例如 `extension("photo.jpg")` 返回 `"jpg"`，没有扩展名时返回空视图。它的返回值在什么情况下会悬空？
3. 下面的代码有什么问题？

   ```cpp
   std::string_view getGreeting(bool formal)
   {
       std::string s = formal ? "您好" : "你好";
       s += "，欢迎！";
       return s;
   }
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   std::string_view trim(std::string_view s)
   {
       const char* ws = " \t\n";
       std::size_t begin = s.find_first_not_of(ws);
       if (begin == std::string_view::npos) return {};
       std::size_t end = s.find_last_not_of(ws);
       return s.substr(begin, end - begin + 1);
   }
   ```

2. 参考代码：

   ```cpp
   std::string_view extension(std::string_view filename)
   {
       std::size_t dot = filename.rfind('.');
       if (dot == std::string_view::npos) return {};
       return filename.substr(dot + 1);
   }
   ```

   返回值指向参数 `filename` 所指向的字符串。如果调用者传入的是临时 `string`，例如 `auto ext = extension(dir + "/a.txt");`，`ext` 就会悬空。

3. 返回的 `string_view` 指向局部变量 `s`，函数返回时 `s` 被销毁，视图悬空。返回类型应该改成 `std::string`。

</details>
