---
title: 多线程：one loop per thread
date: 2026-11-04 14:30:00
updated: 2026-11-04 14:30:00
categories:
  - 计算机网络
  - 服务器开发
tags:
  - 网络编程
  - 多线程
  - Reactor
  - C++
order: 4
description: 一个事件循环只能用一个 CPU 核。tinynet 的多线程模型是 one loop per thread：主线程的 EventLoop 接受连接，再把连接轮流分给几个 IO 线程，每个连接从此只在一个线程中处理，不需要加锁。跨线程的唯一通道是 runInLoop：把函数放进目标线程的队列，用 eventfd 唤醒它。本文实现这一套，并记录 ASan 和 TSan 在其中发现的三个真实问题：一个遗漏的唤醒、服务器退出时访问已经销毁的 EventLoop、以及 quit 与析构之间的竞争。
disableNunjucks: true
---

> **本文目标**：理解 one loop per thread 的模型，知道连接为什么固定属于一个线程；会用 eventfd 唤醒另一个线程的事件循环；理解 `runInLoop`、`queueInLoop` 的实现和唤醒的三种情况；知道多线程网络库中生命周期问题的典型形式，以及怎样用 Sanitizer 发现它们。
>
> **前置知识**：[上一篇](/posts/srv-timer/)；《条件变量与 async / future》；《C++工程实践》中的 [Sanitizer 与测试覆盖率](/posts/cppeng-sanitizers-coverage/)。

## 1. one loop per thread

一个 EventLoop 在一个线程中运行，最多用满一个 CPU 核。要用上多个核，有几种模型：

| 模型 | 做法 | 问题 |
|---|---|---|
| 每个连接一个线程 | 阻塞 IO，简单 | 连接多了线程太多，见《同时服务多个客户端》 |
| 一个事件循环 + 工作线程池 | IO 在一个线程，计算交给线程池 | IO 本身成为瓶颈 |
| **多个事件循环** | 每个线程一个 EventLoop，各管一部分连接 | 连接要分配给某个线程 |

tinynet 用第三种，这也是 muduo、Netty、Nginx（每个工作进程一个循环）的做法：

- **baseLoop**：主线程中的 EventLoop，只负责接受新连接；
- **IO 线程**：`setThreadNum(n)` 启动 n 个线程，每个线程一个 EventLoop；
- 新连接**轮流**分给各个 IO 线程，**此后这个连接的一切都在那个线程中处理**：读、写、回调、定时器、关闭。

<figure class="diagram">
<svg viewBox="0 0 700 240" role="img" aria-label="one loop per thread：主线程的 baseLoop 中的 Acceptor 接受新连接，轮流分给 IO 线程 1 和 IO 线程 2 的 EventLoop；之后每个连接只在自己的线程中处理；跨线程用 runInLoop 把函数放进目标 EventLoop 的队列，并写 eventfd 唤醒它">
  <defs><marker id="th-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="th-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="14" y="14" width="200" height="190" rx="8" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="30.0" y="38.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">主线程：baseLoop</text><rect x="30" y="56" width="168" height="46" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="114.0" y="75.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">Acceptor</text><text x="114.0" y="91.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">accept 新连接</text><rect x="30" y="120" width="168" height="64" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="114.0" y="140.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">connections_</text><text x="114.0" y="156.6" text-anchor="middle" style="fill:var(--muted);font-size:11px">所有连接的 map</text><text x="114.0" y="172.6" text-anchor="middle" style="fill:var(--muted);font-size:11px">只在 baseLoop 中访问</text><rect x="320" y="14" width="366" height="96" rx="8" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="336.0" y="38.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">IO 线程 1：EventLoop</text><rect x="336" y="52" width="100" height="40" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="386.0" y="76.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">连接 #1</text><rect x="448" y="52" width="100" height="40" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="498.0" y="76.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">连接 #3</text><rect x="560" y="52" width="100" height="40" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="610.0" y="76.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">连接 #5</text><line x1="198" y1="79" x2="318" y2="72" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#th-a)"/><text x="258.0" y="58.0" text-anchor="middle" style="fill:var(--link);font-size:11px">轮流分配</text><rect x="320" y="126" width="366" height="96" rx="8" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="336.0" y="150.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">IO 线程 2：EventLoop</text><rect x="336" y="164" width="100" height="40" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="386.0" y="188.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">连接 #2</text><rect x="448" y="164" width="100" height="40" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="498.0" y="188.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">连接 #4</text><rect x="560" y="164" width="100" height="40" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="610.0" y="188.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">连接 #6</text><line x1="198" y1="79" x2="318" y2="184" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#th-a)"/><text x="14.0" y="222.0" text-anchor="start" style="fill:var(--muted);font-size:11px">连接的读写、定时器、回调都在它所属的线程；跨线程只用 runInLoop + eventfd 唤醒</text>
  </g>
