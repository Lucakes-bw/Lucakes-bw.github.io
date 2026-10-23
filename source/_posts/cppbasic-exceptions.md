---
title: 异常处理
date: 2026-10-23 12:00:00
updated: 2026-10-23 12:00:00
categories:
  - C/C++
  - C++基础
tags:
  - C++基础
  - 异常
  - RAII
order: 10
description: 异常是 C++ 报告错误的标准方式。本文讲清楚 throw、try、catch 的用法和栈展开的过程，介绍标准异常体系和自定义异常类，实测异常导致的内存泄漏以及用 RAII 修复，最后说明 noexcept、异常安全保证和什么时候应该使用异常。
disableNunjucks: true
---

> **本文目标**：会用 `throw`、`try`、`catch` 抛出和处理异常；理解栈展开；熟悉常用的标准异常类型，会定义自己的异常类；理解为什么异常和 RAII 必须一起使用；知道 `noexcept` 的含义和三种异常安全保证。
>
> **前置知识**：《构造函数与析构函数》、《继承》、《多态与虚函数》，C++11 章节的《智能指针》。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。

## 1. C 语言的错误处理有什么问题

C 语言用**返回值**报告错误，比如 `fopen` 失败返回 `NULL`，`malloc` 失败返回 `NULL`，很多函数失败时返回 `-1`。这种方式有几个问题：

- **容易被忽略**：调用者不检查返回值，编译器也不会提醒，错误就悄悄地被吞掉了。
- **占用了返回值**：一个计算除法的函数，返回值已经用来返回结果了，出错时返回什么？只能再加一个输出参数或者全局变量（比如 `errno`）。
- **构造函数没有返回值**：构造函数失败时，根本没有办法通过返回值报告。
- **需要层层传递**：错误发生在第 5 层调用，但只有第 1 层知道怎么处理，中间的每一层都要写 `if (ret != 0) return ret;`。

C++ 提供了**异常**（exception）机制来解决这些问题。

## 2. throw、try、catch

```cpp
#include <iostream>
#include <stdexcept>

double divide(double a, double b)
{
    if (b == 0) {
        throw std::invalid_argument("除数不能为 0");     // 抛出异常
    }
    return a / b;
}

int main()
{
    try {
        double r1 = divide(10, 4);
        std::cout << "10 / 4 = " << r1 << '\n';
        double r2 = divide(1, 0);                          // 这里抛出异常
        std::cout << "1 / 0 = " << r2 << '\n';             // 不会执行
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到异常: " << e.what() << '\n';
    }
    std::cout << "程序继续运行\n";
    return 0;
}
```

运行结果：

```
10 / 4 = 2.5
捕获到异常: 除数不能为 0
程序继续运行
```

- **`throw 表达式`**：抛出一个异常。函数立即停止执行，后面的代码（包括 `return`）都不会执行。异常可以是任何类型，但**应该抛出 `std::exception` 的派生类对象**。
- **`try { ... }`**：把可能抛出异常的代码放在 `try` 块中。
- **`catch (类型 变量) { ... }`**：如果 `try` 块中抛出了匹配类型的异常，就跳到对应的 `catch` 块执行。执行完 `catch` 块后，程序从整个 `try-catch` 后面继续运行。
- **`e.what()`**：所有标准异常都有这个成员函数，返回描述错误的字符串，也就是抛出时传入的那句话。

`divide` 中不需要关心错误怎么处理，`main` 中也不需要在每次调用后检查返回值。**发现错误的地方负责抛出，知道怎么处理的地方负责捕获**，中间的代码什么都不用写。

## 3. 栈展开

异常抛出后，会沿着调用链一层一层地往外找匹配的 `catch`，途中经过的每个函数都会立即退出。那么这些函数中的局部对象怎么办？

```cpp
#include <iostream>
#include <stdexcept>
#include <string>

struct Tracer {
    std::string name;
    explicit Tracer(std::string n) : name(std::move(n)) { std::cout << "  构造 " << name << '\n'; }
    ~Tracer() { std::cout << "  析构 " << name << '\n'; }
};

void level3()
{
    Tracer t("level3 的局部对象");
    throw std::runtime_error("level3 出错了");
    std::cout << "  level3 结束\n";        // 不会执行
}

void level2()
{
    Tracer t("level2 的局部对象");
    level3();
    std::cout << "  level2 结束\n";        // 不会执行
}

int main()
{
    try {
        Tracer t("main 的局部对象");
        level2();
    } catch (const std::exception& e) {
        std::cout << "main 捕获: " << e.what() << '\n';
    }
    return 0;
}
```

