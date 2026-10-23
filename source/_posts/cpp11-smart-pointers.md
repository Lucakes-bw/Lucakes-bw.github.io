---
title: 智能指针：unique_ptr、shared_ptr 与 weak_ptr
date: 2026-10-22 11:40:00
updated: 2026-10-22 11:40:00
categories:
  - C/C++
  - C++11
tags:
  - C++11
  - 智能指针
  - 内存管理
order: 9
description: 智能指针让对象在不再需要时自动释放，从根本上解决内存泄漏、重复释放和悬空指针。本文讲清楚三种智能指针的用法、控制块的原理、循环引用、make_shared 的优势和 enable_shared_from_this。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 RAII 思想；会用 `unique_ptr` 表达独占所有权，会用自定义删除器管理非内存资源；会用 `shared_ptr` 表达共享所有权，理解引用计数和控制块；会用 `weak_ptr` 打破循环引用；知道 `make_shared` 的好处；避开智能指针最常见的几个坑。
>
> **前置知识**：《右值引用与移动语义》。
>
> **编译环境**：`g++ -std=c++11` / `clang++ -std=c++11`，需要包含头文件 `<memory>`。

## 1. 裸指针的问题

用 `new` 申请的内存必须用 `delete` 释放。听起来简单，实际写代码时很难保证每条路径都正确释放：

```cpp
#include <iostream>
#include <memory>
#include <stdexcept>

struct Resource {
    explicit Resource(int id) : id(id) { std::cout << "  获取资源 " << id << '\n'; }
    ~Resource() { std::cout << "  释放资源 " << id << '\n'; }
    int id;
};

void mayThrow(bool fail)
{
    if (fail) throw std::runtime_error("出错了");
}

void useRaw(bool fail)
{
    Resource* r = new Resource(1);
    mayThrow(fail);          // 抛出异常时，下面的 delete 永远不会执行
    delete r;
}

void useSmart(bool fail)
{
    std::unique_ptr<Resource> r(new Resource(2));
    mayThrow(fail);          // 抛出异常时，r 的析构函数仍会被调用
}

int main()
{
    std::cout << "裸指针：\n";
    try { useRaw(true); } catch (const std::exception& e) { std::cout << "  捕获: " << e.what() << '\n'; }
    std::cout << "智能指针：\n";
    try { useSmart(true); } catch (const std::exception& e) { std::cout << "  捕获: " << e.what() << '\n'; }
    return 0;
}
```

运行结果：

```
裸指针：
  获取资源 1
  捕获: 出错了
智能指针：
  获取资源 2
  释放资源 2
  捕获: 出错了
```

资源 1 永远没有被释放：**内存泄漏**。除了异常，函数中间的 `return`、`break` 也会跳过 `delete`。裸指针还有另外两类问题：

- **重复释放**（double free）：两处代码都以为自己该负责 `delete`。
- **悬空指针**（dangling pointer）：对象已经被 `delete`，别处还拿着指向它的指针在用。

这些问题的根源在于，裸指针**无法表达「谁负责释放」**。

## 2. RAII：让析构函数负责释放

C++ 有一个可靠的机制：**对象离开作用域时，析构函数一定会被调用**，无论是正常结束、`return` 还是抛出异常。

**RAII**（Resource Acquisition Is Initialization，资源获取即初始化）就是利用这一点：**在构造函数中获取资源，在析构函数中释放资源**。资源的生命周期和对象绑定在一起，不需要手动释放，也不会忘记释放。

智能指针就是 RAII 在内存管理上的应用：它是一个类，内部保存裸指针，在析构函数中 `delete` 它。上面的 `useSmart` 抛出异常时，栈上的 `r` 被销毁，它的析构函数释放了资源 2。

C++11 提供了三种智能指针：

| 智能指针 | 所有权 | 能否拷贝 | 典型用途 |
| --- | --- | --- | --- |
| `std::unique_ptr` | **独占**：同一时间只有一个所有者 | 不能，只能移动 | 默认选择，绝大多数情况 |
| `std::shared_ptr` | **共享**：多个所有者，最后一个负责释放 | 能 | 确实需要多方共同拥有时 |
| `std::weak_ptr` | **不拥有**，只观察 | 能 | 配合 `shared_ptr`，打破循环引用、做缓存 |

## 3. unique_ptr：独占所有权