</svg>
</figure>

最后一点是整个设计的关键：一个连接的缓冲区、状态，永远只被一个线程访问，所以 TcpConnection 内部几乎不需要锁。线程之间的交互被限制在少数几个明确的地方，下面逐个看。

### 1.1 启动 IO 线程

```cpp
EventLoop* EventLoopThread::start()
{
    thread_ = std::thread([this] { threadFunc(); });
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] { return loop_ != nullptr; });   // 等新线程把 EventLoop 创建好
    return loop_;
}

void EventLoopThread::threadFunc()
{
    EventLoop loop;                         // EventLoop 在新线程的栈上，生命期和线程一样长
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = &loop;
    }
    cond_.notify_one();
    loop.loop();
    std::lock_guard<std::mutex> lock(mutex_);
    loop_ = nullptr;
}
```

EventLoop 必须在它所属的线程中创建（构造函数记下了线程 id），所以由新线程自己创建，再用条件变量通知 `start()` 返回它的地址。

### 1.2 分配连接

```cpp
void TcpServer::newConnection(int fd, const InetAddress& peer)
{
    loop_->assertInLoopThread();
    EventLoop* ioLoop = threadPool_->getNextLoop();         // 轮流分给各个 IO 线程
    std::string name = name_ + "#" + std::to_string(nextConnId_++);
    auto conn = std::make_shared<TcpConnection>(ioLoop, name, fd, sockets::localAddress(fd), peer);
    connections_[name] = conn;
    // ……设置回调
    ioLoop->runInLoop([conn] { conn->connectEstablished(); });   // 从此以后，这个连接只在 ioLoop 中处理
}
```

`newConnection` 在 baseLoop 的线程中执行，而 `connectEstablished`（把连接的 Channel 加进 epoll）必须在 IO 线程中执行，所以用 `runInLoop` 交过去。没有 IO 线程时，`getNextLoop()` 返回 baseLoop 自己，一切都在主线程中进行。

## 2. 跨线程：runInLoop

### 2.1 实现

```cpp
void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread()) cb();
    else queueInLoop(std::move(cb));
}

void EventLoop::queueInLoop(Functor cb)
{
    std::lock_guard<std::mutex> lock(mutex_);   // 唤醒也在锁中，原因见第 4.3 节
    pendingFunctors_.push_back(std::move(cb));
    // 别的线程：loop 可能在 epoll_wait 中睡着，要叫醒它。
    // 本线程、但正在执行 pendingFunctors：新加的要等下一轮，也要叫醒，否则下一轮的 epoll_wait 会一直等下去。
    // 本线程、loop 还没有开始：第一次 epoll_wait 之前也要有事件，否则要等到超时才执行。
    if (!isInLoopThread() || callingPendingFunctors_ || !looping_) wakeup();
}

void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);    // 交换出来再执行：执行时不持有锁，函数中可以再调用 queueInLoop
    }
    for (const Functor& f : functors) f();
    callingPendingFunctors_ = false;
}
```

`pendingFunctors_` 是 EventLoop 中唯一被多个线程访问的数据，用一个互斥锁保护。执行时先 `swap` 到局部变量，锁只持有交换的那一瞬间：执行函数时不持有锁，函数中再调用 `queueInLoop` 也不会死锁。

### 2.2 唤醒：eventfd

目标线程可能正阻塞在 `epoll_wait` 中，要让它醒来。tinynet 用 **eventfd**：一个内核维护的 64 位计数器，写入会让它变成可读。每个 EventLoop 创建一个 eventfd，作为一个普通的 Channel 加进自己的 epoll：

