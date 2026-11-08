---
title: 单例模式
date: 2026-11-08 14:00:00
updated: 2026-11-08 14:00:00
categories:
  - 设计模式
  - 创建型模式
tags:
  - 设计模式
  - 单例
  - 多线程
  - 依赖注入
order: 1
description: 单例保证一个类只有一个实例。最朴素的懒汉式写法，8 个线程同时访问，5 次运行全都造出了 8 个「单例」；全局对象写法有静态初始化顺序的问题，只是调换链接顺序，读到的端口就从 5432 变成了 0。C++11 起最简单的 Meyers 单例同时解决了这两个问题，实测访问开销和普通全局变量一样（0.63 纳秒）。但单例也不一定唯一：用 -fvisibility=hidden 编译的两个动态库，各自有一个「单例」。最后说明单例为什么常被当作反模式，以及怎么用依赖注入代替它。
top_group_list: true
disableNunjucks: true
viz: [dp-singleton]
---

> **本文目标**：会写线程安全的单例（Meyers 单例），知道朴素懒汉式和经典双重检查锁定错在哪里；理解静态初始化顺序和析构顺序的问题；知道单例在动态库里可能不唯一；理解单例为什么常被当作反模式，会用依赖注入代替它。
>
> **前置知识**：第 1 章《SOLID 原则（下）》（依赖注入）；C/C++ 专栏的《多线程入门：std::thread 与互斥锁》《原子操作与内存序》。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`，多线程的程序要加 `-pthread`。完整代码在仓库的 `projects/dp-lab/06-singleton/` 目录。

## 1. 意图

**单例模式**（Singleton）：保证一个类只有一个实例，并提供一个全局访问点。

想用单例的场合通常是这样的：程序里某种东西**确实只该有一个**，而且很多地方都要用到它，比如日志、配置、连接池、硬件设备的句柄。把它做成单例，任何地方写一句 `Logger::instance().log(...)` 就能用，不用一层层把对象传下去。

```uml
label: 单例的结构：构造函数私有，静态成员 instance 返回唯一的实例，拷贝构造和拷贝赋值被删除
classes:
  - { id: S, name: Singleton, x: 230, y: 16, w: 250, ops: ['$+ instance() : Singleton&', '- Singleton()', '+ Singleton(const Singleton&) = delete', '+ operator=(const Singleton&) = delete', '+ doSomething()'] }
  - { id: C1, name: ClientA, x: 20, y: 62.5, w: 110 }
  - { id: C2, name: ClientB, x: 20, y: 140, w: 110 }
rels:
  - { from: C1, to: S, type: depend, label: 'instance()' }
  - { from: C2, to: S, type: depend, tdx: 30 }
```

要做到「只有一个」，单例需要三样东西：**私有的构造函数**（外面不能自己创建），**删除的拷贝操作**（不能复制出第二个），以及一个**静态函数**返回唯一的实例。难点全在最后一个：这个实例什么时候创建、怎么保证多线程下只创建一次。

## 2. 最直接的写法，和它的两个问题

### 2.1 懒汉式：多线程下创建了好几个

最直接的写法是「第一次用到时才创建」，叫**懒汉式**：

<!-- run: off -->
```cpp
class Config {
public:
    static Config* instance() {
        if (inst_ == nullptr)              // ① 检查
            inst_ = new Config;            // ② 创建并赋值
        return inst_;
    }
private:
    Config();                              // 构造时要读配置文件，比较慢
    static Config* inst_;
};
Config* Config::inst_ = nullptr;
```

单线程下这没有问题。`race_naive.cpp` 让 8 个线程同时第一次调用 `instance()`（构造函数里用 `sleep_for` 模拟 10 毫秒的读文件），统计构造函数执行了几次：

```text
构造函数执行了 8 次，线程们拿到了 8 个不同的对象
```

运行 5 次，5 次都是 8 个对象；在 Ubuntu 上用 GCC 编译运行，结果一样。用 ThreadSanitizer（`-fsanitize=thread`）运行，它在赋值的那一行报告了数据竞争：

```text
WARNING: ThreadSanitizer: data race
  Write of size 8 at 0x000104844008 by thread T5:
    #0 Config::instance() race_naive.cpp:15
```

问题出在「检查」和「赋值」之间：一个线程检查完、还没来得及赋值，另一个线程也来检查，看到的同样是空指针。下面的演示一步步展示了这个过程，也展示了第 3 节的 Meyers 单例是怎么避免它的：

<div class="dsviz" data-viz="dp-singleton"></div>

真实程序的构造函数没有这么慢，竞争窗口很小，可能跑一万次才出一次问题，这样的 bug 最难查。而且，即使碰巧只创建了一个对象，对 `inst_` 不加同步的读写本身就是未定义行为。

### 2.2 饿汉式：静态初始化顺序

另一种写法是程序启动时就创建好，叫**饿汉式**：直接用一个全局对象，或者一个静态成员变量。全局对象在 `main` 之前由运行时库初始化，那时只有一个线程，没有竞争的问题。

但它有另一个问题。`initorder/` 里有三个源文件：`config.cpp` 定义了一个全局变量 `g_dbPort`，它的初值要在运行时计算（读环境变量）；`logger.cpp` 定义了一个全局对象 `g_logger`，它的构造函数要用到 `g_dbPort`：

<!-- run: off -->
```cpp
// config.cpp
int computePort() {
    const char* env = std::getenv("DB_PORT");     // 运行时才能确定，所以是「动态初始化」
    return env ? std::atoi(env) : 5432;
}
int g_dbPort = computePort();

