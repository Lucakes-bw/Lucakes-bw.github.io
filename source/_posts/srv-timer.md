---
title: 定时器与空闲连接
date: 2026-11-04 14:20:00
updated: 2026-11-04 14:20:00
categories:
  - 计算机网络
  - 服务器开发
tags:
  - 网络编程
  - 定时器
  - 堆
  - C++
order: 3
description: 服务器到处需要定时：空闲的连接要断开，请求要有超时，心跳要定期发送。事件循环中不能 sleep，定时器也要变成一种事件。本文用 Linux 的 timerfd 加上一个最小堆，实现 tinynet 的 TimerQueue：只用一个 fd，到期时和其他 IO 一样由 epoll 通知；用懒删除支持取消；最后用「每个连接一个定时器、只记最后活动时间」的方法踢掉空闲连接。
disableNunjucks: true
---

> **本文目标**：知道为什么事件循环中的定时器要和 epoll 结合；会用 timerfd；理解用最小堆管理定时器的方法，以及懒删除；会实现空闲连接的超时断开，知道时间轮等其他方案的取舍。
>
> **前置知识**：[上一篇](/posts/srv-buffer-connection/)；《堆与优先队列》。

## 1. 事件循环中的定时器

服务器中需要定时的地方很多：

- 客户端连上来以后一直不发数据，60 秒以后断开它，否则这种连接会越积越多，占满文件描述符；
- 每隔 30 秒给客户端发一次心跳；
- 一个请求转发给后端以后，3 秒没有回复就返回错误。

普通程序中可以 `sleep`，或者开一个线程去等。但在事件循环中，**任何阻塞都会让这个线程上所有的连接停下来**；每个定时器一个线程，又回到了多线程的老路。

定时器也要变成**事件**：到期时让 `epoll_wait` 返回，由事件循环调用回调。有两种做法：

1. 用 `epoll_wait` 的超时参数：每一轮把超时设为「离最近的定时器到期还有多久」；
2. 用 Linux 的 **timerfd**：一个特殊的 fd，到期时变成可读，和套接字一样加进 epoll。

tinynet 用第二种：定时器和 IO 走完全相同的路径（Channel → 回调），`epoll_wait` 的超时参数不用每次计算。

## 2. timerfd

```cpp
int timerfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);

itimerspec spec{};
spec.it_value.tv_sec = 2;                   // 2 秒以后到期
spec.it_value.tv_nsec = 0;                  // it_interval 为 0：只触发一次
::timerfd_settime(timerfd, 0, &spec, nullptr);

// 到期以后，timerfd 变成可读；read 出一个 uint64_t，表示到期了几次
std::uint64_t expirations;
::read(timerfd, &expirations, sizeof expirations);
```

- `CLOCK_MONOTONIC`：单调时钟，不受修改系统时间的影响。用 `CLOCK_REALTIME` 的话，有人把系统时间往回调一小时，定时器就晚一小时触发；
- 到期以后必须 `read` 一次，否则它一直是可读的，水平触发的 epoll 会不停地报告；
- 一个 timerfd 同时只能有一个到期时间，`timerfd_settime` 会覆盖之前的设置。

## 3. TimerQueue：一个 timerfd + 一个最小堆

服务器中可能有成千上万个定时器（每个连接一个），不可能每个都用一个 timerfd。tinynet 的做法是：

- 所有定时器放在一个**按到期时间排列的最小堆**中；
- 只用一个 timerfd，它的到期时间**总是堆顶的到期时间**，也就是最早到期的那个；
- timerfd 到期时，把堆中所有已经到期的定时器取出来执行，再把 timerfd 设为新的堆顶。

