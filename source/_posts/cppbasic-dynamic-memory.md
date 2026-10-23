---
title: 动态内存：new 与 delete
date: 2026-10-23 09:40:00
updated: 2026-10-23 09:40:00
categories:
  - C/C++
  - C++基础
tags:
  - C++基础
  - 内存管理
order: 3
description: 栈和堆有什么区别？new/delete 和 malloc/free 有什么不同？本文讲清楚动态内存的用法，并把五种最常见的内存错误（泄漏、重复释放、new[]/delete 混用、释放后使用、越界）逐一复现出来，演示如何用 AddressSanitizer 和 leaks 工具找到它们。
disableNunjucks: true
---

> **本文目标**：理解栈和堆的区别；会用 `new`/`delete`、`new[]`/`delete[]` 分配和释放内存；知道 `new` 和 `malloc` 的区别；认识五种常见的内存错误，会用工具检测它们；知道现代 C++ 中为什么很少直接写 `new`。
>
> **前置知识**：C 语言的指针和 `malloc`/`free`。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。

## 1. 栈与堆

程序运行时，变量主要存放在两个地方：

| | 栈（stack） | 堆（heap） |
| --- | --- | --- |
| 存放什么 | 局部变量、函数参数 | 用 `new`（或 `malloc`）申请的内存 |
| 何时释放 | 离开作用域时**自动**释放 | 必须**手动** `delete`（或 `free`） |
| 大小 | 很小，通常 8MB（Linux、macOS 的默认值） | 很大，受限于系统可用内存 |
| 速度 | 分配和释放极快（移动栈指针） | 较慢（需要内存分配器查找空闲块） |
| 何时使用 | 大小固定、生命周期局限在函数内的数据 | 大小运行时才确定、很大、或需要在函数返回后继续存在的数据 |

栈空间有多小？看一个实验：

```cpp
#include <iostream>
#include <vector>

void bigOnStack()
{
    int onStack[10'000'000];                   // 40MB 放在栈上：栈溢出
    onStack[0] = 1;
    std::cout << onStack[0] << '\n';
}

int main(int argc, char**)
{
    std::vector<int> onHeap(10'000'000);       // 40MB 放在堆上：没问题
    std::cout << "堆上分配 40MB 成功, size = " << onHeap.size() << '\n';
    if (argc > 1) bigOnStack();
    std::cout << "程序正常结束\n";
    return 0;
}
```

不带参数运行，40MB 的数据放在堆上（`std::vector` 内部使用堆内存），一切正常：

```
堆上分配 40MB 成功, size = 10000000
程序正常结束
```

带一个参数运行，调用 `bigOnStack()`，在栈上定义 40MB 的数组：程序直接崩溃，退出码 139（即 128 + 11，被信号 11 `SIGSEGV`，也就是「段错误」终止）。`ulimit -s` 显示这台机器的栈大小只有 8176KB，约 8MB，装不下 40MB 的数组。

> 一个容易踩的坑：最初我把这个数组直接写在 `main` 中的 `if` 语句里，结果**不带参数时程序也崩溃了**。因为编译器在进入函数时，就为函数中所有的局部变量一次性分配好栈空间，不管它们所在的代码块有没有执行。所以示例中把它放到了单独的函数里。

## 2. new 和 delete