// logger.cpp
extern int g_dbPort;
struct Logger {
    Logger() { std::printf("Logger 构造时：g_dbPort = %d\n", g_dbPort); }
};
Logger g_logger;
```

同样的三个源文件，只调换链接时的顺序（`initorder/run.sh`）：

```text
== 链接顺序：main.cpp config.cpp logger.cpp
Logger 构造时：g_dbPort = 5432，dbPort() = 5432
main 开始时：g_dbPort = 5432
== 链接顺序：main.cpp logger.cpp config.cpp
Logger 构造时：g_dbPort = 0，dbPort() = 5432
main 开始时：g_dbPort = 5432
```

<figure class="diagram">
<svg viewBox="0 0 720 200" role="img" aria-label="静态初始化顺序：链接顺序为 config.cpp 在前时，g_dbPort 先初始化为 5432，Logger 构造时读到 5432；logger.cpp 在前时，Logger 先构造，读到的 g_dbPort 还是 0"><g style="font-family:var(--mono)"><text x="10.0" y="24.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">链接顺序 main.cpp config.cpp logger.cpp</text><rect x="10.0" y="34.0" width="250.0" height="32.0" rx="4" style="fill:var(--panel);stroke:var(--fg-2);stroke-width:1.2"/><text x="135.0" y="55.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">g_dbPort = computePort() → 5432</text><path d="M262,50 h22" style="stroke:var(--fg-2);stroke-width:1.4"/><path d="M284,45 l8,5 l-8,5 z" style="fill:var(--fg-2)"/><rect x="294.0" y="34.0" width="250.0" height="32.0" rx="4" style="fill:var(--panel);stroke:var(--fg-2);stroke-width:1.2"/><text x="419.0" y="55.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">g_logger 构造：读到 5432</text><rect x="578.0" y="34.0" width="120.0" height="32.0" rx="4" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.2"/><text x="638.0" y="55.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">main()</text><path d="M546,50 h22" style="stroke:var(--fg-2);stroke-width:1.4"/><path d="M568,45 l8,5 l-8,5 z" style="fill:var(--fg-2)"/><text x="10.0" y="116.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">链接顺序 main.cpp logger.cpp config.cpp</text><rect x="10.0" y="126.0" width="250.0" height="32.0" rx="4" style="fill:var(--mark);stroke:var(--danger);stroke-width:1.6"/><text x="135.0" y="147.0" text-anchor="middle" style="fill:var(--danger);font-size:11.5px">g_logger 构造：读到 0</text><path d="M262,142 h22" style="stroke:var(--fg-2);stroke-width:1.4"/><path d="M284,137 l8,5 l-8,5 z" style="fill:var(--fg-2)"/><rect x="294.0" y="126.0" width="250.0" height="32.0" rx="4" style="fill:var(--panel);stroke:var(--fg-2);stroke-width:1.2"/><text x="419.0" y="147.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">g_dbPort = computePort() → 5432</text><rect x="578.0" y="126.0" width="120.0" height="32.0" rx="4" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.2"/><text x="638.0" y="147.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">main()</text><path d="M546,142 h22" style="stroke:var(--fg-2);stroke-width:1.4"/><path d="M568,137 l8,5 l-8,5 z" style="fill:var(--fg-2)"/><text x="10.0" y="196.0" text-anchor="start" style="fill:var(--muted);font-size:11px">同一个源文件里按定义顺序初始化；不同源文件之间的顺序，标准没有规定（实测取决于链接顺序）</text></g></svg>
</figure>

第二种链接顺序下，`Logger` 构造时 `g_dbPort` 还没初始化，读到的是 0（全局变量在动态初始化之前先被清零）。macOS 上的 Clang 和 Ubuntu 上的 GCC 结果完全相同。C++ 标准只规定了**同一个源文件内**的全局对象按定义顺序初始化，**不同源文件之间**的顺序是不确定的。这就是著名的**静态初始化顺序问题**（static initialization order fiasco）。

如果把全局变量换成第一次使用时才初始化的函数（输出里的 `dbPort()`），两种链接顺序下都是正确的 5432：

<!-- run: off -->
```cpp
int& dbPort() {
    static int port = computePort();               // 第一次调用 dbPort() 时才初始化
    return port;
}
```

这就是下一节的 Meyers 单例。

## 3. 线程安全的写法

### 3.1 每次都加锁

最直接的修复是给 `instance()` 加一把锁：

<!-- run: off -->
```cpp
static Config& instance() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!inst_) inst_ = new Config;
    return *inst_;
}
```

这是正确的，但只有第一次调用需要锁，之后的每一次调用都在白白加锁、解锁。第 4 节会实测它有多慢。

### 3.2 双重检查锁定

为了只在第一次加锁，有人想出了**双重检查锁定**（double-checked locking）：先不加锁检查一次，是空的再加锁、再检查一次。在 C++11 之前，下面这种写法曾经广为流传，但它是**错的**：

<!-- run: off -->
```cpp
static Config* instance() {
    if (inst_ == nullptr) {                        // 第一次检查，不加锁
        std::lock_guard<std::mutex> lock(mu_);
        if (inst_ == nullptr)                      // 第二次检查，加锁
            inst_ = new Config;                    // 错在这里
    }
    return inst_;
}
```

`inst_ = new Config` 实际上分三步：分配内存、在这块内存上构造对象、把地址赋给 `inst_`。编译器和 CPU 都可能把第三步提前到第二步之前，因为对当前线程来说，结果是一样的。于是另一个线程在第一次检查时就可能看到一个**非空但还没构造完的对象**，直接拿去用。Scott Meyers 和 Andrei Alexandrescu 在 2004 年的论文里详细分析过这个问题。

C++11 有了内存模型以后，可以用 `std::atomic` 加上 acquire / release 内存序把它写对：

<!-- run: off -->
```cpp
static std::atomic<Config*> inst_;