```cpp
void EventLoop::wakeup()
{
    std::uint64_t one = 1;
    ::write(wakeupFd_, &one, sizeof one);
}

void EventLoop::handleWakeup()              // wakeupChannel_ 的读回调
{
    std::uint64_t value = 0;
    ::read(wakeupFd_, &value, sizeof value);
}
```

别的线程写 eventfd，`epoll_wait` 返回，这一轮的事件处理完以后执行 `doPendingFunctors`。eventfd 比管道更轻：只占一个 fd，内核中也只是一个计数器。

### 2.3 一个遗漏的唤醒

`queueInLoop` 中唤醒的条件，最初只有前两个。单元测试 `QueueInLoopDuringPendingFunctors` 是这样写的：

```cpp
TEST(EventLoop, QueueInLoopDuringPendingFunctors)
{
    EventLoop loop;
    std::vector<int> order;
    loop.queueInLoop([&] {
        order.push_back(1);
        loop.queueInLoop([&] {
            order.push_back(2);
            loop.quit();
        });
    });
    auto start = std::chrono::steady_clock::now();
    loop.loop();
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
    EXPECT_LT(std::chrono::steady_clock::now() - start, 2s);
}
```

结果：

```
tests/eventloop_test.cpp:63: Failure
Expected: (std::chrono::steady_clock::now() - start) < (2s), actual: 8-byte object <C1-75 20-55 02-00 00-00> vs 8-byte object <02-00 00-00 00-00 00-00>

[  FAILED  ] EventLoop.QueueInLoopDuringPendingFunctors (10018 ms)
```

结果是对的，但用了 10 秒。原因是第一次 `queueInLoop` 是在 `loop()` 之前、在 loop 自己的线程中调用的：不是别的线程，也不在执行 pending 函数，于是没有唤醒。`loop()` 开始以后，第一次 `epoll_wait` 没有任何事件，一直等到 10 秒的超时才返回。加上第三个条件「loop 还没有开始」以后，测试 0 毫秒通过。

这也说明了上一篇提到的 10 秒超时的作用：没有它，这个测试会永远卡住。

## 3. 跨线程的 send

业务代码可能在别的线程中调用 `conn->send`，比如在工作线程中算完结果，再发给客户端。`send` 是线程安全的：

```cpp
void TcpConnection::send(std::string_view data)
{
    if (state_ != kConnected) return;
    if (loop_->isInLoopThread()) {
        sendInLoop(data);
    } else {
        // 别的线程：data 指向的内存在交给 IO 线程以后可能已经无效了，复制一份
        auto self = shared_from_this();
        loop_->runInLoop([this, self, copy = std::string(data)] { sendInLoop(copy); });
    }
}
```

两个细节：

1. 交给 IO 线程时**复制一份数据**：`string_view` 指向的内存属于调用者，lambda 在 IO 线程中执行时它可能已经被释放了；
2. lambda 捕获 `self`（`shared_ptr`），保证执行时连接还活着。

`state_` 在 IO 线程中被修改，却在调用 `send` 的线程中被读取，所以它是 `std::atomic<State>`。最初的版本中它是一个普通的枚举，这是一个数据竞争：写代码时我在 TSan 运行之前就注意到了，但如果没有注意到，TSan 也会报告。

测试 `SendFromAnotherThread` 从一个单独的线程调用 100 次 `send`，检查客户端收到了全部 1000 个字节。

## 4. Sanitizer 发现的问题

tinynet 的测试都在 Debug、ASan、TSan 三种构建下运行，涉及多线程的测试用 `--gtest_repeat=200` 重复运行。下面两个问题，单次运行时测试大部分都是通过的。

### 4.1 服务器退出时：访问已经销毁的 EventLoop

`EchoTest.EchoesBack`（3 个 IO 线程）在 ASan 下偶尔失败：