```cpp
class TimerQueue {
public:
    // 只能在 loop 的线程中调用。编号由 EventLoop 分配；interval 为 0 表示只执行一次
    void add(TimerId id, TimerCallback cb, Clock::time_point when, Clock::duration interval);
    void cancel(TimerId id);

private:
    struct Timer {
        Clock::time_point when;
        Clock::duration interval;
        TimerId id;
        TimerCallback callback;
    };
    struct Later {                          // 让 priority_queue 成为最小堆：到期早的在堆顶
        bool operator()(const Timer& a, const Timer& b) const
        {
            return a.when != b.when ? a.when > b.when : a.id > b.id;
        }
    };

    void handleRead();                      // timerfd 可读：执行所有到期的定时器
    void resetTimerfd();                    // 把 timerfd 设为堆顶的到期时间

    const int timerfd_;
    Channel timerfdChannel_;
    std::priority_queue<Timer, std::vector<Timer>, Later> heap_;
    std::unordered_set<TimerId> cancelled_;   // 已经取消、但还在堆中的定时器（懒删除）
};
```

`std::priority_queue` 默认是最大堆，比较函数反过来（`a.when > b.when`），就成了最小堆。到期时间相同时按编号排，先添加的先执行。

<figure class="diagram">
<svg viewBox="0 0 700 210" role="img" aria-label="TimerQueue：所有定时器放在一个按到期时间排列的最小堆中，堆顶是最早到期的；只有一个 timerfd，它的到期时间总是堆顶的时间；取消只是把编号放进 cancelled 集合，弹出时再丢掉">
  <defs><marker id="hp-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="hp-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="152" y="12" width="96" height="36" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="200.0" y="34.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">t=10ms #3</text><rect x="62" y="82" width="96" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="110.0" y="104.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">t=20ms #1</text><rect x="242" y="82" width="96" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="290.0" y="104.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">t=35ms #4</text><rect x="12" y="152" width="96" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="60.0" y="174.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">t=40ms #2</text><rect x="112" y="152" width="96" height="36" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="160.0" y="174.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">t=50ms #5</text><line x1="200" y1="48" x2="110" y2="82" style="stroke:var(--fg-2);stroke-width:1.5"/><line x1="200" y1="48" x2="290" y2="82" style="stroke:var(--fg-2);stroke-width:1.5"/><line x1="110" y1="118" x2="60" y2="152" style="stroke:var(--fg-2);stroke-width:1.5"/><line x1="110" y1="118" x2="160" y2="152" style="stroke:var(--fg-2);stroke-width:1.5"/><rect x="420" y="12" width="200" height="52" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="520.0" y="34.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">timerfd</text><text x="520.0" y="50.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">到期时间 = 堆顶的 10ms</text><line x1="248" y1="30" x2="418" y2="36" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#hp-a)"/><text x="333.0" y="24.0" text-anchor="middle" style="fill:var(--link);font-size:11px">堆顶决定</text><rect x="420" y="96" width="200" height="46" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="520.0" y="115.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">cancelled_ = {5}</text><text x="520.0" y="131.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">取消：O(1)，先不删</text><text x="208.0" y="178.0" text-anchor="start" style="fill:var(--danger);font-size:11px">← #5 已取消，到期弹出时丢掉</text><text x="14.0" y="204.0" text-anchor="start" style="fill:var(--muted);font-size:11px">添加 O(log n)；只有新定时器成为堆顶时才改 timerfd；到期时一次取出所有到期的，再执行</text>
  </g>
</svg>
</figure>

### 3.1 添加

```cpp
void TimerQueue::add(TimerId id, TimerCallback cb, Clock::time_point when, Clock::duration interval)
{
    loop_->assertInLoopThread();
    bool earliest = heap_.empty() || when < heap_.top().when;
    heap_.push(Timer{when, interval, id, std::move(cb)});
    if (earliest) resetTimerfd();           // 新的定时器比原来最早的还早，timerfd 要提前
}
```

只有新的定时器成为了新的堆顶，才需要修改 timerfd。插入是 O(log n)。

### 3.2 到期

