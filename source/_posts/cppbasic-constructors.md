---
title: 构造函数与析构函数
date: 2026-10-23 10:20:00
updated: 2026-10-23 10:20:00
categories:
  - C/C++
  - C++基础
tags:
  - C++基础
  - 面向对象
  - RAII
order: 5
description: 构造函数保证对象一诞生就处于合法状态，析构函数保证对象消亡时释放资源。本文讲清楚初始化列表和成员初始化顺序的陷阱、对象的生命周期、RAII，以及浅拷贝导致重复释放的问题和深拷贝、三法则的解决办法。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会写构造函数和析构函数，会用成员初始化列表；知道成员的初始化顺序由声明顺序决定；理解对象的构造和析构时机；理解 RAII 的思想；理解浅拷贝的危害，会写拷贝构造函数和拷贝赋值运算符；掌握三法则；会用 `explicit` 防止意外的隐式转换。
>
> **前置知识**：上一篇《类与对象》，本章《动态内存》。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。

## 1. 构造函数与析构函数

上一篇的 `BankAccount` 必须先调用 `open()` 才能使用，如果忘了调用，对象就处于未初始化的状态。**构造函数**解决了这个问题：它在对象**创建的那一刻自动调用**，用来初始化对象。与之对应，**析构函数**在对象**销毁时自动调用**，用来清理资源。

```cpp
#include <iostream>
#include <string>

class BankAccount {
public:
    BankAccount() : owner_("匿名"), balance_(0)                    // 默认构造函数
    {
        std::cout << "  默认构造: " << owner_ << '\n';
    }
    BankAccount(const std::string& owner, double initial)           // 带参数的构造函数
        : owner_(owner), balance_(initial > 0 ? initial : 0)        // 成员初始化列表
    {
        std::cout << "  构造: " << owner_ << ", 余额 " << balance_ << '\n';
    }
    ~BankAccount()                                                   // 析构函数
    {
        std::cout << "  析构: " << owner_ << '\n';
    }
    double balance() const { return balance_; }
private:
    std::string owner_;
    double balance_;
};

int main()
{
    std::cout << "创建 a:\n";
    BankAccount a("Alice", 100);          // 创建时必须提供参数，不可能忘记初始化
    std::cout << "创建 b:\n";
    BankAccount b;                        // 调用默认构造函数
    std::cout << "创建 c（在一个代码块中）:\n";
    {
        BankAccount c("Carol", 50);
    }                                     // c 离开作用域，立即析构
    std::cout << "创建堆上的 d:\n";
    BankAccount* d = new BankAccount("Dave", 10);
    delete d;                             // delete 时析构
    std::cout << "main 结束:\n";
    return 0;                             // a、b 按照与创建相反的顺序析构
}
```

运行结果：

```
创建 a:
  构造: Alice, 余额 100
创建 b:
  默认构造: 匿名
创建 c（在一个代码块中）:
  构造: Carol, 余额 50
  析构: Carol
创建堆上的 d:
  构造: Dave, 余额 10
  析构: Dave
main 结束:
  析构: 匿名
  析构: Alice
```

| | 构造函数 | 析构函数 |
| --- | --- | --- |
| 名字 | 和类名相同 | `~` 加类名 |
| 返回类型 | 没有（连 `void` 都不写） | 没有 |
| 参数 | 可以有，可以重载多个 | 没有参数，只能有一个 |
| 调用时机 | 对象创建时 | 对象销毁时：局部对象离开作用域、`delete` 堆对象、程序结束（全局对象） |

从输出可以看出对象的生命周期：

- **局部对象**在离开所在的花括号时析构，`c` 在代码块结束时立刻析构了。
- **堆上的对象**在 `delete` 时析构。
- **同一作用域中的多个对象，析构顺序与构造顺序相反**：先构造 `a` 再构造 `b`，先析构 `b` 再析构 `a`。这保证了后创建的对象（可能依赖先创建的对象）先被销毁。

**没有参数的构造函数叫做默认构造函数**。如果一个类没有写任何构造函数，编译器会自动生成一个默认构造函数；**只要写了任何一个构造函数，编译器就不再自动生成**。