static Config& instance() {
    Config* p = inst_.load(std::memory_order_acquire);
    if (!p) {
        std::lock_guard<std::mutex> lock(mu_);
        p = inst_.load(std::memory_order_relaxed);
        if (!p) {
            p = new Config;
            inst_.store(p, std::memory_order_release);   // 构造完成之后，才让别的线程看到指针
        }
    }
    return *p;
}
```

release 写入保证「构造对象」的所有内存操作都在「存入指针」之前完成并对其他线程可见；acquire 读取保证读到指针的线程，也能看到对象构造完成后的内容。写对它需要理解内存序，很容易出错，所以下面有更简单的办法。

### 3.3 std::call_once

标准库提供了「只执行一次」的工具 `std::call_once`：

<!-- run: off -->
```cpp
static Config& instance() {
    static std::once_flag flag;
    static Config* inst = nullptr;
    std::call_once(flag, [] { inst = new Config; });
    return *inst;
}
```

即使多个线程同时调用，lambda 也只会执行一次，其他线程会等它执行完再返回。

### 3.4 Meyers 单例：最简单，也是推荐的写法

C++11 标准规定：**如果多个线程同时执行到一个局部静态变量的声明，而它正在被初始化，其他线程必须等待初始化完成**。也就是说，局部静态变量的初始化本身就是线程安全的。于是单例可以写成这样，这种写法以 Scott Meyers 的名字命名：

```cpp
#include <iostream>
#include <string>

class Logger {
public:
    static Logger& instance() {
        static Logger inst;                    // 第一次调用时构造，之后一直是同一个对象
        return inst;
    }
    Logger(const Logger&) = delete;            // 禁止拷贝：不然 Logger copy = Logger::instance(); 会悄悄造出第二个
    Logger& operator=(const Logger&) = delete;

    void log(const std::string& msg) { std::cout << '[' << ++count_ << "] " << msg << '\n'; }

private:
    Logger() { std::cout << "Logger 构造\n"; }  // 构造函数私有：外面不能 new，也不能直接定义变量
    ~Logger() { std::cout << "Logger 析构\n"; }
    int count_ = 0;
};

void connect() { Logger::instance().log("连接数据库"); }
void query() { Logger::instance().log("执行查询"); }