```cpp
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct Book {
    explicit Book(std::string t) : title(std::move(t)) { std::cout << "  创建《" << title << "》\n"; }
    ~Book() { std::cout << "  销毁《" << title << "》\n"; }
    std::string title;
};

// 工厂函数：返回 unique_ptr，所有权交给调用者
std::unique_ptr<Book> makeBook(const std::string& title)
{
    return std::unique_ptr<Book>(new Book(title));
}

int main()
{
    std::cout << "1. 基本用法\n";
    std::unique_ptr<Book> p1(new Book("C++ Primer"));
    std::cout << "  p1->title = " << p1->title << ", (*p1).title = " << (*p1).title << '\n';

    std::cout << "2. 转移所有权\n";
    // std::unique_ptr<Book> p2 = p1;          // 错误：不能拷贝
    std::unique_ptr<Book> p2 = std::move(p1);  // 可以移动
    std::cout << "  p1 " << (p1 ? "非空" : "为空") << ", p2 " << (p2 ? "非空" : "为空") << '\n';

    std::cout << "3. 放进容器\n";
    std::vector<std::unique_ptr<Book>> shelf;
    shelf.push_back(makeBook("Effective C++"));
    shelf.push_back(std::move(p2));
    std::cout << "  书架上有 " << shelf.size() << " 本书\n";

    std::cout << "4. reset：释放当前对象，可以换一个新的\n";
    std::unique_ptr<Book> p3(new Book("旧书"));
    p3.reset(new Book("新书"));
    p3.reset();                                 // 只释放，变为空
    std::cout << "  p3 " << (p3 ? "非空" : "为空") << '\n';

    std::cout << "5. 数组版本\n";
    std::unique_ptr<int[]> arr(new int[5]{1, 2, 3, 4, 5});   // 析构时调用 delete[]
    std::cout << "  arr[2] = " << arr[2] << '\n';

    std::cout << "6. 自定义删除器：管理 C 语言的 FILE*\n";
    {
        std::unique_ptr<std::FILE, int (*)(std::FILE*)> file(std::fopen("demo.txt", "w"), std::fclose);
        if (file) std::fputs("hello\n", file.get());   // get() 取出裸指针，交给 C 函数
    }   // 离开作用域时自动 fclose
    std::cout << "  文件已自动关闭\n";

    std::cout << "main 结束\n";
    return 0;
}
```

运行结果：

```
1. 基本用法
  创建《C++ Primer》
  p1->title = C++ Primer, (*p1).title = C++ Primer
2. 转移所有权
  p1 为空, p2 非空
3. 放进容器
  创建《Effective C++》
  书架上有 2 本书
4. reset：释放当前对象，可以换一个新的
  创建《旧书》
  创建《新书》
  销毁《旧书》
  销毁《新书》
  p3 为空
5. 数组版本
  arr[2] = 3
6. 自定义删除器：管理 C 语言的 FILE*
  文件已自动关闭
main 结束
  销毁《C++ Primer》
  销毁《Effective C++》
```

逐条说明：

1. **用法和裸指针一样**：`->`、`*`，可以用 `if (p)` 判断是否为空。
2. **不能拷贝，只能移动**。如果允许拷贝，就会有两个 `unique_ptr` 指向同一个对象，各自析构时都 `delete`，造成重复释放。尝试拷贝时编译器会报错：`call to implicitly-deleted copy constructor of 'std::unique_ptr<int>'`。用 `std::move` 可以把所有权转移给另一个 `unique_ptr`，原来的变为空。
3. **可以放进容器**。容器扩容时会移动元素，没有问题。注意最后两本书是在 `main` 结束时，随着 `shelf` 销毁而被释放的，**不需要写任何 `delete`**。
4. **`reset(新指针)`** 先释放旧对象，再接管新对象；**`reset()`** 只释放。还有一个 `release()`，它放弃所有权并返回裸指针，**但不释放对象**，调用者要自己负责 `delete`，一般只在和老代码交互时使用。
5. **数组版本** `unique_ptr<T[]>` 析构时调用 `delete[]`，并且提供 `operator[]`。不过通常直接用 `std::vector` 更方便。
6. **自定义删除器**：第二个模板参数是删除器的类型，构造时传入删除器。这样 `unique_ptr` 就不只能管理内存，还能管理文件、网络连接、锁等**任何需要释放的资源**。

### 3.1 make_unique

C++14 增加了 `std::make_unique<T>(参数...)`，写起来更简洁，也更安全（原因见第 7.1 节）：