```
==346==ERROR: AddressSanitizer: SEGV on unknown address 0xff5b06ad7068 (pc 0xab0100b95650 bp 0xff5b08dfe0a0 sp 0xff5b08dfe0a0 T2)
==346==The signal is caused by a READ memory access.
    #0 0xab0100b95650 in tinynet::EventLoop::isInLoopThread() const include/tinynet/EventLoop.h:46
    #1 0xab0100ce748c in tinynet::Channel::~Channel() src/Channel.cpp:20
    #4 0xab0100c9d45c in tinynet::TcpConnection::~TcpConnection() src/TcpConnection.cpp:31
    #14 0xab0100cb4544 in ~<lambda> src/TcpServer.cpp:61
    ...
    #24 0xab0100c6c9c8 in tinynet::EventLoop::~EventLoop() src/EventLoop.cpp:43
```

从下往上读：baseLoop 的 EventLoop 正在析构（#24）；它销毁 `pendingFunctors_` 中剩下的一个 lambda（#14，`TcpServer.cpp:61`，就是「通知 baseLoop 删除连接」的那个）；lambda 持有连接的最后一个 `shared_ptr`，于是 TcpConnection 析构（#4）、Channel 析构（#1）；Channel 的析构函数访问了它的 EventLoop（IO 线程的），而那个 EventLoop 早已不存在了。

事情的经过是这样的：

1. 测试结束，ServerThread 让 baseLoop 退出（`quit`），baseLoop 的 `loop()` 返回；
2. 与此同时，客户端关闭了连接。IO 线程处理关闭，把「删除这个连接」交给 baseLoop：`baseLoop->runInLoop(...)`；
3. 但 baseLoop 已经不再循环了，这个 lambda 永远不会执行，一直留在队列中；
4. TcpServer 析构，IO 线程全部退出，它们的 EventLoop 被销毁；
5. 最后 baseLoop 的 EventLoop 析构，销毁队列中的 lambda，连接的最后一个引用随之释放。

问题有两个层面：

- Channel 的析构函数中有一个检查 `assert(!loop_->hasChannel(this))`，它要访问 EventLoop。连接的最后一个引用可能在任何线程中释放，那时它的 EventLoop 甚至可能已经没了。修改：**析构函数不访问 EventLoop**。到这个时候 Channel 早已从 Poller 中删除了，什么也不用做；
- 更危险的是：如果那个 lambda 在 TcpServer 析构**之后**被执行了（比如 baseLoop 还在运行，只是 TcpServer 被销毁了），它会调用一个已经销毁的 TcpServer 的成员函数。修改：IO 线程发给 baseLoop 的通知带上一个 `weak_ptr`，执行时先检查 TcpServer 还在不在：

```cpp
// 关闭回调在 IO 线程中执行，只能用 baseLoop 和 alive_ 的 weak_ptr，不能直接访问 TcpServer 的成员
conn->setCloseCallback([this, base = loop_, alive = std::weak_ptr<int>(alive_)](const TcpConnectionPtr& c) {
    base->runInLoop([this, alive, c] {
        if (alive.lock()) removeConnectionInLoop(c);   // 在 baseLoop 中检查：服务器的析构也在这个线程
    });
});
```

`alive_` 是 TcpServer 的一个成员 `std::shared_ptr<int>`，TcpServer 析构时它也随之销毁，`weak_ptr` 就失效了。检查和析构都在 baseLoop 的线程中，不存在「检查时还在、用的时候没了」的问题。

### 4.2 quit 与析构之间的竞争

修好上一个问题以后，TSan 又报告了一个数据竞争：

```
WARNING: ThreadSanitizer: data race (pid=233)
  Write of size 8 at 0xffaf5bc00120 by thread T8:
    #1 tinynet::EventLoop::~EventLoop() src/EventLoop.cpp:41
    #2 tinynet::EventLoopThread::threadFunc() src/EventLoopThreadPool.cpp:39
  Previous read of size 8 at 0xffaf5bc00120 by thread T7:
    #1 tinynet::EventLoop::wakeup() src/EventLoop.cpp:138
    #2 tinynet::EventLoop::quit() src/EventLoop.cpp:64
    #3 tinynet::EventLoopThread::~EventLoopThread() src/EventLoopThreadPool.cpp:16
  Location is file descriptor 9 created by thread T8 at:
    #1 tinynet::EventLoop::EventLoop() src/EventLoop.cpp:25
    #2 tinynet::EventLoopThread::threadFunc() src/EventLoopThreadPool.cpp:30
```

「Location is file descriptor 9」：TSan 不仅检查内存，还检查文件描述符。竞争的双方是：

