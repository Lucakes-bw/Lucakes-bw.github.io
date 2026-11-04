---
title: 从 epoll 到 Reactor：事件循环
date: 2026-11-04 14:00:00
updated: 2026-11-04 14:00:00
categories:
  - 计算机网络
  - 服务器开发
tags:
  - 网络编程
  - epoll
  - Reactor
  - C++
order: 1
description: 《IO 多路复用》中的 epoll 回声服务器只有一百多行，但所有的逻辑都写在一个大循环里：哪个 fd 是监听套接字、哪个是连接、读到一半怎么办，全混在一起。这一章用 C++ 写一个小型的网络库 tinynet，把这些通用的部分抽出来。本文是第一步：把 epoll 封装成 Poller，把「一个 fd 和它的回调」封装成 Channel，再用 EventLoop 把它们组织成一个事件循环，最后用它写出一个 30 行的回声服务器。
disableNunjucks: true
---

> **本文目标**：理解 Reactor 模式：事件循环等待事件、把事件分派给回调；理解 tinynet 中 EventLoop、Poller、Channel 三个类的分工；知道为什么「一个线程最多一个 EventLoop」；会用 tinynet 写一个回声服务器。
>
> **前置知识**：《IO 多路复用》（epoll）、《TCP Socket 入门》；C++ 的 `std::function`、lambda、智能指针。本章的项目用《C++工程实践》一章的方法组织：CMake 预设、GoogleTest、Sanitizer。
>
> **运行环境**：tinynet 用到 epoll、eventfd、timerfd，只能在 Linux 上运行。本章的所有结果都是在 Docker 的 Ubuntu 24.04（GCC 13，Apple M6 上的虚拟机，2 个 CPU）中得到的；macOS 和 Windows 可以用 Docker 或 WSL。

## 0. 这一章要做什么

《IO 多路复用》的最后说：epoll 让一个线程能同时服务上万个连接，代价是编程变复杂了。任何操作都不能阻塞，一个请求的处理被拆散在多次事件中，每个连接的状态要自己保存。

如果每写一个服务器都要从头处理这些，复杂度就永远降不下来。常见的做法是写一个**网络库**，把和业务无关的部分（等待事件、接受连接、读写缓冲、关闭连接、定时器、多线程）做好，业务代码只需要回答「收到数据时做什么」。Nginx、Redis、Netty、陈硕的 muduo 都是这样组织的。

本章写一个这样的库 **tinynet**（C++17，大约 2000 行，其中一半是注释），设计上参考了 muduo，但做了简化：

| 篇 | 内容 | 主要的类 |
|---|---|---|
| 1 | 事件循环（本文） | `EventLoop`、`Poller`、`Channel` |
| 2 | 缓冲区与连接的生命周期 | `Buffer`、`TcpConnection`、`TcpServer` |
| 3 | 定时器与空闲连接 | `TimerQueue` |
| 4 | 多线程：one loop per thread | `EventLoopThreadPool` |
| 5 | 实战：HTTP 服务器 | `HttpParser`、`HttpServer` |
| 6 | 压测与性能分析 | wrk、perf |