## 2. 成员初始化列表

构造函数参数列表后面、冒号开始的部分 `: owner_(owner), balance_(...)` 叫做**成员初始化列表**。也可以在函数体中赋值：

```cpp
BankAccount(const std::string& owner, double initial)
{
    owner_ = owner;          // 先默认构造一个空字符串，再赋值
    balance_ = initial;
}
```

但这两种写法并不相同：初始化列表是**初始化**，函数体中是**先默认初始化、再赋值**。对于 `std::string` 这样的成员，后者多做了一次无用功。更重要的是，有些成员**只能**在初始化列表中初始化：

```cpp
#include <string>
class Config {
public:
    Config(int id, std::string& log)
    {
        id_ = id;           // 错误：const 成员只能在初始化列表中初始化
        log_ = log;         // 错误：引用成员必须在初始化列表中绑定
    }
private:
    const int id_;
    std::string& log_;
};
```

```
error: constructor for 'Config' must explicitly initialize the const member 'id_'
error: constructor for 'Config' must explicitly initialize the reference member 'log_'
error: cannot assign to non-static data member 'id_' with const-qualified type 'const int'
```

`const` 成员、引用成员、没有默认构造函数的类类型成员，都必须在初始化列表中初始化。**规则：一律使用初始化列表。**

### 2.1 陷阱：初始化的顺序

```cpp
#include <iostream>

class Range {
public:
    Range(int first, int size) : size_(size), last_(first + size_ - 1)   // 想先算 size_，再用它算 last_
    {
        std::cout << "size_ = " << size_ << ", last_ = " << last_ << '\n';
    }
private:
    int last_;      // 但 last_ 声明在前，所以先初始化 last_，此时 size_ 还没有值
    int size_;
};

int main()
{
    Range r(10, 5);   // 期望 last_ = 14
    return 0;
}
```

编译器给出了两条警告：

```
warning: field 'size_' will be initialized after field 'last_' [-Wreorder-ctor]
warning: field 'size_' is uninitialized when used here [-Wuninitialized]
```

运行结果：

```
size_ = 5, last_ = -270991135
```

**成员的初始化顺序由它们在类中声明的顺序决定，与初始化列表中书写的顺序无关。** `last_` 声明在前，所以先初始化 `last_`，这时 `size_` 还是一个随机值，`last_` 就得到了一个垃圾值。

规则：初始化列表的顺序和成员的声明顺序保持一致，并且尽量不要用一个成员去初始化另一个成员（这里直接写 `last_(first + size - 1)`，用参数 `size` 而不是成员 `size_` 即可）。开启 `-Wall` 后编译器会发出警告，**不要忽略警告**。

## 3. RAII：用对象管理资源

析构函数**一定会被调用**：无论函数是正常结束、提前 `return`，还是抛出了异常。利用这一点，可以把资源的释放交给析构函数：

```cpp
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>

// RAII：构造时获取资源，析构时释放资源
class File {
public:
    File(const std::string& path, const char* mode) : fp_(std::fopen(path.c_str(), mode))
    {
        if (!fp_) throw std::runtime_error("无法打开 " + path);
        std::cout << "  打开 " << path << '\n';
    }
    ~File()
    {
        std::fclose(fp_);
        std::cout << "  关闭文件\n";
    }
    void write(const std::string& s) { std::fputs(s.c_str(), fp_); }
private:
    std::FILE* fp_;
};

void save(const std::string& data, bool fail)
{
    File f("raii_demo.txt", "w");
    f.write(data);
    if (fail) throw std::runtime_error("写入过程中出错");   // 提前离开函数
    f.write("\n完成\n");
}   // 无论正常结束还是抛出异常，f 的析构函数都会关闭文件

int main()
{
    std::cout << "正常保存:\n";
    save("hello", false);
    std::cout << "保存时出错:\n";
    try {
        save("hello", true);
    } catch (const std::exception& e) {
        std::cout << "  捕获异常: " << e.what() << '\n';
    }
    std::remove("raii_demo.txt");
    return 0;
}
```

运行结果：