```cpp
void TimerQueue::handleRead()
{
    std::uint64_t expirations = 0;
    ::read(timerfd_, &expirations, sizeof expirations);
    const auto now = Clock::now();
    std::vector<Timer> expired;             // 先全部取出来再执行：回调中可能会添加新的定时器
    while (!heap_.empty() && heap_.top().when <= now) {
        expired.push_back(heap_.top());
        heap_.pop();
    }
    for (Timer& t : expired) {
        if (cancelled_.erase(t.id) > 0) continue;       // 已经取消了
        t.callback();
        if (t.interval > Clock::duration::zero()) {     // 重复的定时器：算出下一次的时间，放回堆中
            if (cancelled_.erase(t.id) > 0) continue;   // 在回调中取消了自己
            t.when = now + t.interval;
            heap_.push(std::move(t));
        }
    }
    resetTimerfd();
}
```

先把到期的全部取出来，再逐个执行。如果边取边执行，回调中添加的一个「立即到期」的定时器又会被这个循环取出来执行，一个不停地添加自己的定时器就能让这个循环永远停不下来。

`resetTimerfd` 把 timerfd 设为新的堆顶；到期时间已经过去了的，设为 100 微秒以后，尽快触发（`it_value` 全为 0 表示停止定时器，不能用 0）。单元测试 `AddExpiredTimerInCallback` 检查的就是在回调中添加一个已经过期的定时器：它会在下一轮被执行，不会丢失，也不会卡住循环。

### 3.3 取消：懒删除

堆只能高效地删除堆顶，删除中间的某个元素要先找到它，O(n)。tinynet 的做法是**懒删除**：取消时只把编号记进 `cancelled_`，定时器留在堆中；等它到期、被取出来时，发现在 `cancelled_` 中，就直接丢掉。

```cpp
void TimerQueue::cancel(TimerId id)
{
    loop_->assertInLoopThread();
    // 堆不支持删除中间的元素：只记下来，等它到达堆顶时再丢掉
    if (id > 0) cancelled_.insert(id);
}
```

取消是 O(1)，代价是被取消的定时器还要占用内存，直到它原定的到期时间。对于「取消很频繁、到期时间又很长」的场景，堆中会积累大量的垃圾，后面会看到更好的做法。

### 3.4 编号由谁分配

`EventLoop::runAfter` 可以在任何线程中调用，必须立即返回定时器的编号（用来取消）。但 TimerQueue 只能在 loop 的线程中修改。tinynet 的做法是：编号用一个原子变量在调用者的线程中分配，真正的添加交给 loop 的线程：

```cpp
TimerId EventLoop::addTimer(TimerCallback cb, Clock::time_point when, Clock::duration interval)
{
    TimerId id = nextTimerId_++;            // std::atomic<TimerId>
    runInLoop([this, id, cb = std::move(cb), when, interval]() mutable {
        timerQueue_->add(id, std::move(cb), when, interval);
    });
    return id;
}
```

我最初的版本不是这样的：调用者用 `std::promise` 等 loop 线程把编号算出来再返回。写完回头检查时发现，如果 loop 还没有开始运行（比如在 `loop()` 之前，从另一个线程添加定时器），调用者会一直等下去。**跨线程的接口最好不要阻塞等待另一个线程**，能在本地完成的事情（分配编号）就在本地完成。

## 4. 空闲连接

### 4.1 每次收到数据都重设定时器？

最直接的做法：每个连接一个定时器，60 秒后关闭；每收到一次数据，取消旧的定时器，添加一个新的。

问题是一个活跃的连接每秒可能收到几百次数据，就要取消、添加几百次。懒删除下，被取消的定时器都留在堆中，直到 60 秒后才清理掉：1 万个活跃连接，堆中会积累上千万个垃圾定时器。

### 4.2 一个定时器 + 最后活动时间

tinynet 的 HttpServer 换了一个思路：**收到数据时只记录时间，不碰定时器**。