完整的代码在 [projects/net-server/tinynet](https://github.com/Lucakes-bw/Lucakes-bw.github.io/tree/main/projects/net-server/tinynet)，也可以下载 [tinynet.zip](/downloads/tinynet.zip)。它有 38 个单元测试，在 ASan 和 TSan 下都能通过，后面的文章中会看到，其中几个 bug 正是 TSan 发现的。

## 1. Reactor 模式

先回顾《IO 多路复用》中 epoll 回声服务器的主循环（简化了一下）：

```c
for (;;) {
    int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
    for (int i = 0; i < n; i++) {
        int fd = events[i].data.fd;
        if (fd == listenfd) {
            // 接受新连接，加入 epoll
        } else if (events[i].events & EPOLLIN) {
            // 读数据，原样写回；读到 0 就关闭
        } else if (events[i].events & EPOLLOUT) {
            // 把上次没写完的写出去
        }
    }
}
```

它的结构其实很固定：**等待事件 → 看是哪个 fd、什么事件 → 执行对应的处理**。变化的只是「对应的处理」。把固定的部分写成一个通用的循环，变化的部分用回调函数交给它，这就是 **Reactor 模式**：

- 每个 fd 登记它关心的事件（可读、可写），以及事件发生时要调用的函数；
- 事件循环等待事件，事件发生了，就调用登记过的函数。

「Reactor」的意思是「反应器」：程序不主动去读、去写，而是对事件做出反应。

## 2. 三个类的分工

tinynet 用三个类实现 Reactor：

| 类 | 负责什么 | 对应上面循环中的 |
|---|---|---|
| `Poller` | 封装 epoll：登记 fd、等待事件 | `epoll_ctl`、`epoll_wait` |
| `Channel` | 一个 fd、它关心的事件、事件发生时的回调 | `if (fd == listenfd) ... else if (EPOLLIN) ...` |
| `EventLoop` | 事件循环：反复调用 Poller，把事件分派给 Channel | `for (;;)` |

<figure class="diagram">
<svg viewBox="0 0 700 250" role="img" aria-label="EventLoop 的一轮循环：Poller 调用 epoll_wait，返回发生了事件的 Channel；EventLoop 逐个调用 Channel::handleEvent，Channel 再调用它登记的读、写、关闭回调，这些回调属于 Acceptor、TcpConnection、TimerQueue 等上层对象；最后执行别的线程交来的函数">
  <defs><marker id="cl-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="cl-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="14" y="20" width="170" height="52" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="99.0" y="42.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">EventLoop::loop()</text><text x="99.0" y="58.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">while (!quit_)</text><rect x="250" y="20" width="190" height="52" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="345.0" y="42.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">Poller::poll</text><text x="345.0" y="58.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">epoll_wait → 活跃的 Channel</text><rect x="250" y="104" width="190" height="52" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="345.0" y="126.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">Channel::handleEvent</text><text x="345.0" y="142.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">按 revents 调用回调</text><rect x="250" y="188" width="190" height="46" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="345.0" y="207.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">doPendingFunctors</text><text x="345.0" y="223.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">别的线程交来的函数</text><line x1="184" y1="40" x2="248" y2="40" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#cl-a)"/><text x="216.0" y="32.0" text-anchor="middle" style="fill:var(--link);font-size:11px">① 等待事件</text><line x1="184" y1="60" x2="248" y2="124" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#cl-a)"/><text x="205.0" y="104.0" text-anchor="middle" style="fill:var(--link);font-size:11px">② 分派</text><line x1="184" y1="66" x2="248" y2="206" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#cl-a)"/><text x="205.0" y="176.0" text-anchor="middle" style="fill:var(--link);font-size:11px">③</text><rect x="500" y="20" width="186" height="52" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="593.0" y="42.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">Acceptor</text><text x="593.0" y="58.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">监听套接字：接受连接</text><line x1="440" y1="130" x2="498" y2="46" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#cl-a)"/><rect x="500" y="94" width="186" height="52" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="593.0" y="116.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">TcpConnection</text><text x="593.0" y="132.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">连接：读、写、关闭</text><line x1="440" y1="130" x2="498" y2="120" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#cl-a)"/><rect x="500" y="168" width="186" height="52" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="593.0" y="190.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">TimerQueue</text><text x="593.0" y="206.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">timerfd：定时器到期</text><line x1="440" y1="130" x2="498" y2="194" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#cl-a)"/><text x="500.0" y="246.0" text-anchor="start" style="fill:var(--muted);font-size:11px">Channel 不拥有 fd，只是 fd 和回调的接线员</text>
  </g>
</svg>
</figure>

### 2.1 Channel：一个 fd 和它的回调

```cpp
// 一个文件描述符和它关心的事件。Channel 不拥有 fd，只负责「事件来了调用哪个函数」。
// 每个 Channel 只属于一个 EventLoop，只在那个 EventLoop 的线程中使用。
class Channel {
public:
    using EventCallback = std::function<void()>;

    Channel(EventLoop* loop, int fd) : loop_(loop), fd_(fd) {}

    void handleEvent();                     // EventLoop 在 poll 返回后调用

    void setReadCallback(EventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    void enableReading() { events_ |= kReadEvent; update(); }
    void disableReading() { events_ &= ~kReadEvent; update(); }
    void enableWriting() { events_ |= kWriteEvent; update(); }
    void disableWriting() { events_ &= ~kWriteEvent; update(); }
    void disableAll() { events_ = kNoneEvent; update(); }
    void remove();                          // 从 Poller 中删除，析构之前必须调用
    // ……
private:
    void update();                          // 调用 loop_->updateChannel(this)

    EventLoop* loop_;
    const int fd_;
    std::uint32_t events_ = 0;              // 关心的事件
    std::uint32_t revents_ = 0;             // 实际发生的事件
    EventCallback readCallback_, writeCallback_, closeCallback_, errorCallback_;
};
```

`enableReading()` 修改关心的事件，然后通过 `update()` → `EventLoop::updateChannel()` → `Poller::updateChannel()` 告诉 epoll。使用者不用直接调用 `epoll_ctl`。

**Channel 不拥有 fd**：它不负责创建，也不负责关闭。fd 属于更上层的对象，比如监听套接字属于 `Acceptor`，连接属于 `TcpConnection`。Channel 只是它们和事件循环之间的「接线员」。

事件发生时，`handleEvent` 根据 `revents_` 调用对应的回调：

```cpp
void Channel::handleEventWithGuard()
{
    // 对方关闭了连接，并且没有数据可读了
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
        if (closeCallback_) closeCallback_();
    }
    if (revents_ & EPOLLERR) {
        if (errorCallback_) errorCallback_();
    }
    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
        if (readCallback_) readCallback_();
    }
    if (revents_ & EPOLLOUT) {
        if (writeCallback_) writeCallback_();
    }
}
```

注意 `EPOLLHUP` 的处理：对方关闭了连接时，epoll 可能同时报告 `EPOLLHUP` 和 `EPOLLIN`。这时缓冲区中可能还有数据没读，应该先当作可读事件处理，读到 0 字节时再关闭；只有不可读了，才直接关闭。

### 2.2 Poller：封装 epoll

```cpp
class Poller {
public:
    // 等待事件，最多 timeoutMs 毫秒，把发生了事件的 Channel 放进 active
    void poll(int timeoutMs, std::vector<Channel*>* active);
    void updateChannel(Channel* channel);   // 新增或修改关心的事件
    void removeChannel(Channel* channel);
private:
    int epollfd_;
    std::vector<epoll_event> events_;                   // epoll_wait 的结果，不够用时翻倍
    std::unordered_map<int, Channel*> channels_;        // fd -> Channel
};
```

关键在 `epoll_event` 的 `data` 字段：登记时存的不是 fd，而是 **Channel 的指针**。

```cpp
void Poller::update(int operation, Channel* channel)
{
    epoll_event ev{};
    ev.events = channel->events();
    ev.data.ptr = channel;                  // 事件发生时，直接拿到对应的 Channel
    if (::epoll_ctl(epollfd_, operation, channel->fd(), &ev) < 0) { /* …… */ }
}

void Poller::poll(int timeoutMs, std::vector<Channel*>* active)
{
    int n = ::epoll_wait(epollfd_, events_.data(), static_cast<int>(events_.size()), timeoutMs);
    for (int i = 0; i < n; ++i) {
        auto* channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->setRevents(events_[i].events);
        active->push_back(channel);
    }
    if (static_cast<std::size_t>(n) == events_.size()) events_.resize(events_.size() * 2);   // 装满了，下次多准备一些
}
```

`epoll_wait` 返回以后，不需要再根据 fd 去查「这是哪个连接」，`data.ptr` 就是答案。`events_` 一开始只有 16 个元素，某一次正好装满，说明可能还有事件没取出来，下次就翻倍。

Channel 不关心任何事件时（`disableAll()`），Poller 会用 `EPOLL_CTL_DEL` 把它从 epoll 中删掉，但仍然记在 `channels_` 中，以后再 `enable` 时重新 `EPOLL_CTL_ADD`。`Channel::index()` 记录它处在哪种状态（从未加入、在 epoll 中、暂时删除），这是一个不起眼但必须处理对的细节：对一个不在 epoll 中的 fd 调用 `EPOLL_CTL_MOD` 会返回 `ENOENT`。

### 2.3 EventLoop：事件循环

```cpp
void EventLoop::loop()
{
    assertInLoopThread();
    looping_ = true;
    while (!quit_) {
        activeChannels_.clear();
        poller_->poll(kPollTimeoutMs, &activeChannels_);
        ++iterations_;
        for (Channel* channel : activeChannels_) channel->handleEvent();   // 1. 处理 IO 事件（包括定时器）
        doPendingFunctors();                                               // 2. 执行别的线程交过来的函数
    }
    doPendingFunctors();                    // quit 之前交过来的函数也要执行，比如清理连接
    looping_ = false;
}
```

这就是整个事件循环：等待事件，逐个调用 Channel 的回调。第 2 步 `doPendingFunctors` 和多线程有关，第 4 篇再讲。

## 3. one loop per thread

EventLoop 有一条重要的规则：**一个线程最多一个 EventLoop，一个 EventLoop 只在创建它的线程中使用**。

```cpp
namespace {
thread_local EventLoop* t_loopInThisThread = nullptr;
}

EventLoop::EventLoop() : threadId_(std::this_thread::get_id()), /* …… */
{
    if (t_loopInThisThread != nullptr) LOG_FATAL("一个线程中只能有一个 EventLoop");
    t_loopInThisThread = this;
    // ……
}

void EventLoop::assertInLoopThread() const
{
    if (!isInLoopThread()) LOG_FATAL("EventLoop 只能在创建它的线程中使用");
}
```

`thread_local` 变量每个线程有一份，用它记住当前线程的 EventLoop。`Poller::updateChannel` 等函数开头都会调用 `assertInLoopThread()`，在错误的线程中调用就直接结束程序，并且给出明确的信息。

为什么要这么严格？因为这样**一个 EventLoop 内部的所有数据都只被一个线程访问，不需要加锁**：Poller 的 `channels_`、每个连接的缓冲区、连接的状态，都只在这个线程中修改。多线程的服务器用多个 EventLoop，每个线程一个，各管各的连接（第 4 篇）。需要跨线程做事情时，也不是直接调用，而是把函数「交给」目标线程的 EventLoop，让它在自己的线程中执行。

违反规则的后果，本章会遇到两次：第 4 篇中，TSan 发现的两个问题，本质上都是「在不该访问的线程中访问了 EventLoop」。

## 4. 回声服务器

有了 EventLoop，再加上下一篇的 `TcpServer` 和 `TcpConnection`，一个多线程的回声服务器只需要这些代码：

```cpp
// 回声服务器：echo [端口] [IO 线程数]
int main(int argc, char** argv)
{
    auto port = static_cast<std::uint16_t>(argc > 1 ? std::atoi(argv[1]) : 9000);
    int threads = argc > 2 ? std::atoi(argv[2]) : 0;

    EventLoop loop;
    TcpServer server(&loop, InetAddress(port), "echo");
    server.setThreadNum(threads);
    server.setConnectionCallback([](const TcpConnectionPtr& conn) {
        LOG_INFO("%s %s", conn->peerAddress().toIpPort().c_str(), conn->connected() ? "连接" : "断开");
    });
    server.setMessageCallback([](const TcpConnectionPtr& conn, Buffer* buf) { conn->send(buf); });
    server.start();
    LOG_INFO("回声服务器在 %s 上监听，%d 个 IO 线程", server.listenAddress().toIpPort().c_str(), threads);
    loop.loop();
}
```

运行，用 bash 的 `/dev/tcp` 连上去发一行：

```
$ ./build/release/echo 9000 2
[INFO] examples/echo.cpp:25 回声服务器在 0.0.0.0:9000 上监听，2 个 IO 线程
[INFO] examples/echo.cpp:21 127.0.0.1:33850 连接
[INFO] examples/echo.cpp:21 127.0.0.1:33850 断开
```

客户端收到了 `hello, echo`。和《IO 多路复用》中的版本相比，这里没有 epoll、没有 fd、没有「读到一半」的处理，只有两件事：**连接建立或断开时做什么**（打印一行），**收到数据时做什么**（原样发回）。`conn->send(buf)` 如果一次写不完，剩下的会放进输出缓冲区，等可写时再发，这些都由库处理。

`main` 函数中创建的 `loop` 叫作 **baseLoop**：它运行在主线程中，负责接受新连接。`setThreadNum(2)` 另外启动 2 个 IO 线程，每个线程有自己的 EventLoop，新连接轮流分给它们。

## 5. 常见错误

1. **在回调中做阻塞的事情**。所有回调都在事件循环的线程中执行，一个回调中调用了 `sleep`、阻塞的 `read`，或者一次很长的计算，这个线程上的所有连接都要等它。耗时的工作交给别的线程（第 4 篇）。
2. **在别的线程中直接操作 Channel 或 EventLoop**。比如在工作线程中调用 `channel->enableWriting()`，Poller 的数据结构就被两个线程同时修改了。tinynet 会用 `assertInLoopThread` 直接报错。
3. **Channel 析构之前没有 `remove()`**。Poller 中还记着它的指针，下次事件到来时访问的就是已经释放的内存。
4. **`EPOLLHUP` 时直接关闭**，丢掉了缓冲区中还没读的数据。

## 6. 小结

- Reactor 模式：事件循环等待事件，把事件分派给登记过的回调；
- tinynet 中，Poller 封装 epoll，Channel 是「一个 fd 加上它的回调」，EventLoop 是循环本身；`epoll_event.data.ptr` 存的是 Channel 的指针；
- 一个线程最多一个 EventLoop，EventLoop 内部的数据只在这个线程中访问，所以不需要锁；
- 使用者只需要提供「连接建立或断开时」和「收到数据时」的回调。

## 7. 练习

1. `Poller::poll` 的超时时间是 10 秒。如果没有任何事件，EventLoop 每 10 秒醒来一次。为什么不用 -1（永远等待）？
2. 把 `Channel::handleEventWithGuard` 中 `EPOLLHUP` 的判断去掉 `!(revents_ & EPOLLIN)` 这个条件，会出现什么问题？
3. epoll 有水平触发（LT，默认）和边缘触发（ET）两种模式。tinynet 用的是 LT。如果改成 ET，`TcpConnection` 读数据时要怎么改？

<details>
<summary>点击查看答案</summary>

1. 用 -1 也可以，tinynet 中所有需要唤醒 EventLoop 的地方（定时器、别的线程交来的函数、退出）都会产生事件，正常情况下不依赖超时。有一个超时，是为了在出现遗漏的唤醒（bug）时，循环最多卡住 10 秒而不是永远，问题更容易被发现（第 4 篇的测试中就靠它发现了一个遗漏的唤醒：测试用了 10 秒才结束）。

2. 对方发送了最后一段数据、然后关闭连接，这两件事几乎同时发生时，epoll 会同时报告 `EPOLLIN` 和 `EPOLLHUP`。去掉这个条件，会先调用关闭的回调，连接被关闭，缓冲区中最后那段数据就丢了。

3. ET 模式下，epoll 只在状态变化时通知一次（比如从「没有数据」变成「有数据」）。如果这一次没有把数据全部读完，剩下的数据不会再触发事件。所以必须循环读，直到 `read` 返回 `EAGAIN`。写也一样，必须一直写到 `EAGAIN` 或者写完。LT 模式下没读完的数据下次还会通知，代码更简单，这也是 muduo 和 tinynet 选择 LT 的原因。

</details>

下一篇：[缓冲区与连接的生命周期](/posts/srv-buffer-connection/)。`conn->send(buf)` 一次写不完时，数据去了哪里？连接什么时候被销毁？