运行结果：

```
  构造 main 的局部对象
  构造 level2 的局部对象
  构造 level3 的局部对象
  析构 level3 的局部对象
  析构 level2 的局部对象
  析构 main 的局部对象
main 捕获: level3 出错了
```

异常从 `level3` 抛出，经过 `level2`，最终在 `main` 中被捕获。在这个过程中，`level3`、`level2` 以及 `try` 块中的局部对象，**按照和构造相反的顺序，全部被析构了**，然后才进入 `catch` 块。这个过程叫**栈展开**（stack unwinding）。

**栈展开保证了局部对象的析构函数一定会被调用**。这是 C++ 异常机制最重要的性质，第 6 节的 RAII 完全建立在它的基础上。

如果一直到 `main` 都没有找到匹配的 `catch`，程序会调用 `std::terminate` 直接终止：

```cpp
#include <stdexcept>
int main() { throw std::runtime_error("没有人捕获我"); }
```

```
libc++abi: terminating due to uncaught exception of type std::runtime_error: 没有人捕获我
```

程序异常退出（退出码 134，即收到了 `SIGABRT` 信号）。

## 4. 标准异常

标准库定义了一组异常类，都派生自 **`std::exception`**（头文件 `<stdexcept>`、`<new>` 等）：

```
std::exception
├── std::logic_error             程序逻辑错误，理论上可以在运行前避免
│   ├── std::invalid_argument    参数无效
│   ├── std::out_of_range        超出范围
│   └── std::length_error        长度超过上限
├── std::runtime_error           运行时才能发现的错误
│   ├── std::overflow_error
│   └── std::range_error
└── std::bad_alloc               内存分配失败
```

标准库自己也会抛出这些异常：

```cpp
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

int main()
{
    std::vector<int> v{1, 2, 3};
    try {
        std::cout << v.at(10) << '\n';                 // at() 会检查下标
    } catch (const std::out_of_range& e) {
        std::cout << "out_of_range:     " << e.what() << '\n';
    }

    try {
        int n = std::stoi("abc");                      // 不是数字
        std::cout << n << '\n';
    } catch (const std::invalid_argument& e) {
        std::cout << "invalid_argument: " << e.what() << '\n';
    }

    try {
        int n = std::stoi("99999999999");              // 超出 int 的范围
        std::cout << n << '\n';
    } catch (const std::out_of_range& e) {
        std::cout << "out_of_range:     " << e.what() << '\n';
    }

    try {
        std::size_t n = std::size_t(1) << 50;          // 申请 4 PB 内存
        int* p = new int[n];
        std::cout << p[0] << '\n';
    } catch (const std::bad_alloc& e) {
        std::cout << "bad_alloc:        " << e.what() << '\n';
    }
    return 0;
}
```

运行结果（`what()` 的内容由标准库实现决定，不同编译器不同）：

```
out_of_range:     vector
invalid_argument: stoi: no conversion
out_of_range:     stoi: out of range
bad_alloc:        std::bad_alloc
```

注意：

- `v.at(10)` 会检查下标并抛出异常，而 `v[10]` **不检查**，越界是未定义行为。
- C++ 中的 `new` 失败时**抛出 `std::bad_alloc`**，而不是像 `malloc` 那样返回 `nullptr`。所以检查 `new` 的返回值是否为空是没有意义的。

## 5. 自定义异常与 catch 的规则

### 5.1 自定义异常类

通常从 `std::runtime_error` 或 `std::logic_error` 派生自己的异常类，可以携带额外的错误信息：

```cpp
#include <iostream>
#include <stdexcept>
#include <string>

class ConfigError : public std::runtime_error {
public:
    ConfigError(const std::string& key, int line)
        : std::runtime_error("配置项 '" + key + "' 无效（第 " + std::to_string(line) + " 行）"),
          key_(key), line_(line) {}
    const std::string& key() const { return key_; }
    int line() const { return line_; }
private:
    std::string key_;
    int line_;
};

void loadConfig() { throw ConfigError("port", 12); }

int main()
{
    try {
        loadConfig();
    } catch (const ConfigError& e) {                 // 先捕获具体的类型
        std::cout << "配置错误: " << e.what() << "，出错的键: " << e.key() << '\n';
    } catch (const std::exception& e) {              // 再捕获一般的类型
        std::cout << "其他错误: " << e.what() << '\n';
    }

    try {
        loadConfig();
    } catch (const std::exception& e) {              // 用基类的引用也能捕获
        std::cout << "通过 std::exception& 捕获: " << e.what() << '\n';
    }
    return 0;
}
```