```cpp
// 每个连接的状态，放在 TcpConnection::context 中
struct ConnState {
    HttpParser parser;
    Clock::time_point lastActive;
};

void HttpServer::onConnection(const TcpConnectionPtr& conn)
{
    if (!conn->connected()) return;
    conn->setContext(ConnState{HttpParser(), Clock::now()});
    if (idleTimeout_.count() > 0) {
        std::weak_ptr<TcpConnection> weak(conn);   // 定时器不应该让连接一直活着
        conn->loop()->runAfter(idleTimeout_, [this, weak] { checkIdle(weak); });
    }
}

// 每个连接只有一个定时器：到期时看看最后一次活动是什么时候。
// 还没有空闲够，就按剩下的时间重新设一个；空闲够了，关闭连接。
void HttpServer::checkIdle(const std::weak_ptr<TcpConnection>& weak)
{
    TcpConnectionPtr conn = weak.lock();
    if (!conn || !conn->connected()) return;
    auto* state = std::any_cast<ConnState>(conn->context());
    auto idle = Clock::now() - state->lastActive;
    if (idle >= idleTimeout_) {
        conn->forceClose();
    } else {
        conn->loop()->runAfter(idleTimeout_ - idle, [this, weak] { checkIdle(weak); });
    }
}

void HttpServer::onMessage(const TcpConnectionPtr& conn, Buffer* buf)
{
    auto* state = std::any_cast<ConnState>(conn->context());
    state->lastActive = Clock::now();       // 只记一下时间
    // ……
}
```

<figure class="diagram">
<svg viewBox="0 0 700 150" role="img" aria-label="空闲检查的时间线：超时 1 秒，连接在 0 秒建立并设置定时器，0.6 秒收到请求只更新 lastActive；1 秒定时器到期，发现只空闲了 0.4 秒，按剩下的 0.6 秒重新设置；1.6 秒再次到期，空闲满 1 秒，关闭连接">
  <defs><marker id="id-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="id-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <line x1="40" y1="70" x2="680" y2="70" style="stroke:var(--border-2);stroke-width:2"/><line x1="40" y1="64" x2="40" y2="76" style="stroke:var(--border-2);stroke-width:1.5"/><text x="40.0" y="92.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">0 秒</text><line x1="190.0" y1="64" x2="190.0" y2="76" style="stroke:var(--border-2);stroke-width:1.5"/><text x="190.0" y="92.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">0.5 秒</text><line x1="340.0" y1="64" x2="340.0" y2="76" style="stroke:var(--border-2);stroke-width:1.5"/><text x="340.0" y="92.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">1 秒</text><line x1="490.0" y1="64" x2="490.0" y2="76" style="stroke:var(--border-2);stroke-width:1.5"/><text x="490.0" y="92.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">1.5 秒</text><line x1="640.0" y1="64" x2="640.0" y2="76" style="stroke:var(--border-2);stroke-width:1.5"/><text x="640.0" y="92.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">2 秒</text><circle cx="40" cy="70" r="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="40.0" y="30.0" text-anchor="start" style="fill:var(--fg);font-size:11px">建立，定时器 1s</text><circle cx="220.0" cy="70" r="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="220.0" y="128.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">收到请求：只记 lastActive</text><circle cx="340.0" cy="70" r="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="340.0" y="30.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">到期：只空闲了 0.4s → 再等 0.6s</text><circle cx="520.0" cy="70" r="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="520.0" y="128.0" text-anchor="end" style="fill:var(--fg);font-size:11px">到期：空闲 1s → 关闭</text>
  </g>
</svg>
</figure>

每个连接任何时候只有一个定时器在堆中。收到数据只是一次赋值；定时器到期时，如果中间有过活动，就按「还差多久」再设一个。一个连接在超时时间内收到再多的数据，定时器的操作次数也是一样的。

注意定时器的回调持有的是 `weak_ptr`：如果持有 `shared_ptr`，连接断开以后，它还要被定时器留着，直到定时器到期；`weak_ptr` 不影响连接的生命周期，到期时 `lock()` 失败就说明连接已经没了。

测试 `IdleTimeout`：超时设为 1 秒，客户端连上以后 0.6 秒发一个请求，然后什么也不做。连接在大约 1.6 秒时被关闭：中间的请求让计时重新开始了。用示例程序（超时 3 秒）实际测一下：