```cpp
auto p = std::make_unique<Book>("C++ Primer");   // C++14
```

C++11 中没有它，可以自己实现一个（用到了可变参数模板和完美转发，后面会讲）：

```cpp
template <typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args)
{
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
```

### 3.2 在函数参数和返回值中表达所有权

`unique_ptr` 最大的价值之一，是让**函数签名说明所有权**：

| 写法 | 含义 |
| --- | --- |
| `std::unique_ptr<T> create();` | 返回新对象，所有权交给调用者 |
| `void take(std::unique_ptr<T> p);` | 函数接管所有权，调用时必须 `std::move` |
| `void use(T& obj);` 或 `void use(T* obj);` | 只是使用，不涉及所有权 |

只是使用对象的函数，**不要**写成 `void use(const std::unique_ptr<T>& p)`，直接传引用或裸指针即可，这样函数不必关心调用者用什么方式管理这个对象。

## 4. shared_ptr：共享所有权

有时一个对象确实需要被多方共同拥有，而且事先无法确定谁最后使用它。比如多个界面元素共享同一张图片，最后一个不再使用它的界面元素负责释放。`shared_ptr` 用**引用计数**解决这个问题：

```cpp
#include <iostream>
#include <memory>
#include <string>

struct Image {
    explicit Image(std::string n) : name(std::move(n)) { std::cout << "  加载图片 " << name << '\n'; }
    ~Image() { std::cout << "  释放图片 " << name << '\n'; }
    std::string name;
};

int main()
{
    std::shared_ptr<Image> a = std::make_shared<Image>("logo.png");
    std::cout << "a 创建后 use_count = " << a.use_count() << '\n';
    {
        std::shared_ptr<Image> b = a;          // 拷贝：共享同一个对象，计数 +1
        std::shared_ptr<Image> c = a;
        std::cout << "b、c 拷贝后 use_count = " << a.use_count() << '\n';
    }                                          // b、c 销毁，计数 -2
    std::cout << "b、c 离开作用域后 use_count = " << a.use_count() << '\n';

    std::shared_ptr<Image> d = std::move(a);   // 移动：所有权转移，计数不变
    std::cout << "移动后 a " << (a ? "非空" : "为空") << ", d.use_count = " << d.use_count() << '\n';

    std::cout << "d.reset()：\n";
    d.reset();                                 // 最后一个 shared_ptr 放手，对象被释放
    std::cout << "main 结束\n";
    return 0;
}
```

运行结果：

```
  加载图片 logo.png
a 创建后 use_count = 1
b、c 拷贝后 use_count = 3
b、c 离开作用域后 use_count = 1
移动后 a 为空, d.use_count = 1
d.reset()：
  释放图片 logo.png
main 结束
```

规则：**拷贝一次，计数加 1；一个 `shared_ptr` 销毁或 `reset`，计数减 1；计数变成 0 时，释放对象。** 移动不改变计数，只是把所有权从一个 `shared_ptr` 转给另一个，所以移动比拷贝更高效。

### 4.1 控制块

计数保存在哪里？不能保存在 `shared_ptr` 对象里，因为多个 `shared_ptr` 必须看到同一个计数。答案是：第一次创建 `shared_ptr` 时，会在堆上额外分配一个**控制块**（control block），所有共享同一个对象的 `shared_ptr` 都指向它。