- 线程 T7（TcpServer 析构时，让 IO 线程退出）：`quit()` 中调用 `wakeup()`，**写** eventfd；
- 线程 T8（IO 线程自己）：`~EventLoop()` 中**关闭** eventfd。

当时的 `quit` 是这样的：

```cpp
void EventLoop::quit()
{
    quit_ = true;
    if (!isInLoopThread()) wakeup();
}
```

`quit_ = true` 和 `wakeup()` 之间有一个空隙。IO 线程如果恰好在这个空隙中（因为别的事件）醒来，看到 `quit_` 已经是 `true`，就会退出循环、析构 EventLoop、关闭 eventfd；这时 T7 才执行 `wakeup()`，写的是一个已经关闭的 fd。更糟糕的是，这个 fd 号可能已经被别的线程新打开的文件或连接复用了，这一次写入就写到了别的地方。

<figure class="diagram">
<svg viewBox="0 0 700 196" role="img" aria-label="quit 与析构的竞争：线程 T7 先设置 quit_，还没来得及写 eventfd；IO 线程 T8 恰好醒来，看到 quit_ 退出循环，析构 EventLoop 并关闭 eventfd；T7 再写 eventfd，写到了一个已经关闭、甚至被重新使用的 fd">
  <defs><marker id="qr-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="qr-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="14.0" y="44.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">T7：quit()</text><text x="14.0" y="134.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">T8：IO 线程</text><rect x="110" y="22" width="150" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="185.0" y="36.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">quit_ = true</text><text x="185.0" y="52.1" text-anchor="middle" style="fill:var(--muted);font-size:11px"></text><rect x="500" y="22" width="186" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="593.0" y="36.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">wakeup()：写 eventfd</text><text x="593.0" y="52.1" text-anchor="middle" style="fill:var(--muted);font-size:11px"></text><rect x="170" y="112" width="150" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="245.0" y="126.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">醒来，看到 quit_</text><text x="245.0" y="142.1" text-anchor="middle" style="fill:var(--muted);font-size:11px"></text><rect x="340" y="112" width="150" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="415.0" y="126.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">退出 loop、析构</text><text x="415.0" y="142.1" text-anchor="middle" style="fill:var(--muted);font-size:11px"></text><rect x="500" y="112" width="186" height="36" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="593.0" y="126.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">close(eventfd)</text><text x="593.0" y="142.1" text-anchor="middle" style="fill:var(--muted);font-size:11px"></text><line x1="260" y1="40" x2="498" y2="40" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#qr-a)"/><text x="379.0" y="32.0" text-anchor="middle" style="fill:var(--link);font-size:11px">空隙</text><line x1="593" y1="58" x2="593" y2="110" style="stroke:var(--danger);stroke-width:1.5" marker-end="url(#qr-d)"/><text x="14.0" y="182.0" text-anchor="start" style="fill:var(--muted);font-size:11px">修改：quit_ = true 和 wakeup() 在锁中一起完成；析构函数先拿到同一把锁，再关闭 eventfd</text>
  </g>
</svg>
</figure>

修改：设置 `quit_` 和唤醒在锁中一起完成；析构函数在关闭 eventfd 之前，也先拿到这把锁：

```cpp
void EventLoop::quit()
{
    // 设置 quit_ 和唤醒必须在锁中一起完成：否则 loop 线程可能在两步之间看到 quit_，
    // 退出循环、析构 EventLoop、关闭 wakeupFd_，这边再写就写到了一个已经关闭（甚至被重新使用）的 fd
    std::lock_guard<std::mutex> lock(mutex_);
    quit_ = true;
    if (!isInLoopThread()) wakeup();
}

EventLoop::~EventLoop()
{
    // 别的线程可能正在 quit() 或 queueInLoop() 中往 wakeupFd_ 写数据（它们写的时候持有 mutex_）。
    // 先拿到锁，保证没有人在写了，再关闭 wakeupFd_
    std::lock_guard<std::mutex> lock(mutex_);
    // ……关闭 wakeupFd_
}
```

`queueInLoop` 中的唤醒也放进了锁中（第 2.1 节的代码），原因相同。

修改以后，全部 38 个测试在 ASan 和 TSan 下各重复 30 遍，没有任何报告。