运行结果：

```
配置错误: 配置项 'port' 无效（第 12 行），出错的键: port
通过 std::exception& 捕获: 配置项 'port' 无效（第 12 行）
```

- 把错误描述传给基类 `std::runtime_error` 的构造函数，`what()` 就会返回它。
- 知道具体错误类型的代码可以捕获 `ConfigError`，读取 `key()` 等额外信息；不关心细节的代码捕获 `std::exception&` 就行。
- `what()` 是一个虚函数，通过 `std::exception&` 调用时，返回的仍然是 `ConfigError` 的描述。这就是上一篇讲的多态。

### 5.2 一个 try 可以有多个 catch，按顺序匹配

异常会和 `catch` 子句**从上到下**逐个比较，进入**第一个**匹配的。派生类的异常可以匹配基类的 `catch`，所以**具体的类型要写在前面，一般的类型写在后面**。如果写反了：

```cpp
try {
    throw std::out_of_range("越界");
} catch (const std::exception& e) {          // 所有标准异常都会在这里被捕获
    std::cout << "exception: " << e.what() << '\n';
} catch (const std::out_of_range& e) {       // 永远不会执行
    std::cout << "out_of_range: " << e.what() << '\n';
}
```

编译器会给出警告：

```
warning: exception of type 'const std::out_of_range &' will be caught by earlier handler [-Wexceptions]
```

还可以用 **`catch (...)`** 捕获任何类型的异常，它必须写在最后。在 `catch (...)` 中无法知道异常的内容，一般只用来做清理工作或记录日志。

### 5.3 按 const 引用捕获

```cpp
try {
    throw std::out_of_range("下标 10 越界");
} catch (std::exception e) {                     // 按值捕获：切片
    std::cout << "按值捕获: " << e.what() << '\n';
}
try {
    throw std::out_of_range("下标 10 越界");
} catch (const std::exception& e) {
    std::cout << "按引用捕获: " << e.what() << '\n';
}
```

```
按值捕获: std::exception
按引用捕获: 下标 10 越界
```

按值捕获时，异常对象被拷贝成一个 `std::exception`，派生类的部分丢失了，这就是上一篇讲的**对象切片**，错误信息也随之丢失了。而且 clang 用 `-Wall -Wextra` 编译时没有任何警告。**规则：抛出时按值 `throw T(...)`，捕获时按 const 引用 `catch (const T& e)`。**

在 `catch` 块中可以用不带表达式的 **`throw;`** 把当前异常重新抛出，交给外层处理。比如只记录一下日志，不真正处理。

## 6. 异常与 RAII

栈展开会调用局部**对象**的析构函数，但是**裸指针不是对象的所有者**，它指向的内存不会被自动释放：

```cpp
#include <iostream>
#include <memory>
#include <stdexcept>

void process(int id)
{
    if (id == 2) throw std::runtime_error("处理第 2 项时出错");
}

void rawVersion()
{
    int* buffer = new int[1000];
    process(2);                       // 抛出异常，下面的 delete[] 被跳过
    delete[] buffer;
}

void raiiVersion()
{
    auto buffer = std::make_unique<int[]>(1000);
    process(2);                       // 抛出异常，buffer 的析构函数仍会释放内存
}

int main()
{
    try { rawVersion(); }  catch (const std::exception& e) { std::cout << "rawVersion: " << e.what() << '\n'; }
    try { raiiVersion(); } catch (const std::exception& e) { std::cout << "raiiVersion: " << e.what() << '\n'; }
    return 0;
}
```

运行结果和 `leaks` 工具的报告：

```
rawVersion: 处理第 2 项时出错
raiiVersion: 处理第 2 项时出错
Process 38490: 1 leak for 4096 total leaked bytes.
```

两个函数都在中途抛出了异常。`rawVersion` 的 `delete[]` 被跳过，泄漏了 4096 字节（4000 字节对齐后的大小）；`raiiVersion` 中的 `unique_ptr` 是一个局部对象，栈展开时它的析构函数被调用，内存被正确释放了。