<figure class="diagram">
<svg viewBox="0 0 700 270" role="img" aria-label="shared_ptr 的内存布局：每个 shared_ptr 和 weak_ptr 都包含两个指针，一个指向对象，一个指向控制块；控制块中保存强引用计数、弱引用计数和删除器">
  <defs>
    <marker id="sp-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/>
    </marker>
  </defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="20" y="20" style="fill:var(--muted)">栈上</text>
    <text x="430" y="20" style="fill:var(--muted)">堆上</text>
    <!-- shared_ptr a -->
    <rect x="20" y="30" width="170" height="64" rx="6" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="30" y="48" style="fill:var(--accent)">shared_ptr a</text>
    <text x="30" y="68" style="fill:var(--fg)">对象指针</text>
    <text x="30" y="86" style="fill:var(--fg)">控制块指针</text>
    <!-- shared_ptr b -->
    <rect x="20" y="110" width="170" height="64" rx="6" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="30" y="128" style="fill:var(--accent)">shared_ptr b</text>
    <text x="30" y="148" style="fill:var(--fg)">对象指针</text>
    <text x="30" y="166" style="fill:var(--fg)">控制块指针</text>
    <!-- weak_ptr w -->
    <rect x="20" y="190" width="170" height="64" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-dasharray:4 3"/>
    <text x="30" y="208" style="fill:var(--muted)">weak_ptr w</text>
    <text x="30" y="228" style="fill:var(--fg)">对象指针</text>
    <text x="30" y="246" style="fill:var(--fg)">控制块指针</text>
    <!-- 对象 -->
    <rect x="430" y="30" width="250" height="60" rx="6" style="fill:var(--code-bg);stroke:var(--border-2)"/>
    <text x="555" y="57" text-anchor="middle" style="fill:var(--fg)">Image 对象</text>
    <text x="555" y="77" text-anchor="middle" style="fill:var(--muted)">logo.png</text>
    <!-- 控制块 -->
    <rect x="430" y="140" width="250" height="114" rx="6" style="fill:var(--code-bg);stroke:var(--warn)"/>
    <text x="445" y="162" style="fill:var(--warn)">控制块</text>
    <text x="445" y="188" style="fill:var(--fg)">强引用计数 use_count = 2</text>
    <text x="445" y="212" style="fill:var(--fg)">弱引用计数 = 1</text>
    <text x="445" y="236" style="fill:var(--muted)">删除器、分配器……</text>
    <!-- 箭头 -->
    <line x1="190" y1="64" x2="428" y2="55" style="stroke:var(--fg-2)" marker-end="url(#sp-arrow)"/>
    <line x1="190" y1="82" x2="428" y2="170" style="stroke:var(--fg-2)" marker-end="url(#sp-arrow)"/>
    <line x1="190" y1="144" x2="428" y2="62" style="stroke:var(--fg-2)" marker-end="url(#sp-arrow)"/>
    <line x1="190" y1="162" x2="428" y2="180" style="stroke:var(--fg-2)" marker-end="url(#sp-arrow)"/>
    <line x1="190" y1="224" x2="428" y2="70" style="stroke:var(--border-2);stroke-dasharray:4 3" marker-end="url(#sp-arrow)"/>
    <line x1="190" y1="242" x2="428" y2="210" style="stroke:var(--border-2);stroke-dasharray:4 3" marker-end="url(#sp-arrow)"/>
  </g>
</svg>
</figure>

- 每个 `shared_ptr` 内部有**两个指针**：一个指向对象，一个指向控制块。所以 `sizeof(shared_ptr<T>)` 在 64 位系统上是 16 字节，是裸指针的两倍。
- 控制块中有两个计数：**强引用计数**（有几个 `shared_ptr`）决定**对象**何时释放；**弱引用计数**（有几个 `weak_ptr`）和强引用计数一起决定**控制块本身**何时释放。
- 计数的增减是**原子操作**，所以多个线程同时拷贝、销毁指向同一对象的 `shared_ptr` 是安全的。但这也意味着 `shared_ptr` 的拷贝比普通指针慢，**能用 `unique_ptr` 就不要用 `shared_ptr`**，传参时能传引用就不要拷贝。注意线程安全的只是计数本身，多个线程通过 `shared_ptr` 访问**对象**时，仍然需要自己加锁。

### 4.2 make_shared

创建 `shared_ptr` 有两种方式。用自定义的 `operator new` 统计堆分配次数：

```cpp
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
static int allocations = 0;
void* operator new(std::size_t n)
{
    ++allocations;
    if (void* p = std::malloc(n)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
struct Widget { int data[4]; };
int main()
{
    allocations = 0;
    std::shared_ptr<Widget> a(new Widget());          // 先 new 对象，shared_ptr 再分配控制块
    std::printf("shared_ptr<Widget>(new Widget): 堆分配 %d 次\n", allocations);
    allocations = 0;
    auto b = std::make_shared<Widget>();              // 对象和控制块一次分配
    std::printf("make_shared<Widget>()        : 堆分配 %d 次\n", allocations);
    return 0;
}
```

运行结果：

```
shared_ptr<Widget>(new Widget): 堆分配 2 次
make_shared<Widget>()        : 堆分配 1 次
```

`make_shared` 把对象和控制块放在**同一块内存**里，只分配一次，速度更快，内存也更紧凑。**创建 `shared_ptr` 时优先使用 `std::make_shared`**（C++11 就有）。