int main() {
    std::cout << "main 开始\n";
    connect();
    query();
    std::cout << "两次拿到的是同一个对象：" << (&Logger::instance() == &Logger::instance()) << '\n';
    std::cout << "main 结束\n";
}
```

```text
main 开始
Logger 构造
[1] 连接数据库
[2] 执行查询
两次拿到的是同一个对象：1
main 结束
Logger 析构
```

把 2.1 节的实验换成 Meyers 单例（`race_meyers.cpp`），8 个线程同时访问，5 次运行都是「构造函数执行了 1 次，线程们拿到了 1 个不同的对象」，ThreadSanitizer 也没有任何警告。

编译器为 `static Logger inst;` 生成的代码，大致相当于 3.2 节那个正确的双重检查锁定：一个隐藏的「守卫」变量记录是否已经初始化，第一次调用时加锁构造，之后每次调用只需读一下守卫、跳过初始化。Meyers 单例同时解决了第 2 节的两个问题：

- **线程安全**：由编译器和标准库保证；
- **初始化顺序**：第一次调用 `instance()` 时才构造，任何依赖它的代码只要通过 `instance()` 访问，就一定能拿到构造好的对象。

> GCC 和 Clang 有一个选项 `-fno-threadsafe-statics`，会去掉守卫里的加锁。一些嵌入式项目为了减小代码体积会打开它，这时 Meyers 单例就不再是线程安全的了。

## 4. 实测：访问一次单例有多贵

单例会被频繁访问，每次访问的开销值得关心。`bench_access.cpp` 比较了五种写法，每种访问 2000 万次（单线程，或 8 个线程分摊），取 9 次的中位数。访问函数都标记成不内联，模拟从别的源文件调用的情况：

| 每次访问的耗时（ns） | Clang 21：1 线程 | Clang 21：8 线程 | GCC 13：1 线程 | GCC 13：8 线程* |
| --- | --- | --- | --- | --- |
| ① 全局对象 | 0.64 | 0.13 | 0.63 | 0.32 |
| ② Meyers 单例 | 0.63 | 0.13 | 0.63 | 0.32 |
| ③ `std::call_once` | 0.84 | 0.16 | 1.79 | 0.95 |
| ⑤ `atomic` 双重检查 | 0.77 | 0.18 | 1.26 | 0.70 |
| ④ 每次加锁 | 3.61 | 33.18 | 3.49 | 29.08 |

（Apple M6，`-O2`。「8 线程」是总耗时除以总访问次数，线程越多、并行得越好，这个数就越小。* GCC 运行在 Docker 的 Ubuntu 24.04 容器里，虚拟机只分到 2 个 CPU，8 个线程实际只有 2 个在同时运行。）

<figure class="diagram">
<svg viewBox="0 0 740 330" role="img" aria-label="访问一次单例的开销（对数刻度）：全局对象、Meyers 单例、call_once、atomic 双重检查都在 2 纳秒以内；每次加锁单线程约 3.5 纳秒，8 个线程争用时约 30 纳秒"><g style="font-family:var(--mono)"><text x="10.0" y="18.0" text-anchor="start" style="fill:var(--muted);font-size:11px">每次访问的平均耗时（纳秒，对数刻度，越短越好）</text><line x1="170.0" y1="28" x2="170.0" y2="268" style="stroke:var(--border-2);stroke-width:.7;stroke-dasharray:2 4"/><text x="170.0" y="284.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">0.1 ns</text><line x1="346.7" y1="28" x2="346.7" y2="268" style="stroke:var(--border-2);stroke-width:.7;stroke-dasharray:2 4"/><text x="346.7" y="284.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1 ns</text><line x1="523.3" y1="28" x2="523.3" y2="268" style="stroke:var(--border-2);stroke-width:.7;stroke-dasharray:2 4"/><text x="523.3" y="284.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">10 ns</text><line x1="700.0" y1="28" x2="700.0" y2="268" style="stroke:var(--border-2);stroke-width:.7;stroke-dasharray:2 4"/><text x="700.0" y="284.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">100 ns</text><text x="162.0" y="58.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">① 全局对象</text><rect x="170.0" y="34.0" width="142.4" height="8.0" rx="1.5" style="fill:var(--link)"/><text x="316.4" y="41.5" text-anchor="start" style="fill:var(--link);font-size:9px">0.64</text><rect x="170.0" y="44.0" width="20.1" height="8.0" rx="1.5" style="fill:var(--accent)"/><text x="194.1" y="51.5" text-anchor="start" style="fill:var(--accent);font-size:9px">0.13</text><rect x="170.0" y="54.0" width="141.2" height="8.0" rx="1.5" style="fill:var(--warn)"/><text x="315.2" y="61.5" text-anchor="start" style="fill:var(--warn);font-size:9px">0.63</text><rect x="170.0" y="64.0" width="89.2" height="8.0" rx="1.5" style="fill:var(--danger)"/><text x="263.2" y="71.5" text-anchor="start" style="fill:var(--danger);font-size:9px">0.32</text><text x="162.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">② Meyers 单例</text><rect x="170.0" y="81.0" width="141.2" height="8.0" rx="1.5" style="fill:var(--link)"/><text x="315.2" y="88.5" text-anchor="start" style="fill:var(--link);font-size:9px">0.63</text><rect x="170.0" y="91.0" width="20.1" height="8.0" rx="1.5" style="fill:var(--accent)"/><text x="194.1" y="98.5" text-anchor="start" style="fill:var(--accent);font-size:9px">0.13</text><rect x="170.0" y="101.0" width="141.2" height="8.0" rx="1.5" style="fill:var(--warn)"/><text x="315.2" y="108.5" text-anchor="start" style="fill:var(--warn);font-size:9px">0.63</text><rect x="170.0" y="111.0" width="89.2" height="8.0" rx="1.5" style="fill:var(--danger)"/><text x="263.2" y="118.5" text-anchor="start" style="fill:var(--danger);font-size:9px">0.32</text><text x="162.0" y="152.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">③ std::call_once</text><rect x="170.0" y="128.0" width="163.3" height="8.0" rx="1.5" style="fill:var(--link)"/><text x="337.3" y="135.5" text-anchor="start" style="fill:var(--link);font-size:9px">0.84</text><rect x="170.0" y="138.0" width="36.1" height="8.0" rx="1.5" style="fill:var(--accent)"/><text x="210.1" y="145.5" text-anchor="start" style="fill:var(--accent);font-size:9px">0.16</text><rect x="170.0" y="148.0" width="221.3" height="8.0" rx="1.5" style="fill:var(--warn)"/><text x="395.3" y="155.5" text-anchor="start" style="fill:var(--warn);font-size:9px">1.79</text><rect x="170.0" y="158.0" width="172.7" height="8.0" rx="1.5" style="fill:var(--danger)"/><text x="346.7" y="165.5" text-anchor="start" style="fill:var(--danger);font-size:9px">0.95</text><text x="162.0" y="199.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">⑤ atomic 双重检查</text><rect x="170.0" y="175.0" width="156.6" height="8.0" rx="1.5" style="fill:var(--link)"/><text x="330.6" y="182.5" text-anchor="start" style="fill:var(--link);font-size:9px">0.77</text><rect x="170.0" y="185.0" width="45.1" height="8.0" rx="1.5" style="fill:var(--accent)"/><text x="219.1" y="192.5" text-anchor="start" style="fill:var(--accent);font-size:9px">0.18</text><rect x="170.0" y="195.0" width="194.4" height="8.0" rx="1.5" style="fill:var(--warn)"/><text x="368.4" y="202.5" text-anchor="start" style="fill:var(--warn);font-size:9px">1.26</text><rect x="170.0" y="205.0" width="149.3" height="8.0" rx="1.5" style="fill:var(--danger)"/><text x="323.3" y="212.5" text-anchor="start" style="fill:var(--danger);font-size:9px">0.7</text><text x="162.0" y="246.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">④ 每次加锁</text><rect x="170.0" y="222.0" width="275.2" height="8.0" rx="1.5" style="fill:var(--link)"/><text x="449.2" y="229.5" text-anchor="start" style="fill:var(--link);font-size:9px">3.61</text><rect x="170.0" y="232.0" width="445.4" height="8.0" rx="1.5" style="fill:var(--accent)"/><text x="619.4" y="239.5" text-anchor="start" style="fill:var(--accent);font-size:9px">33.18</text><rect x="170.0" y="242.0" width="272.6" height="8.0" rx="1.5" style="fill:var(--warn)"/><text x="446.6" y="249.5" text-anchor="start" style="fill:var(--warn);font-size:9px">3.49</text><rect x="170.0" y="252.0" width="435.2" height="8.0" rx="1.5" style="fill:var(--danger)"/><text x="609.2" y="259.5" text-anchor="start" style="fill:var(--danger);font-size:9px">29.08</text><rect x="10.0" y="300.0" width="12.0" height="10.0" rx="2" style="fill:var(--link)"/><text x="28.0" y="309.0" text-anchor="start" style="fill:var(--fg);font-size:11px">Clang · 1 线程</text><rect x="180.0" y="300.0" width="12.0" height="10.0" rx="2" style="fill:var(--accent)"/><text x="198.0" y="309.0" text-anchor="start" style="fill:var(--fg);font-size:11px">Clang · 8 线程</text><rect x="350.0" y="300.0" width="12.0" height="10.0" rx="2" style="fill:var(--warn)"/><text x="368.0" y="309.0" text-anchor="start" style="fill:var(--fg);font-size:11px">GCC · 1 线程</text><rect x="520.0" y="300.0" width="12.0" height="10.0" rx="2" style="fill:var(--danger)"/><text x="538.0" y="309.0" text-anchor="start" style="fill:var(--fg);font-size:11px">GCC · 8 线程*</text><text x="10.0" y="326.0" text-anchor="start" style="fill:var(--muted);font-size:10px">* GCC 运行在 Docker 的 Ubuntu 24.04 里，虚拟机只有 2 个 CPU，8 个线程实际只有 2 个在同时运行</text></g></svg>
</figure>

- **Meyers 单例和普通全局对象一样快**：两个编译器上都是 0.63 ns。守卫变量的检查只是一次读取加一次几乎总能预测对的分支，和函数调用本身的开销相比可以忽略。
- `call_once` 和 `atomic` 双重检查也只多一点点。GCC 上的 `call_once` 慢一些（1.79 ns），这和 libstdc++ 的实现方式有关，但仍然很快。
- **每次加锁**是唯一慢的：单线程就慢了 5 倍；8 个线程同时访问时，所有线程都在抢同一把锁，每次访问要 30 ns 以上，比单线程还慢了将近 10 倍，线程越多越慢。

结论很简单：**用 Meyers 单例**。它最短、最不容易写错，还和全局变量一样快。

## 5. 析构顺序

Meyers 单例解决了构造顺序，析构顺序的问题却还在。静态对象在程序退出时按**构造完成的相反顺序**析构。如果单例 A 先构造，另一个静态对象 B 后构造，那么退出时 B 先析构、A 后析构，B 的析构函数里使用 A 是安全的。反过来就不行了：如果 B 的析构函数里用到了一个比 B 晚构造的单例，那个单例已经先被析构掉了，再用它就是未定义行为（练习 3）。

一种常见的解决办法是**故意不析构**：

<!-- run: off -->
```cpp
static Logger& instance() {
    static Logger* inst = new Logger;              // 永远不 delete，程序退出时由操作系统回收内存
    return *inst;
}
```

对象永远不会被析构，也就不存在「析构以后再被使用」的问题。代价是它的析构函数永远不会执行，所以只适合析构时不需要做事情的对象（比如不需要把缓冲区刷到文件里）。Google 的 C++ 风格指南就要求：具有静态存储期的对象要么平凡可析构，要么用这种方式让它永不析构。

## 6. 单例不一定唯一

「全局只有一个」这个承诺，在动态库面前可能会失效。`dup/registry.h` 是一个放在头文件里的 Meyers 单例，两个动态库 `liba` 和 `libb` 都包含了它，各自调用 `Registry::instance()`，主程序比较它们拿到的地址（`dup/run.sh`）：

```text
== -fvisibility=default
库 A 拿到 0x…，库 B 拿到 0x…，是同一个对象
库 A 看到的 count = 2，库 B 看到的 count = 2
== -fvisibility=hidden
库 A 拿到 0x…，库 B 拿到 0x…，是两个不同的对象！
库 A 看到的 count = 1，库 B 看到的 count = 1
```

<figure class="diagram">
<svg viewBox="0 0 720 210" role="img" aria-label="默认符号可见性下，两个动态库里的 Registry::instance 被合并，共用一个静态对象；用 -fvisibility=hidden 编译后，每个库各有一个静态对象"><g style="font-family:var(--mono)"><text x="10.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:13px;font-weight:700">-fvisibility=default</text><rect x="120.0" y="30.0" width="120.0" height="30.0" rx="4" style="fill:var(--panel-2);stroke:var(--fg-2);stroke-width:1.2"/><text x="180.0" y="50.0" text-anchor="middle" style="fill:var(--fg);font-size:12px">main</text><rect x="10.0" y="90.0" width="140.0" height="64.0" rx="6" style="fill:var(--panel);stroke:var(--fg-2);stroke-width:1.2"/><text x="80.0" y="108.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">liba</text><text x="80.0" y="126.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">instance()</text><path d="M180,60 L80,90" style="stroke:var(--border-2);stroke-width:1.2"/><rect x="210.0" y="90.0" width="140.0" height="64.0" rx="6" style="fill:var(--panel);stroke:var(--fg-2);stroke-width:1.2"/><text x="280.0" y="108.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">libb</text><text x="280.0" y="126.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">instance()</text><path d="M180,60 L280,90" style="stroke:var(--border-2);stroke-width:1.2"/><rect x="115.0" y="170.0" width="130.0" height="22.0" rx="4" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.6"/><text x="180.0" y="185.0" text-anchor="middle" style="fill:var(--accent);font-size:11px;font-weight:700">static Registry</text><path d="M80,154 L180,170" style="stroke:var(--accent);stroke-width:1.4;stroke-dasharray:4 3"/><path d="M280,154 L180,170" style="stroke:var(--accent);stroke-width:1.4;stroke-dasharray:4 3"/><text x="180.0" y="206.0" text-anchor="middle" style="fill:var(--accent);font-size:11px">同一个对象，count = 2</text><text x="370.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:13px;font-weight:700">-fvisibility=hidden</text><rect x="480.0" y="30.0" width="120.0" height="30.0" rx="4" style="fill:var(--panel-2);stroke:var(--fg-2);stroke-width:1.2"/><text x="540.0" y="50.0" text-anchor="middle" style="fill:var(--fg);font-size:12px">main</text><rect x="370.0" y="90.0" width="140.0" height="64.0" rx="6" style="fill:var(--panel);stroke:var(--fg-2);stroke-width:1.2"/><text x="440.0" y="108.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">liba</text><text x="440.0" y="126.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">instance()</text><path d="M540,60 L440,90" style="stroke:var(--border-2);stroke-width:1.2"/><rect x="390.0" y="134.0" width="100.0" height="16.0" rx="3" style="fill:var(--mark);stroke:var(--danger);stroke-width:1.4"/><text x="440.0" y="146.0" text-anchor="middle" style="fill:var(--danger);font-size:10px">static Registry</text><rect x="570.0" y="90.0" width="140.0" height="64.0" rx="6" style="fill:var(--panel);stroke:var(--fg-2);stroke-width:1.2"/><text x="640.0" y="108.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">libb</text><text x="640.0" y="126.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">instance()</text><path d="M540,60 L640,90" style="stroke:var(--border-2);stroke-width:1.2"/><rect x="590.0" y="134.0" width="100.0" height="16.0" rx="3" style="fill:var(--mark);stroke:var(--danger);stroke-width:1.4"/><text x="640.0" y="146.0" text-anchor="middle" style="fill:var(--danger);font-size:10px">static Registry</text><text x="540.0" y="185.0" text-anchor="middle" style="fill:var(--danger);font-size:11px">两个对象，各自 count = 1</text></g></svg>
</figure>

macOS 和 Linux 上的结果相同。原因是：类里定义的 `instance()` 是内联函数，每个包含它的动态库里都有一份代码和一份局部静态变量。默认情况下这些符号对外可见，动态链接器加载时会把它们合并成一个；用 `-fvisibility=hidden` 编译（很多项目为了减少导出符号、加快加载会这样做），符号不再导出，每个库就各用各的「单例」。Windows 的 DLL 默认就不导出符号，所以也是每个 DLL 各有一份。

解决办法是让单例**只在一个地方定义**：`instance()` 只在头文件里声明，在某一个库的 `.cpp` 文件里实现并明确导出，其他库都链接到这个库。

## 7. 为什么单例常被当作反模式

到这里，单例已经可以写得既正确又高效了。但很多有经验的开发者仍然建议**尽量不用单例**。它的问题不在实现，而在设计：

- **它是全局变量**。任何代码都能读写它，程序的行为取决于一份看不见的全局状态。
- **依赖被藏起来了**。一个函数里调用了 `Config::instance()`，从函数签名上完全看不出来，只能读代码才知道。这违反了上一章的依赖倒置原则：业务代码直接依赖了一个具体的类。
- **测试互相干扰**。所有测试共用同一个实例，一个测试改了它，另一个测试就受影响，结果取决于测试的运行顺序：

```cpp
#include <iostream>
#include <string>