这说明，**在有异常的程序中，任何「先申请、后释放」的手动管理方式都不安全**。只要中间有一行代码可能抛出异常（而几乎任何代码都可能，比如 `new`、`push_back`、`std::string` 拼接都可能抛出 `bad_alloc`），释放的代码就可能被跳过。不只是内存，文件、锁、网络连接都是如此。

解决的方法就是 **RAII**：把资源交给一个对象管理，在构造函数中获取，在析构函数中释放。内存用 `std::unique_ptr` / `std::vector`，文件用 `std::fstream`，锁用 `std::lock_guard`。**异常和 RAII 是一对搭档**：没有 RAII，异常处理几乎不可能写对。

## 7. noexcept 与异常安全

### 7.1 noexcept

在函数声明后面加上 **`noexcept`**，表示这个函数**承诺不抛出异常**。如果它违背了承诺：

```cpp
#include <iostream>
#include <stdexcept>

void promise() noexcept                     // 承诺不抛出异常
{
    throw std::runtime_error("违背承诺");
}

int main()
{
    try {
        promise();
    } catch (const std::exception& e) {
        std::cout << "捕获: " << e.what() << '\n';   // 不会执行
    }
    return 0;
}
```

```
warning: 'promise' has a non-throwing exception specification but can still throw [-Wexceptions]
libc++abi: terminating due to uncaught exception of type std::runtime_error: 违背承诺
```

即使外面有 `try-catch`，异常也不会传出 `noexcept` 函数，而是**直接调用 `std::terminate` 终止程序**。所以只有确定不会抛出异常的函数才能标记为 `noexcept`。

`noexcept` 的主要作用是让调用者（尤其是标准库）放心地做优化。最典型的例子是 C++11 章节《移动语义》中讲过的：`std::vector` 扩容时，只有元素的移动构造函数是 `noexcept` 的，才会使用移动，否则会退回到拷贝。所以**移动构造函数、移动赋值运算符、`swap` 函数应该标记为 `noexcept`**。

**析构函数默认就是 `noexcept` 的**，绝对不要从析构函数中抛出异常。栈展开的过程中会调用析构函数，如果这时析构函数又抛出了一个异常，程序同样会终止。

### 7.2 三种异常安全保证

一个函数抛出异常后，对象处于什么状态？按照从弱到强，有三种保证：

| 保证 | 含义 | 例子 |
|---|---|---|
| **基本保证** | 不泄漏资源，对象仍然有效（可以安全地使用和析构），但状态可能变了 | 所有代码至少要做到 |
| **强保证** | 要么完全成功，要么状态和调用之前完全一样（像数据库事务一样可以「回滚」） | `vector::push_back` |
| **不抛出保证** | 保证不抛出异常 | 析构函数、移动操作、`swap` |

使用 RAII 就能做到基本保证。强保证的常用技巧是「**先在副本上完成所有可能失败的操作，成功后再用不抛出异常的 `swap` 交换**」。比如《构造函数与析构函数》中 `MyString` 类的拷贝赋值运算符可以这样写，叫作拷贝并交换（copy-and-swap）：

```cpp
MyString& operator=(const MyString& other)
{
    MyString tmp(other);               // 可能抛出 bad_alloc，但此时 *this 还没有被修改
    std::swap(data_, tmp.data_);       // 交换指针，不会抛出异常
    return *this;
}                                      // tmp 析构时释放旧的内容
```

如果拷贝失败，异常在修改 `*this` 之前就抛出了，对象保持原样；而且自赋值也自然是正确的，不需要特别检查。

## 8. 什么时候使用异常

- **适合用异常的情况**：真正的「意外」错误，比如文件打不开、网络断开、配置无效、内存不足；以及构造函数失败（这是构造函数报告失败的唯一方式）。
- **不适合用异常的情况**：
  - **正常的流程控制**。比如在一个列表中查找元素，找不到是正常的结果，应该返回 `std::optional` 或者 `end()` 迭代器，而不是抛出异常。
  - **程序的 bug**。比如下标越界、空指针，这些应该在开发时用 `assert` 发现并修复，而不是在运行时捕获后继续运行。
- **不要捕获之后什么都不做**。`catch (...) {}` 把错误悄悄吞掉，比不处理还糟糕。不知道怎么处理，就不要捕获，让它继续往外传。
- **异常的开销**：在主流编译器上，不抛出异常时 `try` 块几乎没有运行时开销；但**抛出异常的代价很高**（通常是普通函数返回的成百上千倍）。所以异常只用于少见的错误，而不能用于频繁发生的情况。
- 有些项目（比如部分游戏引擎和嵌入式项目）会用编译选项 `-fno-exceptions` 完全禁用异常，改用错误码。C++23 的 `std::expected` 提供了一种「返回值或错误」的类型，是错误码的现代写法。