## 5. 循环引用与 weak_ptr

引用计数有一个天生的缺陷：**循环引用**。

```cpp
#include <iostream>
#include <memory>
#include <string>

struct Child;
struct Parent {
    std::string name = "父";
    std::shared_ptr<Child> child;
    ~Parent() { std::cout << "  ~Parent\n"; }
};
struct Child {
    std::string name = "子";
    std::shared_ptr<Parent> parent;      // 强引用：形成循环
    ~Child() { std::cout << "  ~Child\n"; }
};

struct Child2;
struct Parent2 {
    std::shared_ptr<Child2> child;
    ~Parent2() { std::cout << "  ~Parent2\n"; }
};
struct Child2 {
    std::weak_ptr<Parent2> parent;       // 弱引用：不增加计数，打破循环
    ~Child2() { std::cout << "  ~Child2\n"; }
};

int main()
{
    std::cout << "循环引用：\n";
    {
        auto p = std::make_shared<Parent>();
        auto c = std::make_shared<Child>();
        p->child = c;
        c->parent = p;
        std::cout << "  p.use_count = " << p.use_count() << ", c.use_count = " << c.use_count() << '\n';
    }
    std::cout << "  （离开作用域，没有任何析构函数被调用：内存泄漏）\n";

    std::cout << "用 weak_ptr 打破循环：\n";
    {
        auto p = std::make_shared<Parent2>();
        auto c = std::make_shared<Child2>();
        p->child = c;
        c->parent = p;
        std::cout << "  p.use_count = " << p.use_count() << ", c.use_count = " << c.use_count() << '\n';
    }
    return 0;
}
```

运行结果：

```
循环引用：
  p.use_count = 2, c.use_count = 2
  （离开作用域，没有任何析构函数被调用：内存泄漏）
用 weak_ptr 打破循环：
  p.use_count = 1, c.use_count = 2
  ~Parent2
  ~Child2
```

分析第一种情况：离开作用域时，局部变量 `p` 和 `c` 被销毁，两个对象的计数各减 1，都变成 1。但 `Parent` 对象被 `Child` 对象里的 `parent` 指着，`Child` 对象被 `Parent` 对象里的 `child` 指着，**谁的计数都到不了 0**，两个对象永远不会被释放，而且再也没有任何办法访问它们。

解决办法是把其中一个方向改成 **`weak_ptr`**。`weak_ptr` 指向对象，但**不增加强引用计数**，所以不会阻止对象被释放。上例中 `Parent2` 的计数只有 1，局部变量 `p` 销毁后它就被释放了，它的成员 `child` 随之销毁，`Child2` 的计数也降到 0。

**经验：在父子、观察者等双向关系中，「拥有」的一方用 `shared_ptr`，「反向引用」的一方用 `weak_ptr`。**

### 5.1 weak_ptr 的用法

`weak_ptr` 不能直接访问对象（没有 `->` 和 `*`），因为对象随时可能已经被释放。使用前必须调用 `lock()` 把它**临时升级**成 `shared_ptr`：

```cpp
#include <iostream>
#include <memory>

int main()
{
    std::weak_ptr<int> watcher;
    {
        auto sp = std::make_shared<int>(42);
        watcher = sp;                                  // 观察，但不拥有
        std::cout << "use_count = " << sp.use_count() << ", expired = " << watcher.expired() << '\n';

        if (std::shared_ptr<int> locked = watcher.lock())   // 临时获得所有权
            std::cout << "对象还在，值为 " << *locked << '\n';
    }
    std::cout << "sp 销毁后 expired = " << watcher.expired() << '\n';
    if (std::shared_ptr<int> locked = watcher.lock())
        std::cout << "对象还在\n";
    else
        std::cout << "lock() 返回空指针，对象已经不在了\n";
    return 0;
}
```

运行结果：

```
use_count = 1, expired = 0
对象还在，值为 42
sp 销毁后 expired = 1
lock() 返回空指针，对象已经不在了
```

- `lock()`：对象还在，返回一个有效的 `shared_ptr`（计数 +1，保证使用期间对象不会被释放）；对象已经释放，返回空的 `shared_ptr`。
- `expired()`：对象是否已经释放。但在多线程中，`expired()` 返回 `false` 后对象仍可能立刻被别的线程释放，所以**应该用 `lock()` 的结果判断，而不是先 `expired()` 再访问**。