class Config {
public:
    static Config& instance() { static Config c; return c; }
    double discount = 0.0;                     // 默认不打折
private:
    Config() = default;
};

// 被测的业务函数：悄悄依赖了全局的 Config
double finalPrice(double price) { return price * (1 - Config::instance().discount); }

bool testVipDiscount() {
    Config::instance().discount = 0.2;         // 为了测 VIP 折扣，改了全局配置，测完忘了改回去
    return finalPrice(100) == 80;
}
bool testNormalPrice() {
    return finalPrice(100) == 100;             // 以为没有折扣
}

int main() {
    // 按「普通价格 → VIP 折扣」的顺序运行：都通过
    std::cout << "顺序一：普通价格 " << (testNormalPrice() ? "✓" : "✗") << "，VIP 折扣 " << (testVipDiscount() ? "✓" : "✗") << '\n';
    // 测试框架换了一个顺序（或者并行运行）：普通价格的测试失败了
    std::cout << "顺序二：VIP 折扣 " << (testVipDiscount() ? "✓" : "✗") << "，普通价格 " << (testNormalPrice() ? "✓" : "✗") << '\n';
}
```

```text
顺序一：普通价格 ✓，VIP 折扣 ✓
顺序二：VIP 折扣 ✓，普通价格 ✗
```

- **职责混在一起**。一个类既要完成自己的业务，又要管理「自己只能有一个实例」，违反了单一职责原则。

### 7.1 用依赖注入代替

换一个角度看：「整个程序只需要一个 `Config`」是**程序**的决定，而不是 `Config` 这个**类**的决定。在 `main` 里只创建一个，把它传给需要的地方，就同样只有一个实例，还没有上面那些问题：

```cpp
#include <iostream>