## 9. 常见错误

1. **按值捕获异常**：发生切片，丢失错误信息。总是用 `catch (const T& e)`。
2. **catch 的顺序写反了**：基类写在前面，派生类的 `catch` 永远不会执行。
3. **用裸指针管理资源**：异常导致 `delete` 被跳过，资源泄漏。使用 RAII。
4. **析构函数抛出异常**：程序终止。
5. **给可能抛出异常的函数加了 `noexcept`**：异常无法被捕获，程序终止。
6. **吞掉异常**：`catch (...) {}` 让错误无声无息地消失，问题更难排查。
7. **检查 `new` 的返回值是否为 `nullptr`**：`new` 失败时抛出 `bad_alloc`，不会返回空指针。

## 10. 小结

- `throw` 抛出异常，`try` 包住可能出错的代码，`catch` 按类型捕获并处理
- 异常沿调用链向外传播，途中经过的函数中的局部对象**按相反顺序被析构**（栈展开）；没有被捕获的异常会终止程序
- 标准异常都派生自 `std::exception`，用 `what()` 获取描述；自定义异常通常派生自 `std::runtime_error`
- `catch` 从上到下匹配，具体类型在前；总是**按 const 引用捕获**
- 异常会跳过手动释放资源的代码，**必须用 RAII 管理资源**（实测裸指针泄漏 4096 字节，`unique_ptr` 没有泄漏）
- `noexcept` 函数抛出异常会直接终止程序；移动操作、`swap`、析构函数应该是 `noexcept` 的
- 三种异常安全保证：基本保证、强保证、不抛出保证
- 异常用于少见的、真正的错误，不用于正常的流程控制

## 11. 练习

1. 写一个函数 `int parseAge(const std::string& s)`，用 `std::stoi` 把字符串转换为整数，如果年龄不在 0 到 150 之间，抛出 `std::out_of_range`。在 `main` 中依次解析 `"25"`、`"abc"`、`"200"`，分别输出年龄、「格式错误」或者异常的描述。
2. 下面的程序输出什么？

   ```cpp
   struct A { ~A() { std::cout << "~A "; } };
   void f() { A a; throw 42; }
   int main()
   {
       try { f(); }
       catch (int x) { std::cout << "caught " << x << '\n'; }
   }
   ```

3. 下面的函数有什么问题？怎么修改？

   ```cpp
   void init()
   {
       Widget* a = new Widget;
       Widget* b = new Widget;
       use(a, b);
       delete b;
       delete a;
   }
   ```

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   int parseAge(const std::string& s)
   {
       int age = std::stoi(s);                  // 非数字时 stoi 自己会抛出 invalid_argument
       if (age < 0 || age > 150) {
           throw std::out_of_range("年龄超出范围: " + s);
       }
       return age;
   }

   int main()
   {
       for (const char* s : {"25", "abc", "200"}) {
           try {
               int age = parseAge(s);
               std::cout << "年龄: " << age << '\n';
           } catch (const std::invalid_argument&) {
               std::cout << "格式错误: " << s << '\n';
           } catch (const std::out_of_range& e) {
               std::cout << e.what() << '\n';
           }
       }
   }
   ```

   输出：

   ```
   年龄: 25
   格式错误: abc
   年龄超出范围: 200
   ```

   注意 `parseAge` 中不需要自己检查格式，`stoi` 失败时抛出的异常会直接穿过 `parseAge` 传到 `main`。另外，如果把输出写成 `std::cout << "年龄: " << parseAge(s)`，「年龄: 」会在异常抛出之前就被输出，所以先把结果存到变量中。

2. 输出 `~A caught 42`。异常抛出后先进行栈展开，析构 `f` 中的局部对象 `a`，然后才进入 `catch` 块。

3. 如果第二个 `new Widget` 抛出了 `bad_alloc`，或者 `use` 抛出了异常，`delete` 就会被跳过，造成泄漏。改为使用智能指针：

   ```cpp
   void init()
   {
       auto a = std::make_unique<Widget>();
       auto b = std::make_unique<Widget>();
       use(a.get(), b.get());
   }                                            // 无论是否发生异常，a 和 b 都会被释放
   ```

</details>