```
连接在 3.0041286945343018 秒后被服务器关闭
```

### 4.3 时间轮

另一种常见的方案是**时间轮**：一个环形数组，每格代表一秒，60 秒超时就有 60 格。每个连接放在「它应该被检查的那一格」中，指针每秒走一格，把那一格中的连接检查一遍。添加和删除都是 O(1)，非常适合「大量定时器、精度要求不高（秒级）」的场景，Linux 内核的定时器、Kafka 都用了时间轮的变种。

对 tinynet 来说，最小堆已经够用：一个通用的定时器队列，加上 4.2 节的技巧，空闲检查的开销就很小了。

## 5. 常见错误

1. **用 `CLOCK_REALTIME` 或者 `system_clock`**。系统时间被调整时，定时器提前或者推迟。用单调时钟。
2. **到期后没有 `read` timerfd**。epoll 不停地报告，CPU 跑满。
3. **定时器回调持有连接的 `shared_ptr`**。连接断开以后还要等定时器到期才能释放；如果是重复的定时器，就永远不会释放。
4. **边取出到期的定时器边执行**。回调中添加的立即到期的定时器又被取出来，可能形成死循环。
5. **在别的线程中直接操作 TimerQueue**。它不是线程安全的，必须通过 `runInLoop` 交给 loop 线程。

## 6. 小结

- 事件循环中的定时器必须是事件：tinynet 用 timerfd，到期时和 IO 一样由 epoll 通知；
- 所有定时器放在一个最小堆中，只用一个 timerfd，它的到期时间是堆顶的时间；
- 取消用懒删除：O(1)，代价是垃圾要等到原定时间才清理；
- 空闲连接：每个连接一个定时器，收到数据只记录时间，到期时按剩下的时间重新设置；
- 大量粗精度的定时器可以用时间轮。

## 7. 练习

1. `TimerQueue::resetTimerfd` 中，如果堆顶的定时器已经被取消了，会怎样处理？为什么要这样做？
2. 一个重复的定时器每 10 毫秒执行一次，它的回调要执行 15 毫秒。会发生什么？tinynet 中下一次的到期时间是按 `now + interval` 算的，`now` 是什么时候的时间？
3. 用时间轮实现空闲连接的检查：60 格，每格是一个 `std::unordered_set<std::shared_ptr<Entry>>`，`Entry` 析构时关闭连接，收到数据时把 `Entry` 放进当前格。为什么这样做能实现「60 秒没有活动就关闭」？（这是 muduo 书中的一个例子。）

<details>
<summary>点击查看答案</summary>

1. `resetTimerfd` 开头会把堆顶上已经取消的定时器弹出丢掉，直到堆顶是一个有效的定时器。否则 timerfd 会被设成一个已经取消的定时器的时间，到时候醒来什么也不用做，白白多一次唤醒。

2. 回调执行期间，下一次的到期时间已经过去了。`handleRead` 中的 `now` 是取出定时器之前的时间，下一次设为 `now + 10ms`，而回调结束时已经是 `now + 15ms`，所以下一次一设置就已经过期，`resetTimerfd` 会把它设为 100 微秒以后触发。结果是这个定时器实际上每 15 毫秒多一点执行一次，不会「补上」错过的那些次，也不会堆积。

3. 每个连接对应一个 `Entry`，用 `shared_ptr` 管理。收到数据时，把这个连接的 `Entry` 的 `shared_ptr` 放进当前的格子。指针每秒前进一格，并把新的当前格清空：格子中的 `shared_ptr` 被释放。一个 `Entry` 只要在最近 60 秒内被放进过任何一格，就至少还有一个格子持有它；如果 60 秒内都没有收到数据，所有持有它的格子都已经被清空了，引用计数变成 0，`Entry` 析构，关闭连接。收到数据时只需要一次插入，O(1)，不需要任何取消操作。

</details>

下一篇：[多线程：one loop per thread](/posts/srv-multithread/)。