struct Config {
    double discount = 0.0;
};

double finalPrice(double price, const Config& cfg) { return price * (1 - cfg.discount); }   // 依赖写在参数里

bool testVipDiscount() {
    Config cfg;
    cfg.discount = 0.2;
    return finalPrice(100, cfg) == 80;
}
bool testNormalPrice() {
    Config cfg;
    return finalPrice(100, cfg) == 100;
}

int main() {
    std::cout << "顺序一：普通价格 " << (testNormalPrice() ? "✓" : "✗") << "，VIP 折扣 " << (testVipDiscount() ? "✓" : "✗") << '\n';
    std::cout << "顺序二：VIP 折扣 " << (testVipDiscount() ? "✓" : "✗") << "，普通价格 " << (testNormalPrice() ? "✓" : "✗") << '\n';

    // 真正运行时，程序在 main 里只创建一个 Config，把它传给需要的地方：
    // 「只有一个」是程序的决定，而不是 Config 这个类的决定
    Config appConfig;
    appConfig.discount = 0.05;
    std::cout << "实际价格 " << finalPrice(100, appConfig) << '\n';
}
```

```text
顺序一：普通价格 ✓，VIP 折扣 ✓
顺序二：VIP 折扣 ✓，普通价格 ✓
实际价格 95
```

每个测试都有自己的 `Config`，互不影响；`finalPrice` 依赖什么，从参数上一眼就能看出来。如果觉得到处传参数太麻烦，可以把依赖放进类的构造函数里（上一章的构造函数注入），这样只需要在创建对象时传一次。

### 7.2 什么时候可以用单例

单例并不是一定不能用。下面这些情况，单例的代价比较小：

- 它**没有可变的状态**，或者初始化以后就不再变化（比如程序启动时读一次的只读配置）；
- 它代表的东西在物理上就只有一个，而且几乎所有代码都要用（比如日志的输出目标）；
- 它不影响业务结果，测试时不需要替换它（比如性能统计）。

即使用了单例，也可以只在程序的最外层（`main`、各个模块的入口）调用 `instance()`，把拿到的对象作为参数传给内部的代码。这样内部代码仍然可以单独测试。

## 8. 小结

- 单例：保证一个类只有一个实例，并提供全局访问点。需要私有构造函数、删除的拷贝操作和一个静态访问函数
- 朴素懒汉式在多线程下会创建多个实例。实测：8 个线程同时访问，5 次运行都创建了 8 个对象，ThreadSanitizer 报告数据竞争
- 饿汉式（全局对象）有静态初始化顺序问题。实测：只调换链接顺序，`Logger` 构造时读到的端口从 5432 变成了 0
- 经典的双重检查锁定是错的；C++11 起可以用 `atomic` + acquire / release 写对，或者用 `std::call_once`
- **Meyers 单例**（局部静态变量）由标准保证线程安全，又是第一次使用时才构造。实测访问开销和普通全局对象相同，都是 0.63 ns；每次加锁则要 3.5 ns，8 线程争用时超过 30 ns
- 析构顺序仍然可能出问题，可以让单例故意不析构
- 头文件里的单例被多个动态库包含时，用 `-fvisibility=hidden` 编译会变成每个库一个实例
- 单例本质上是全局变量，会隐藏依赖、让测试互相干扰。优先在 `main` 里只创建一个对象，再通过参数或构造函数注入

## 9. 练习

1. 下面的单例有哪些问题？

   <!-- run: off -->
   ```cpp
   class Config {
   public:
       static Config instance() {
           static Config c;
           return c;
       }
       Config() { load("app.conf"); }
       void set(const std::string& key, const std::string& value);
       std::string get(const std::string& key) const;
   };

   Config::instance().set("theme", "dark");
   std::cout << Config::instance().get("theme");     // 期望输出 dark
   ```

2. 3.2 节说经典的双重检查锁定是错的。请具体描述一种执行顺序，让某个线程拿到一个还没构造完的对象。为什么第二次检查放在锁里面也救不了它？

3. 下面的程序里，`g_cache` 是一个全局对象，它的析构函数会写一条日志；`Logger` 是 Meyers 单例，第一次使用是在 `main` 里。程序退出时会发生什么？怎么修复？

   <!-- run: off -->
   ```cpp
   struct Cache {
       ~Cache() { Logger::instance().log("缓存已清空"); }
   };
   Cache g_cache;

   int main() {
       Logger::instance().log("程序启动");
   }
   ```

4. 下面的订单服务通过单例拿到数据库连接，没法单独测试。用依赖注入改造它，并写出测试时的用法。

   <!-- run: off -->
   ```cpp
   class OrderService {
   public:
       bool cancel(int orderId) {
           auto& db = Database::instance();
           if (db.status(orderId) == "shipped") return false;   // 已发货的不能取消
           db.setStatus(orderId, "cancelled");
           return true;
       }
   };
   ```

<details>
<summary>点击查看答案</summary>

1. 三个问题：
   ① `instance()` **按值返回**，每次调用都会拷贝出一个新的 `Config`。`set` 改的是第一次调用返回的临时拷贝，第二次调用又拷贝了一份原来的，所以输出的不是 dark。应该返回引用 `static Config& instance()`。
   ② **构造函数是公有的**，任何地方都可以 `Config c;` 再创建一个，应该改成私有。
   ③ **没有删除拷贝构造和拷贝赋值**，即使改成返回引用，`Config copy = Config::instance();` 仍然会悄悄复制出第二个。应该加上 `Config(const Config&) = delete;` 和 `Config& operator=(const Config&) = delete;`，这样问题 ① 也会变成编译错误。

2. 一种执行顺序：线程 A 获得锁，执行 `inst_ = new Config`。编译器（或 CPU）把它安排成「分配内存 → 把地址写入 `inst_` → 执行构造函数」。在地址写入之后、构造函数执行完之前，线程 B 执行第一次检查，发现 `inst_` 不是空的，于是**不加锁**直接返回这个指针，然后去读一个还没初始化的对象。
   第二次检查只能防止两个线程都去创建对象，但线程 B 根本没有走到加锁那一步：问题出在**不加锁的第一次检查**，它和线程 A 的写入之间没有任何同步关系，看到的写入顺序不受保证。所以要么用 `atomic` 的 acquire / release 建立同步关系，要么直接用 Meyers 单例。

3. 构造顺序：`g_cache` 在 `main` 之前构造；`Logger` 在 `main` 里第一次调用 `instance()` 时构造，比 `g_cache` 晚。析构顺序和构造完成的顺序相反：程序退出时**先析构 `Logger`，再析构 `g_cache`**。`~Cache` 里调用 `Logger::instance()` 时，返回的是一个已经析构的对象，使用它是未定义行为：可能什么事都没有，可能输出乱码，也可能崩溃。
   修复方法：
   - 在 `Cache` 的构造函数里先调用一次 `Logger::instance()`，让 `Logger` 比 `g_cache` 先构造完成，从而后析构；
   - 或者把 `Logger` 改成第 5 节那种永不析构的写法；
   - 更好的做法是不要让全局对象的析构函数依赖其他全局对象：把 `Cache` 改成在 `main` 里创建的局部对象，它在 `main` 返回时析构，那时 `Logger` 一定还活着。

4. 把数据库抽象成接口，通过构造函数注入：

   <!-- run: off -->
   ```cpp
   class OrderDb {                                     // OrderService 需要的操作
   public:
       virtual ~OrderDb() = default;
       virtual std::string status(int orderId) = 0;
       virtual void setStatus(int orderId, const std::string& s) = 0;
   };

   class OrderService {
   public:
       explicit OrderService(OrderDb& db) : db_(db) {}
       bool cancel(int orderId) {
           if (db_.status(orderId) == "shipped") return false;
           db_.setStatus(orderId, "cancelled");
           return true;
       }
   private:
       OrderDb& db_;
   };

   // 测试：每个测试用自己的假数据库
   struct FakeDb : OrderDb {
       std::map<int, std::string> s;
       std::string status(int id) override { return s[id]; }
       void setStatus(int id, const std::string& v) override { s[id] = v; }
   };

   FakeDb db;
   db.s[1] = "shipped";
   db.s[2] = "paid";
   OrderService svc(db);
   assert(!svc.cancel(1));                            // 已发货的不能取消
   assert(svc.cancel(2) && db.s[2] == "cancelled");
   ```

   真正运行时，在 `main` 里创建唯一的数据库连接对象（它实现 `OrderDb`），传给 `OrderService`。如果暂时不想改动太多代码，也可以只在 `main` 里调用一次 `Database::instance()`，再把结果传进去，内部代码就不再直接依赖单例了。

</details>