```cpp
#include <cstdlib>
#include <iostream>
#include <string>

struct Student {
    std::string name;
    int age;
    Student(std::string n, int a) : name(std::move(n)), age(a) { std::cout << "  构造 " << name << '\n'; }
    ~Student() { std::cout << "  析构 " << name << '\n'; }
};

int main()
{
    // 1. 单个对象
    int* p = new int(42);                 // 分配并初始化为 42
    std::cout << "*p = " << *p << '\n';
    delete p;                             // 释放
    p = nullptr;                          // 好习惯：释放后置空

    // 2. 数组
    int n = 5;
    int* arr = new int[n]{1, 2, 3};       // 长度可以是运行期的变量；未给出的元素为 0
    for (int i = 0; i < n; ++i) std::cout << arr[i] << ' ';
    std::cout << '\n';
    delete[] arr;                         // 数组用 delete[]

    // 3. new 会调用构造函数，delete 会调用析构函数
    std::cout << "new Student:\n";
    Student* s = new Student("Alice", 20);
    std::cout << "  " << s->name << " 今年 " << s->age << " 岁\n";
    delete s;

    // 4. malloc 只分配内存，不调用构造函数
    std::cout << "malloc Student:\n";
    Student* m = static_cast<Student*>(std::malloc(sizeof(Student)));   // 没有任何输出：name 根本没有被构造
    std::cout << "  malloc 完成（没有调用构造函数）\n";
    std::free(m);                        // 也不会调用析构函数
    return 0;
}
```

运行结果：

```
*p = 42
1 2 3 0 0 
new Student:
  构造 Alice
  Alice 今年 20 岁
  析构 Alice
malloc Student:
  malloc 完成（没有调用构造函数）
```

（构造函数和析构函数会在《构造与析构》中详细介绍，这里只需知道它们分别在对象「诞生」和「消亡」时自动调用。）

用法总结：

| 分配 | 释放 |
| --- | --- |
| `T* p = new T;` / `new T(初始值)` | `delete p;` |
| `T* p = new T[n];` / `new T[n]{...}` | `delete[] p;` |

### 2.1 new 和 malloc 的区别

| | `new` / `delete` | `malloc` / `free` |
| --- | --- | --- |
| 是什么 | C++ 运算符 | C 标准库函数 |
| 返回类型 | 正确类型的指针，不需要强制转换 | `void*`，需要强制转换 |
| 大小 | 编译器自动计算 | 自己写 `sizeof` |
| **构造 / 析构** | **自动调用构造函数和析构函数** | **不调用** |
| 失败时 | 抛出 `std::bad_alloc` 异常 | 返回 `NULL` |

最关键的区别是第四行。上面的例子中，`malloc` 出来的 `Student` 中，`name` 这个 `std::string` 根本没有被构造，它的内部数据是随机的，如果对它赋值，程序很可能崩溃。**在 C++ 中，不要用 `malloc` 创建对象。**

### 2.2 分配失败

```cpp
#include <iostream>
#include <new>

int main()
{
    std::size_t huge = std::size_t(1) << 60;      // 1 EB，不可能分配成功

    try {
        char* p = new char[huge];                 // 失败时抛出 std::bad_alloc
        delete[] p;
    } catch (const std::bad_alloc& e) {
        std::cout << "new 失败，抛出异常: " << e.what() << '\n';
    }

    char* q = new (std::nothrow) char[huge];      // nothrow 版本：失败时返回 nullptr
    if (q == nullptr) std::cout << "new(std::nothrow) 失败，返回 nullptr\n";
    delete[] q;                                   // 对 nullptr 调用 delete 是安全的，什么也不做
    return 0;
}
```

运行结果：

```
new 失败，抛出异常: std::bad_alloc
new(std::nothrow) 失败，返回 nullptr
```

`new` 失败时**抛出异常**，所以不需要像 `malloc` 那样每次都检查返回值是否为空（异常处理会在《异常处理》中介绍）。确实需要检查返回值的场合，可以用 `new (std::nothrow)`。另外，**对空指针调用 `delete` 是安全的**，什么也不做，所以不需要写 `if (p) delete p;`。

## 3. 五种常见的内存错误

手动管理内存非常容易出错。下面的程序包含了五种最常见的错误，通过命令行参数选择执行哪一个：