`weak_ptr` 的另一个典型用途是**缓存**：缓存表里存 `weak_ptr`，对象还有人用时可以直接取出，没人用时自动释放，缓存不会阻止释放。

## 6. 常见错误

### 6.1 用同一个裸指针创建两个 shared_ptr

```cpp
#include <memory>
int main()
{
    int* raw = new int(10);
    std::shared_ptr<int> a(raw);
    std::shared_ptr<int> b(raw);     // 错误：两个独立的控制块，各自认为自己拥有 raw
    return *a + *b;
}                                    // a、b 析构时各 delete 一次：double free
```

用 AddressSanitizer 运行：

```
ERROR: AddressSanitizer: attempting double-free
```

每次用裸指针构造 `shared_ptr`，都会创建一个**新的控制块**。`a` 和 `b` 有各自的控制块，各自的计数都是 1，析构时都会 `delete raw`。**想共享，必须从已有的 `shared_ptr` 拷贝**：`std::shared_ptr<int> b = a;`。最好的习惯是**根本不让裸指针出现**，直接用 `make_shared` 创建。

### 6.2 在成员函数中用 this 创建 shared_ptr

同样的问题，在成员函数里写 `std::shared_ptr<Session>(this)` 会创建第二个控制块。需要在成员函数中拿到指向自己的 `shared_ptr` 时（比如异步操作要保证对象在回调前不被释放），让类继承 `std::enable_shared_from_this`，然后调用 `shared_from_this()`：

```cpp
#include <iostream>
#include <memory>
#include <vector>

class Session : public std::enable_shared_from_this<Session> {
public:
    std::shared_ptr<Session> self()
    {
        return shared_from_this();          // 正确：与已有的 shared_ptr 共享控制块
    }
    ~Session() { std::cout << "  ~Session\n"; }
};

int main()
{
    auto s = std::make_shared<Session>();
    std::vector<std::shared_ptr<Session>> active;
    active.push_back(s->self());
    std::cout << "use_count = " << s.use_count() << '\n';
    return 0;
}
```

运行结果：

```
use_count = 2
  ~Session
```

计数是 2，说明两者共享同一个控制块，对象只析构了一次。注意：**只有当对象已经被某个 `shared_ptr` 管理时，才能调用 `shared_from_this()`**。在构造函数里，或者对象是直接在栈上创建的，调用它是错误的（C++17 起会抛出 `std::bad_weak_ptr` 异常）。

### 6.3 其他

- **`get()` 返回的裸指针不要 `delete`**，也不要用它再去构造智能指针。
- **数组要用 `unique_ptr<T[]>`**。`unique_ptr<int>` 管理 `new int[5]` 时会用 `delete` 而不是 `delete[]`，这是未定义行为。C++11 的 `shared_ptr` 不支持数组（C++17 起支持 `shared_ptr<T[]>`），需要自定义删除器。
- **不要滥用 `shared_ptr`**。「不知道谁该负责释放」通常说明设计有问题，先想清楚所有权，大多数情况 `unique_ptr` 就够了。

## 7. 进阶

### 7.1 为什么 make 函数更安全

在 C++17 之前，下面的代码可能泄漏内存：

```cpp
process(std::shared_ptr<Widget>(new Widget), computePriority());
```

C++17 之前，函数参数的求值顺序没有规定，编译器可以这样安排：① `new Widget`；② 调用 `computePriority()`；③ 构造 `shared_ptr`。如果第 ② 步抛出异常，第 ① 步分配的内存还没有交给任何智能指针，就泄漏了。`make_shared` / `make_unique` 把「分配」和「交给智能指针」合并成一个函数调用，不存在中间状态。C++17 规定一个参数的求值必须完整结束后才能开始另一个，这个问题不再存在，但使用 make 函数仍然是好习惯。

### 7.2 make_shared 的代价

`make_shared` 把对象和控制块放在一块内存里，带来一个副作用：**只要还有 `weak_ptr` 存在，控制块就不能释放，于是整块内存都不能释放**。对象的析构函数会在强引用计数归零时正常调用，但对象占用的内存要等到弱引用计数也归零才归还。对象很大、`weak_ptr` 又存活很久时，可以考虑用 `shared_ptr<T>(new T)` 分开分配。另外，`make_shared` 无法指定自定义删除器。

### 7.3 智能指针的大小