```
正常保存:
  打开 raii_demo.txt
  关闭文件
保存时出错:
  打开 raii_demo.txt
  关闭文件
  捕获异常: 写入过程中出错
```

第二次调用中途抛出了异常，`save` 函数提前结束，但文件仍然被正确关闭了：**「关闭文件」打印在「捕获异常」之前**，说明在异常传播出函数的过程中，局部对象 `f` 已经被析构了。

这种技术叫做 **RAII**（Resource Acquisition Is Initialization，资源获取即初始化）：**在构造函数中获取资源，在析构函数中释放资源**，资源的生命周期与对象绑定。它是 C++ 管理资源最核心的思想，`std::string`、`std::vector`、`std::fstream`、智能指针、`std::lock_guard` 都是 RAII 的应用。用了 RAII，就不需要在每个 `return` 前、每个可能抛出异常的地方手动释放资源。

## 4. 拷贝：浅拷贝与深拷贝

### 4.1 编译器生成的拷贝：浅拷贝

用一个对象初始化另一个对象（`MyString b = a;`）时，会调用**拷贝构造函数**。如果没有自己写，编译器会自动生成一个，它**逐个复制每个成员**。对于指针成员，复制的只是指针的值（地址），而不是指针指向的内容，这叫**浅拷贝**：

```cpp
#include <cstring>
#include <iostream>

// 自己管理一块动态内存的字符串类（没有自定义拷贝）
class MyString {
public:
    MyString(const char* s) : data_(new char[std::strlen(s) + 1]) { std::strcpy(data_, s); }
    ~MyString() { delete[] data_; }
    const char* c_str() const { return data_; }
    void setFirst(char c) { data_[0] = c; }
private:
    char* data_;
};

int main()
{
    MyString a("hello");
    MyString b = a;                 // 编译器生成的拷贝构造函数：只复制指针（浅拷贝）
    b.setFirst('J');
    std::cout << "a = " << a.c_str() << ", b = " << b.c_str() << '\n';   // a 也被改了！
    return 0;                       // a、b 析构时都 delete[] 同一块内存：重复释放
}
```

用 AddressSanitizer 运行：

```
a = Jello, b = Jello
ERROR: AddressSanitizer: attempting double-free
```

不开 ASan 直接运行，程序在结束时崩溃，退出码 133（macOS 的内存分配器检测到重复释放后主动终止了程序）。

两个问题：

1. **修改 `b`，`a` 也变了**：`a.data_` 和 `b.data_` 指向同一块内存。
2. **重复释放**：`a`、`b` 析构时都会 `delete[]` 这块内存。

```
浅拷贝：                     深拷贝：
 a.data_ ──┐                  a.data_ ──> "hello"
           ├──> "hello"
 b.data_ ──┘                  b.data_ ──> "hello"（独立的一份）
```

### 4.2 自己实现深拷贝

```cpp
#include <cstring>
#include <iostream>
#include <utility>

class MyString {
public:
    MyString(const char* s = "") : data_(new char[std::strlen(s) + 1]) { std::strcpy(data_, s); }

    ~MyString() { delete[] data_; }

    // 拷贝构造函数：深拷贝，分配新内存并复制内容
    MyString(const MyString& other) : data_(new char[std::strlen(other.data_) + 1])
    {
        std::strcpy(data_, other.data_);
        std::cout << "  [拷贝构造]\n";
    }

    // 拷贝赋值运算符：先复制新内容，再释放旧内存
    MyString& operator=(const MyString& other)
    {
        std::cout << "  [拷贝赋值]\n";
        if (this != &other) {                                  // 处理自己给自己赋值
            char* fresh = new char[std::strlen(other.data_) + 1];
            std::strcpy(fresh, other.data_);
            delete[] data_;
            data_ = fresh;
        }
        return *this;
    }

    const char* c_str() const { return data_; }
    void setFirst(char c) { data_[0] = c; }
private:
    char* data_;
};

int main()
{
    MyString a("hello");
    std::cout << "MyString b = a:\n";
    MyString b = a;
    b.setFirst('J');
    std::cout << "a = " << a.c_str() << ", b = " << b.c_str() << '\n';

    std::cout << "MyString c; c = a:\n";
    MyString c;
    c = a;
    std::cout << "c = " << c.c_str() << '\n';

    std::cout << "a = a（自赋值）:\n";
    a = a;
    std::cout << "a = " << a.c_str() << '\n';
    return 0;
}
```