```cpp
#include <cstring>
#include <iostream>

void leak()
{
    int* p = new int[100];       // 分配了，却忘记释放
    p[0] = 1;
}

void doubleFree()
{
    int* p = new int(5);
    delete p;
    delete p;                    // 重复释放
}

void mismatch()
{
    int* arr = new int[10];
    delete arr;                  // 用 new[] 分配，却用 delete 释放
}

void useAfterFree()
{
    int* p = new int(42);
    delete p;
    std::cout << *p << '\n';     // 释放之后继续使用
}

void overflow()
{
    int* arr = new int[5];
    arr[5] = 1;                  // 越界：合法下标是 0~4
    delete[] arr;
}

int main(int argc, char* argv[])
{
    if (argc < 2) return 0;
    switch (argv[1][0]) {
        case 'l': leak(); break;
        case 'd': doubleFree(); break;
        case 'm': mismatch(); break;
        case 'u': useAfterFree(); break;
        case 'o': overflow(); break;
    }
    std::cout << "程序正常结束\n";
    return 0;
}
```

### 3.1 不用工具时，什么也看不出来

正常编译，分别运行内存泄漏（`l`）、释放后使用（`u`）、越界（`o`）三个版本：

```
l: 程序正常结束
u: 5
o: 程序正常结束
```

三个有严重 bug 的程序都「正常」地运行完了。最诡异的是释放后使用：`*p` 本来是 42，释放后读出来的却是 5，而程序没有任何报错。**这就是未定义行为最危险的地方：程序可能看起来一切正常，错误在完全无关的地方、在某个特定的时刻才爆发出来。**

### 3.2 用 AddressSanitizer 检测

编译时加上 `-fsanitize=address -g`，再运行：

```bash
g++ -std=c++17 -g -fsanitize=address errors.cpp -o errors
./errors d
```

| 错误 | ASan 的报告 |
| --- | --- |
| 重复释放（`d`） | `ERROR: AddressSanitizer: attempting double-free` |
| `new[]` 配 `delete`（`m`） | `ERROR: AddressSanitizer: alloc-dealloc-mismatch (operator new [] vs operator delete)` |
| 释放后使用（`u`） | `ERROR: AddressSanitizer: heap-use-after-free` |
| 越界（`o`） | `ERROR: AddressSanitizer: heap-buffer-overflow` |

每份报告中都会列出出错的代码行、内存是在哪一行分配的、在哪一行释放的，定位问题非常方便。

两点说明：

- 在 macOS 上，`new[]` / `delete` 混用的检测默认是关闭的，需要设置环境变量 `ASAN_OPTIONS=alloc_dealloc_mismatch=1`。不过编译器在编译时就已经发出了警告：`'delete' applied to a pointer that was allocated with 'new[]'; did you mean 'delete[]'? [-Wmismatched-new-delete]`。
- 在 Linux 上，ASan 还会在程序结束时自动报告内存泄漏（`ERROR: LeakSanitizer: detected memory leaks`）。**macOS 上的 ASan 不支持泄漏检测**，设置 `detect_leaks=1` 会直接报错 `detect_leaks is not supported on this platform`。

### 3.3 在 macOS 上检测内存泄漏

macOS 自带了 `leaks` 命令：

```bash
leaks --atExit -- ./errors l
```

```
Process 36760: 1 leak for 448 total leaked bytes.
```

它发现了 1 处泄漏。泄漏的是 `new int[100]` 分配的 400 字节，报告中是 448 字节，多出来的是内存分配器为了对齐而多分配的部分。Linux 上可以用 ASan 或 Valgrind（`valgrind --leak-check=full ./errors l`）。

## 4. 现代 C++：尽量不直接写 new

从上面的例子可以看出，手动管理内存需要保证：每个 `new` 都有且只有一个对应的 `delete`，数组用 `delete[]`，释放后不再使用……在复杂的程序中，尤其是有多个 `return` 或抛出异常时，几乎不可能做到万无一失。

现代 C++ 的做法是**让对象自己管理内存**：

| 需求 | 推荐 | 而不是 |
| --- | --- | --- |
| 动态数组 | `std::vector<T>` | `new T[n]` |
| 字符串 | `std::string` | `new char[n]` |
| 单个动态对象 | `std::unique_ptr<T>`、`std::make_unique<T>()` | `new T` |
| 共享的动态对象 | `std::shared_ptr<T>`、`std::make_shared<T>()` | 多处持有同一个 `T*` |