```cpp
#include <cstdio>
#include <memory>
struct FreeDeleter { void operator()(int* p) const { delete p; } };   // 无状态的删除器
void freeInt(int* p) { delete p; }
int main()
{
    std::printf("int*                               : %zu\n", sizeof(int*));
    std::printf("unique_ptr<int>                    : %zu\n", sizeof(std::unique_ptr<int>));
    std::printf("unique_ptr<int, FreeDeleter>       : %zu\n", sizeof(std::unique_ptr<int, FreeDeleter>));
    std::printf("unique_ptr<int, void(*)(int*)>     : %zu\n", sizeof(std::unique_ptr<int, void (*)(int*)>));
    std::printf("shared_ptr<int>                    : %zu\n", sizeof(std::shared_ptr<int>));
    std::printf("weak_ptr<int>                      : %zu\n", sizeof(std::weak_ptr<int>));
    std::unique_ptr<int, void (*)(int*)> u(new int(1), freeInt);
    return 0;
}
```

运行结果（64 位系统）：

```
int*                               : 8
unique_ptr<int>                    : 8
unique_ptr<int, FreeDeleter>       : 8
unique_ptr<int, void(*)(int*)>     : 16
shared_ptr<int>                    : 16
weak_ptr<int>                      : 16
```

- **默认的 `unique_ptr` 和裸指针一样大**，`->`、`*` 都会被内联，**零开销**。所以在所有该用指针表达所有权的地方，都没有理由不用 `unique_ptr`。
- 删除器是**无状态的函数对象**时，`unique_ptr` 仍然是 8 字节（标准库利用空基类优化，不为空对象分配空间）；删除器是**函数指针**时，需要额外存一个指针，变成 16 字节。所以自定义删除器时优先写成函数对象类（或 C++20 中无捕获的 lambda）。
- `shared_ptr` 和 `weak_ptr` 都是两个指针的大小，删除器保存在控制块里，不影响它们本身的大小。

## 8. 小结

- 智能指针用 **RAII** 管理资源：析构函数负责释放，异常、提前返回都不会泄漏
- **`unique_ptr`**：独占所有权，只能移动，零开销，**默认首选**；可以用自定义删除器管理任何资源
- **`shared_ptr`**：引用计数实现共享所有权，最后一个销毁时释放对象；有控制块和原子操作的开销
- **`weak_ptr`**：不增加计数的观察者，用 `lock()` 访问对象，用来**打破循环引用**和实现缓存
- 用 **`make_shared`**（C++14 起还有 `make_unique`）创建智能指针，不要让裸指针出现
- 不要用同一个裸指针创建多个 `shared_ptr`；在成员函数中用 `shared_from_this()` 而不是 `this`

## 9. 练习

1. 下面的代码有什么问题？怎么改？

   ```cpp
   std::unique_ptr<int> p(new int[10]);
   ```

2. 写一个函数 `std::unique_ptr<std::FILE, int(*)(std::FILE*)> openFile(const char* path, const char* mode)`，打开失败时返回空指针。

3. 设计一个树结构：每个节点拥有若干子节点，并且能访问自己的父节点。`children` 和 `parent` 分别应该用什么类型？

<details>
<summary>点击查看答案</summary>

1. `unique_ptr<int>` 析构时用 `delete` 释放用 `new[]` 分配的数组，是未定义行为。改成 `std::unique_ptr<int[]> p(new int[10]);`，或者直接用 `std::vector<int> v(10);`。
2. 参考代码：

   ```cpp
   std::unique_ptr<std::FILE, int (*)(std::FILE*)> openFile(const char* path, const char* mode)
   {
       return std::unique_ptr<std::FILE, int (*)(std::FILE*)>(std::fopen(path, mode), std::fclose);
   }
   ```

   `fopen` 失败时返回 `nullptr`，`unique_ptr` 就是空的。`unique_ptr` 析构时不会对空指针调用删除器，所以不必担心 `fclose(nullptr)`。

3. 父节点拥有子节点，子节点只引用父节点。如果节点只被它的父节点拥有，最简单的设计是 `std::vector<std::unique_ptr<Node>> children;` 加上 `Node* parent;`：子节点一定比父节点先销毁，所以裸指针 `parent` 不会悬空。如果节点还会被树之外的代码共享，就用 `std::vector<std::shared_ptr<Node>> children;` 加 `std::weak_ptr<Node> parent;`。

</details>