运行结果：

```
MyString b = a:
  [拷贝构造]
a = hello, b = Jello
MyString c; c = a:
  [拷贝赋值]
c = hello
a = a（自赋值）:
  [拷贝赋值]
a = hello
```

用 AddressSanitizer 运行，没有任何错误。（`a = a;` 这一行，编译器会警告「把变量赋值给自己」，这里是有意演示的。）

需要区分两个函数：

| | 拷贝构造函数 `T(const T&)` | 拷贝赋值运算符 `T& operator=(const T&)` |
| --- | --- | --- |
| 何时调用 | **创建新对象**时，用另一个对象初始化它：`T b = a;`、`T b(a);`、按值传参 | **已经存在的对象**被赋值：`c = a;` |
| 要做什么 | 分配内存，复制内容 | 复制新内容，**释放旧内容**，处理自赋值 |

拷贝赋值的写法有讲究：

1. **检查自赋值** `if (this != &other)`：如果不检查，`a = a` 时先 `delete[] data_`，再从已经释放的 `other.data_`（就是自己的 `data_`）复制，就出错了。
2. **先分配新内存，再释放旧内存**：如果先释放旧内存，而 `new` 抛出了异常，对象就留下了一个悬空的指针。先分配好新内存，确认成功后再释放旧的，对象在任何时刻都是完整的。
3. **返回 `*this` 的引用**：支持 `a = b = c` 这样的连续赋值。

### 4.3 三法则

如果一个类需要自己写**析构函数、拷贝构造函数、拷贝赋值运算符**中的任何一个，那么通常三个都需要自己写。这叫**三法则**（rule of three）。

道理很简单：需要自己写析构函数，说明类直接管理着某种资源（比如上面的 `new char[]`）；编译器生成的拷贝只会复制指针，必然出问题，所以拷贝构造和拷贝赋值也必须自己写，或者禁止拷贝。

C++11 增加了移动语义，三法则扩展为**五法则**，见 C++11 章节的《右值引用与移动语义》。不过，**最好的做法是根本不需要写这些函数**：上面的 `char* data_` 换成 `std::string data_`，编译器生成的拷贝、赋值、析构就全部是正确的。这叫**零法则**：自己写的普通类不要直接管理资源，把资源交给 `std::string`、`std::vector`、智能指针这样已经正确实现了 RAII 的类。

## 5. explicit：防止隐式转换

只有一个参数的构造函数，会被编译器当作**类型转换**的规则：

```cpp
#include <iostream>
#include <vector>

class Buffer {
public:
    Buffer(int size) : data_(size) { std::cout << "  创建了大小为 " << size << " 的 Buffer\n"; }
    int size() const { return (int)data_.size(); }
private:
    std::vector<char> data_;
};

class SafeBuffer {
public:
    explicit SafeBuffer(int size) : data_(size) {}
    int size() const { return (int)data_.size(); }
private:
    std::vector<char> data_;
};

void process(const Buffer& b) { std::cout << "  处理 " << b.size() << " 字节\n"; }
void processSafe(const SafeBuffer& b) { std::cout << "  处理 " << b.size() << " 字节\n"; }

int main()
{
    std::cout << "process(1024):\n";
    process(1024);              // int 被悄悄地转换成了 Buffer
    Buffer b = 64;              // 看起来像是把整数赋给了 Buffer
    std::cout << "processSafe(SafeBuffer(1024)):\n";
    processSafe(SafeBuffer(1024));
    // processSafe(1024);       // 错误：explicit 构造函数不能用于隐式转换
    return b.size() == 64 ? 0 : 1;
}
```

运行结果：

```
process(1024):
  创建了大小为 1024 的 Buffer
  处理 1024 字节
  创建了大小为 64 的 Buffer
processSafe(SafeBuffer(1024)):
  处理 1024 字节
```