它们都在析构函数中自动释放内存，离开作用域、抛出异常时都不会泄漏。这种技术叫做 RAII，会在《构造与析构》中详细介绍；智能指针见 C++11 章节的《智能指针》。

**那为什么还要学 `new` 和 `delete`？** 因为 `vector`、`string`、智能指针的内部都是用它们实现的。理解了手动管理内存的困难，才能真正理解这些工具的价值；自己实现一个容器类时，也必须正确地使用它们。

## 5. 常见错误

1. **`new` 和 `delete`、`new[]` 和 `delete[]`、`malloc` 和 `free` 不配对**：三组必须严格对应，不能混用。
2. **释放后不置空**：之后再次释放或访问，就是重复释放或释放后使用。`delete p; p = nullptr;` 能避免一部分问题。
3. **多个指针指向同一块内存**：一个释放后，其他指针全部悬空，而且无法知道。
4. **在函数中 `new`，却在异常或提前 `return` 时忘记 `delete`**。
5. **把大数组放在栈上**：超过几百 KB 的数据应该放在堆上，用 `std::vector`。

## 6. 小结

- 局部变量在栈上，离开作用域自动释放，但栈空间很小（约 8MB）；`new` 的内存在堆上，必须手动释放
- `new` / `delete` 用于单个对象，`new[]` / `delete[]` 用于数组，必须配对
- `new` 会调用构造函数，失败时抛出 `std::bad_alloc`；`malloc` 不会调用构造函数，C++ 中不要用它创建对象
- 五种常见错误：泄漏、重复释放、`new[]`/`delete` 混用、释放后使用、越界；不用工具时很可能看不出来
- 用 `-fsanitize=address` 检测内存错误；macOS 上用 `leaks --atExit` 检测泄漏
- 现代 C++ 中优先使用 `vector`、`string`、智能指针，而不是直接 `new`

## 7. 练习

1. 写一个函数 `int* makeSquares(int n)`，返回一个包含 `1², 2², ..., n²` 的动态数组。调用者应该如何释放它？再用 `std::vector<int>` 改写，比较两种写法。
2. 下面的代码有几处内存错误？

   ```cpp
   char* copyString(const char* s)
   {
       char* buf = new char[std::strlen(s)];
       std::strcpy(buf, s);
       return buf;
   }
   int main()
   {
       char* a = copyString("hello");
       char* b = a;
       delete a;
       delete[] b;
   }
   ```

3. 为什么说「对 `nullptr` 调用 `delete` 是安全的」这一点很重要？

<details>
<summary>点击查看答案</summary>

1. 参考代码：

   ```cpp
   int* makeSquares(int n)
   {
       int* arr = new int[n];
       for (int i = 0; i < n; ++i) arr[i] = (i + 1) * (i + 1);
       return arr;
   }
   int* sq = makeSquares(5);
   // ……
   delete[] sq;                   // 调用者必须记得用 delete[] 释放

   std::vector<int> makeSquaresVec(int n)
   {
       std::vector<int> v(n);
       for (int i = 0; i < n; ++i) v[i] = (i + 1) * (i + 1);
       return v;                  // 不用担心释放，也不会有拷贝开销（返回值优化）
   }
   ```

   第一种写法中，「调用者必须用 `delete[]` 释放」这个约定只能写在注释里，编译器无法检查；`vector` 版本没有这个问题。

2. 三处：① `new char[std::strlen(s)]` 少分配了 1 个字节，没有给结尾的 `'\0'` 留位置，`strcpy` 会越界写入；② `delete a;` 用 `delete` 释放 `new[]` 分配的内存；③ `b` 和 `a` 指向同一块内存，`delete[] b;` 是重复释放。

3. 它让「释放后置空」的写法变得安全：`delete p; p = nullptr;` 之后，即使某处代码再次执行 `delete p;`，也不会出错。析构函数中也可以直接 `delete` 成员指针，不必先判断是否为空。

</details>