这两个问题有一个共同点：**都发生在退出的时候**。启动和正常运行时的线程交互都经过精心设计，退出时各个线程以不同的顺序停下来，对象以不同的顺序销毁，最容易出现「一个线程还在用，另一个线程已经销毁了」。muduo 的源代码中也有一条注释承认，EventLoopThread 析构时存在「极小的可能访问已经销毁的对象」。写多线程代码，退出的路径和正常的路径一样需要测试。

## 5. 计算密集的工作

IO 线程中的回调必须很快。如果处理一个请求要做大量计算（比如压缩、加密、复杂的查询），应该交给一个单独的工作线程池：

```cpp
server.setMessageCallback([&pool](const TcpConnectionPtr& conn, Buffer* buf) {
    std::string request = buf->retrieveAllAsString();
    pool.submit([conn, request] {           // 在工作线程中计算
        std::string result = heavyCompute(request);
        conn->send(result);                 // send 是线程安全的，会交回 IO 线程发送
    });
});
```

lambda 捕获了 `conn`（`shared_ptr`），计算期间连接即使断开了，对象也还在；`send` 发现已经断开就什么也不做。

## 6. 常见错误

1. **在别的线程中直接操作连接的缓冲区或 Channel**。用 `runInLoop` 交给连接所在的线程。
2. **跨线程传递 `string_view` 或指针**。执行时指向的内存可能已经无效了。复制一份。
3. **唤醒的条件不全**。函数放进了队列，loop 却睡着，直到超时才执行。
4. **只测试正常运行，不测试退出**。生命周期的问题集中在退出时。用 Sanitizer 重复运行测试。
5. **在 IO 线程中做耗时的计算**。这个线程上所有的连接都被拖慢。

## 7. 小结

- one loop per thread：baseLoop 接受连接，IO 线程各有一个 EventLoop，连接固定属于一个线程，内部不需要锁；
- 跨线程只有一个通道：`runInLoop` / `queueInLoop`，把函数放进目标线程的队列，用 eventfd 唤醒；
- 唤醒有三种情况：别的线程、正在执行 pending 函数、loop 还没有开始；
- 跨线程的 `send` 要复制数据，连接的状态要是原子的；
- 退出时最容易出现生命周期的问题：析构函数不要访问可能已经销毁的对象，跨线程的通知要能检查目标是否还在，「设置标志」和「唤醒」要在锁中一起完成。

## 8. 练习

1. `EventLoopThreadPool` 用轮转的方式分配连接。如果有的连接很忙、有的很闲，可能出现什么问题？有什么改进的办法？
2. 为什么 `doPendingFunctors` 要把队列 `swap` 出来再执行，而不是在锁中逐个执行？
3. 多个 IO 线程时，`TcpServer::connectionCount()` 是 `std::atomic` 的。`connections_` 这个 `std::map` 为什么不需要锁？

<details>
<summary>点击查看答案</summary>

1. 轮转只保证每个线程分到的连接数差不多，不保证负载差不多。如果几个特别忙的连接恰好分到了同一个线程，这个线程会很忙，其他线程很闲。改进的办法：按每个线程当前的连接数分配（选最少的）；或者每个 IO 线程有自己的监听套接字（`SO_REUSEPORT`），由内核分配连接；但已经建立的连接不能在线程之间迁移，这是这个模型固有的限制。

2. 在锁中执行，有两个问题：一是执行期间别的线程调用 `queueInLoop` 会被阻塞，等很久；二是被执行的函数如果自己又调用 `queueInLoop`（很常见），会再次加锁，`std::mutex` 不可重入，直接死锁。`swap` 以后锁只持有一瞬间。

3. `connections_` 只在 baseLoop 的线程中访问：`newConnection`（Acceptor 的回调，在 baseLoop 中）、`removeConnectionInLoop`（通过 `runInLoop` 交给 baseLoop）、析构函数（在 baseLoop 中）。IO 线程从不直接访问它，而是把「删除」交给 baseLoop。`connectionCount_` 是给别的线程读的（比如 `/stats` 在 IO 线程中读取），所以单独用一个原子变量。

</details>

下一篇：[实战：HTTP 服务器](/posts/srv-http/)。