`process(1024)` 能编译，因为编译器用 `Buffer(int)` 构造函数把 `1024` 隐式转换成了一个临时的 `Buffer`。这通常不是你想要的：调用者可能以为 `process` 接收的是一个整数。加上 **`explicit`** 后，这种隐式转换被禁止：

```
error: no matching function for call to 'processSafe'
error: no viable conversion from 'int' to 'SafeBuffer'
```

**规则：只有一个参数的构造函数，除非确实希望支持隐式转换（比如 `std::string` 可以从 `const char*` 转换），否则都加上 `explicit`。**

## 6. 常见错误

1. **初始化列表的顺序与声明顺序不一致**，并且用一个成员初始化另一个成员。
2. **类管理着资源，却没有写拷贝构造和拷贝赋值**：浅拷贝导致重复释放。
3. **拷贝赋值中忘记处理自赋值，或者先释放旧内存再分配新内存**。
4. **写了带参数的构造函数后，以为默认构造函数还在**：`BankAccount b;` 会编译失败，需要自己写一个默认构造函数，或者写 `BankAccount() = default;`。
5. **在析构函数中抛出异常**：析构函数可能在异常传播的过程中被调用，此时再抛出异常，程序会直接终止。析构函数应该保证不抛出异常。

## 7. 小结

- 构造函数在对象创建时自动调用，保证对象一开始就是合法的；析构函数在对象销毁时自动调用
- 局部对象离开作用域时析构，析构顺序与构造顺序相反
- 一律使用成员初始化列表；成员按**声明顺序**初始化，与列表的书写顺序无关
- **RAII**：构造时获取资源、析构时释放资源，无论如何离开作用域都不会泄漏
- 编译器生成的拷贝是**浅拷贝**，类中有指向动态内存的指针时会导致数据共享和重复释放；需要自己实现深拷贝
- **三法则**：析构、拷贝构造、拷贝赋值，要么都不写，要么都写；更好的做法是用 `string`、`vector` 等管理资源（零法则）
- 单参数构造函数加 `explicit`，防止意外的隐式转换

## 8. 练习

1. 给 `MyString` 增加一个 `size()` 成员函数和一个 `append(const char* s)` 成员函数，注意 `append` 需要重新分配更大的内存。
2. 下面的类有什么问题？如何用零法则修改？

   ```cpp
   class IntArray {
   public:
       explicit IntArray(int n) : data_(new int[n]), size_(n) {}
       ~IntArray() { delete[] data_; }
       int& operator[](int i) { return data_[i]; }
   private:
       int* data_;
       int size_;
   };
   ```

3. 写一个 `Timer` 类，构造时记录当前时间，析构时打印从构造到析构经过了多少毫秒。用它测量一个函数的运行时间。

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   std::size_t size() const { return std::strlen(data_); }

   void append(const char* s)
   {
       std::size_t oldLen = std::strlen(data_), addLen = std::strlen(s);
       char* fresh = new char[oldLen + addLen + 1];
       std::strcpy(fresh, data_);
       std::strcpy(fresh + oldLen, s);
       delete[] data_;
       data_ = fresh;
   }
   ```

2. 有析构函数，却没有自定义拷贝构造和拷贝赋值，违反了三法则：拷贝 `IntArray` 会导致浅拷贝和重复释放。用 `std::vector<int>` 代替手动管理的数组，析构、拷贝都不用写了：

   ```cpp
   class IntArray {
   public:
       explicit IntArray(int n) : data_(n) {}
       int& operator[](int i) { return data_[i]; }
   private:
       std::vector<int> data_;
   };
   ```

3. 这是 RAII 思想的一个小应用：

   ```cpp
   #include <chrono>

   class Timer {
   public:
       explicit Timer(std::string name) : name_(std::move(name)), start_(std::chrono::steady_clock::now()) {}
       ~Timer()
       {
           auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
           std::cout << name_ << " 耗时 " << ms << " ms\n";
       }
   private:
       std::string name_;
       std::chrono::steady_clock::time_point start_;
   };

   void work()
   {
       Timer t("work");          // 函数结束时自动打印耗时
       // ……
   }
   ```

</details>
