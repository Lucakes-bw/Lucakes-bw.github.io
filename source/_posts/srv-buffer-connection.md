---
title: 缓冲区与连接的生命周期
date: 2026-11-04 14:10:00
updated: 2026-11-04 14:10:00
categories:
  - 计算机网络
  - 服务器开发
tags:
  - 网络编程
  - Reactor
  - C++
order: 2
description: 非阻塞 IO 中，数据读到一半、写到一半是常态，所以每个连接都需要输入缓冲区和输出缓冲区。本文实现 tinynet 的 Buffer：读走数据只移动下标，空间不够时先挪动再扩容，用 readv 和栈上的临时空间一次读够数据；再实现 TcpConnection：什么时候关心可写事件、半关闭怎么做，以及最容易出错的部分：一个连接被几个地方持有，什么时候才能安全地销毁它。
disableNunjucks: true
---

> **本文目标**：理解应用层为什么需要缓冲区，掌握 Buffer 的设计（三段式、挪动和扩容、`readv`）；理解输出缓冲区和可写事件的配合；理解 TcpConnection 为什么用 `shared_ptr` 管理，以及删除一个连接时为什么要「推迟」；知道文件描述符用完时 Acceptor 怎么处理。
>
> **前置知识**：[上一篇](/posts/srv-reactor/)；《智能指针》（`shared_ptr`、`weak_ptr`、`enable_shared_from_this`）。

## 1. 为什么要有缓冲区

非阻塞的套接字上，`read` 和 `write` 都不会等：

- `read` 返回的是**当前已经到达**的数据。对方发了一个 10 KB 的请求，这一次可能只读到 3 KB，剩下的要等下一个可读事件；也可能一次读到了两个请求加上第三个的一半（TCP 是字节流，没有消息边界）；
- `write` 返回的是**这一次写进内核发送缓冲区**的字节数。要发 1 MB，可能只写进去了 200 KB，剩下的必须自己保存好，等可写事件再写。

所以每个连接都需要两个缓冲区：

- **输入缓冲区**：读到的数据先放进来，业务代码从中取出完整的消息，不完整的留着等下次；
- **输出缓冲区**：没写完的数据放进来，等可写时继续写。

业务代码（`messageCallback`）收到的是一个 `Buffer*`，而不是一次 `read` 的结果，正是这个原因。

## 2. Buffer

### 2.1 三段式的结构

```
+-------------------+------------------+------------------+
| prependable bytes |  readable bytes  |  writable bytes  |
|                   |    （数据）       |    （空闲）       |
+-------------------+------------------+------------------+
0      <=      readerIndex   <=   writerIndex    <=     size
```

```cpp
class Buffer {
public:
    static constexpr std::size_t kCheapPrepend = 8;
    static constexpr std::size_t kInitialSize = 1024;

    std::size_t readableBytes() const { return writerIndex_ - readerIndex_; }
    std::size_t writableBytes() const { return buffer_.size() - writerIndex_; }
    std::size_t prependableBytes() const { return readerIndex_; }
    const char* peek() const { return begin() + readerIndex_; }       // 第一个可读的字节
    // ……
private:
    std::vector<char> buffer_;
    std::size_t readerIndex_;
    std::size_t writerIndex_;
};
```

一个 `std::vector<char>` 加上两个下标：

- 读走数据：`readerIndex_` 往后移，**不移动数据**。如果全部读走了，两个下标都回到开头；
- 写入数据：写在 `writerIndex_` 的位置，`writerIndex_` 往后移；
- 开头预留 8 个字节（prependable）：有些协议要在消息前面加上长度，消息内容写好以后，可以直接把长度「贴」到前面，不用移动消息。

<figure class="diagram">
<svg viewBox="0 0 700 190" role="img" aria-label="Buffer 的结构：一个 vector&lt;char&gt;，前 8 个字节预留；readerIndex 到 writerIndex 之间是可读的数据，之后是空闲空间；读走数据只移动 readerIndex，写入时移动 writerIndex">
  <defs><marker id="bf-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="bf-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="14" y="60" width="80" height="46" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="54.0" y="79.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">预留</text><text x="54.0" y="95.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">8 字节</text><rect x="94" y="60" width="90" height="46" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="139.0" y="79.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">已读走</text><text x="139.0" y="95.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">空闲</text><rect x="184" y="60" width="260" height="46" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="314.0" y="79.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">readable：可读的数据</text><text x="314.0" y="95.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">peek() 指向开头</text><rect x="444" y="60" width="240" height="46" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="564.0" y="79.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">writable：空闲</text><text x="564.0" y="95.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">beginWrite() 指向开头</text><line x1="184" y1="30" x2="184" y2="58" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#bf-a)"/><text x="184.0" y="24.0" text-anchor="middle" style="fill:var(--link);font-size:11px">readerIndex</text><line x1="444" y1="30" x2="444" y2="58" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#bf-a)"/><text x="444.0" y="24.0" text-anchor="middle" style="fill:var(--link);font-size:11px">writerIndex</text><text x="14.0" y="138.0" text-anchor="start" style="fill:var(--muted);font-size:11px">retrieve(n)：readerIndex 后移 n；全部读走时两个下标都回到 8</text><text x="14.0" y="158.0" text-anchor="start" style="fill:var(--muted);font-size:11px">append：后面不够，但「已读走 + 空闲」够 → 把数据挪到前面；还不够 → resize 扩容</text><text x="14.0" y="178.0" text-anchor="start" style="fill:var(--muted);font-size:11px">readFd：readv 先填 writable，多出的读进栈上 64 KB 的临时空间，再 append</text>
  </g>
</svg>
</figure>

### 2.2 空间不够时：先挪，再扩

```cpp
void makeSpace(std::size_t len)
{
    if (writableBytes() + prependableBytes() < len + kCheapPrepend) {
        buffer_.resize(writerIndex_ + len);         // 前后的空闲加起来也不够：扩容
    } else {
        std::size_t readable = readableBytes();     // 够：把数据挪到最前面，腾出后面的空间
        std::copy(begin() + readerIndex_, begin() + writerIndex_, begin() + kCheapPrepend);
        readerIndex_ = kCheapPrepend;
        writerIndex_ = readerIndex_ + readable;
    }
}
```

读走的数据在前面留下了空闲。写入时后面的空间不够，但前后加起来够，就把还没读的数据挪到最前面，不用分配内存。单元测试 `MovesDataInsteadOfGrowing` 检查的就是这种情况：写入 800 字节、读走 700 字节以后再写入 500 字节，容量保持不变。

### 2.3 一次读多少：readv

从 fd 读数据时，事先不知道有多少数据可读。缓冲区小了，一次 `read` 读不完，要多次系统调用；每个连接的缓冲区都预先分配得很大，一万个连接就要占用大量内存。

`Buffer::readFd` 用 `readv` 一次读进两块内存：

```cpp
// 先读进缓冲区剩下的空间，不够的部分读进栈上一块 64 KB 的临时空间，再追加到缓冲区中。
ssize_t Buffer::readFd(int fd, int* savedErrno)
{
    char extrabuf[65536];
    const std::size_t writable = writableBytes();
    iovec vec[2];
    vec[0].iov_base = beginWrite();
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof extrabuf;
    const int iovcnt = writable < sizeof extrabuf ? 2 : 1;   // 缓冲区本身已经够大，就不用临时空间
    const ssize_t n = ::readv(fd, vec, iovcnt);
    if (n < 0) {
        *savedErrno = errno;
    } else if (static_cast<std::size_t>(n) <= writable) {
        writerIndex_ += static_cast<std::size_t>(n);
    } else {
        writerIndex_ = buffer_.size();
        append(std::string_view(extrabuf, static_cast<std::size_t>(n) - writable));
    }
    return n;
}
```

`readv` 按顺序填满第一块、再填第二块，一次系统调用最多能读 `writable + 64 KB` 字节。数据少时只用到缓冲区本身；数据多时，多出的部分临时放在栈上，再追加进缓冲区（这时才扩容）。临时空间在栈上，每个线程一份，不占用每个连接的内存。

测试 `ReadFdLargerThanBuffer` 从管道中读 50000 字节：缓冲区一开始只有 1 KB，检查读到的内容完全正确。

## 3. TcpConnection：读和写

### 3.1 读

```cpp
void TcpConnection::handleRead()
{
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(fd_, &savedErrno);
    if (n > 0) {
        if (messageCallback_) messageCallback_(shared_from_this(), &inputBuffer_);
    } else if (n == 0) {
        handleClose();                      // 对方关闭了连接（或者关闭了写端）
    } else if (savedErrno != EAGAIN && savedErrno != EWOULDBLOCK) {
        handleError();
        handleClose();
    }
}
```

读到 0 字节表示对方关闭了连接。业务代码在 `messageCallback` 中处理输入缓冲区，用 `retrieve` 取走处理完的部分。

### 3.2 写：先直接写，写不完再交给输出缓冲区

```cpp
void TcpConnection::sendInLoop(std::string_view data)
{
    std::size_t written = 0;
    // 输出缓冲区是空的：先直接写一次，大多数情况下能全部写完，就不用经过缓冲区了
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
        ssize_t n = ::write(fd_, data.data(), data.size());
        if (n >= 0) written = static_cast<std::size_t>(n);
        // ……错误处理
    }
    // 没写完的放进输出缓冲区，等可写事件
    if (written < data.size()) {
        outputBuffer_.append(data.substr(written));
        if (!channel_->isWriting()) channel_->enableWriting();
    }
}

void TcpConnection::handleWrite()
{
    ssize_t n = ::write(fd_, outputBuffer_.peek(), outputBuffer_.readableBytes());
    // ……
    outputBuffer_.retrieve(static_cast<std::size_t>(n));
    if (outputBuffer_.readableBytes() == 0) {
        channel_->disableWriting();         // 发完了，不再关心可写事件，否则 epoll 会不停地报告
        if (state_ == kDisconnecting) shutdownInLoop();   // 之前要求过关闭，现在可以关了
    }
}
```

两个要点：

**只在需要时关心可写事件。** 套接字的发送缓冲区大部分时间都有空间，也就是一直「可写」。如果一直关心 `EPOLLOUT`，水平触发的 epoll 会每一轮都报告这个连接可写，`epoll_wait` 立即返回，CPU 空转到 100%。正确的做法是：输出缓冲区有数据时才 `enableWriting()`，写完立即 `disableWriting()`。

**顺序不能乱。** 如果输出缓冲区中还有上次没写完的数据，新数据就不能直接 `write`，否则会插队到旧数据前面。所以直接写的条件是「没有在等可写事件，并且输出缓冲区是空的」。

测试 `LargeMessage` 发送 1 MB 的数据：本地回环上内核的发送缓冲区只有几百 KB，服务器一次写不完，必须经过输出缓冲区和可写事件，才能把 1 MB 完整地发回来。

### 3.3 高水位

如果对方一直不读（或者读得很慢），而服务器不停地 `send`，输出缓冲区就会无限增长，直到内存耗尽。`setHighWaterMarkCallback` 在输出缓冲区超过一个阈值时通知业务代码，业务代码可以停止读取上游的数据，或者断开这个连接。

### 3.4 半关闭

```cpp
void TcpConnection::shutdown()
{
    if (state_ != kConnected) return;
    state_ = kDisconnecting;
    auto self = shared_from_this();
    loop_->runInLoop([this, self] { shutdownInLoop(); });
}

void TcpConnection::shutdownInLoop()
{
    if (!channel_->isWriting()) sockets::shutdownWrite(fd_);   // 还有数据没发完，等 handleWrite 发完再关
}
```

服务器想「发完这些数据就结束」时，不能直接 `close`：输出缓冲区中可能还有数据没发出去，`close` 以后就丢了。`shutdown` 只关闭写端（发送 FIN），并且等到输出缓冲区发完才真正关闭；读端还开着，对方后续发来的数据、对方的 FIN 都还能收到，等对方也关闭时，`read` 返回 0，再走正常的关闭流程。

测试 `HalfClose`：服务器收到数据，回一个加了 `!` 的副本，然后 `shutdown`；客户端读到 `bye!`，再读到连接关闭。

## 4. 连接的生命周期

这是网络库中最容易出错的部分。

### 4.1 一个连接被谁持有

一个 `TcpConnection` 可能同时被这几个地方用到：

- `TcpServer` 的 `connections_`：记录所有的连接；
- 正在执行的回调：`messageCallback` 的参数；
- 业务代码：比如一个聊天服务器，把在线用户的连接保存起来，有消息时推送给他；
- 定时器：第 3 篇的空闲检查，要在几秒以后访问这个连接。

连接断开时，谁来 `delete` 它？任何一方删除了，其他几方手中的指针就悬空了。所以 TcpConnection 用 `std::shared_ptr` 管理：**最后一个持有者放手时，才析构，才关闭 fd**。

```cpp
TcpConnection::~TcpConnection()
{
    assert(state_ == kDisconnected);
    sockets::close(fd_);                    // fd 由 TcpConnection 拥有，最后一个 shared_ptr 放手时关闭
}
```

fd 在析构函数中关闭，这一点很重要：只要还有人持有这个连接，fd 就不会被关闭，也就不会被操作系统分配给一个新的连接。否则会出现一种很难查的 bug：旧连接的指针还在，往它的 fd 上写数据，写到了一个刚建立的新连接上。

### 4.2 删除一个连接：为什么要推迟

对方关闭连接时的调用顺序是：

```
EventLoop::loop
  └─ Channel::handleEvent                   ← 正在执行 Channel 的成员函数
       └─ TcpConnection::handleRead          读到 0 字节
            └─ TcpConnection::handleClose
                 └─ closeCallback：通知 TcpServer 删除这个连接
```

如果 TcpServer 在这里直接从 `connections_` 中删除它，而这是最后一个 `shared_ptr`，TcpConnection 会立即析构，连同它的 Channel 一起。可是调用栈的上面，`Channel::handleEvent` 还没有返回：返回以后继续执行的，是一个已经被销毁的对象的成员函数。

tinynet 用两层保护：

```cpp
void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
{
    connections_.erase(conn->name());
    // 不能在这里直接析构：可能正处在这个连接的 Channel::handleEvent 中。
    // 用 queueInLoop 推迟到本轮事件处理完以后，lambda 持有的 conn 是最后一个 shared_ptr
    conn->loop()->queueInLoop([conn] { conn->connectDestroyed(); });
}
```

1. **推迟**：`queueInLoop` 中的函数在本轮所有事件处理完以后才执行（EventLoop::loop 的第 2 步）。那时调用栈已经回到了 `loop()`，不在任何 Channel 的成员函数中了。lambda 捕获了 `conn`，保证它一直活到那个时候；lambda 执行完、被销毁时，才是最后一个 `shared_ptr` 放手的时刻；
2. **tie**：连接建立时，`channel_->tie(shared_from_this())` 让 Channel 记住它的主人（一个 `weak_ptr`）。`handleEvent` 开始时先把它提升成 `shared_ptr`，处理事件期间主人一定活着：

```cpp
void Channel::handleEvent()
{
    if (tied_) {
        std::shared_ptr<void> guard = tie_.lock();      // 处理事件期间，owner 不会被析构
        if (guard) handleEventWithGuard();
    } else {
        handleEventWithGuard();
    }
}
```

<figure class="diagram">
<svg viewBox="0 0 700 232" role="img" aria-label="删除一个连接的过程：handleRead 读到 0，handleClose 通知 TcpServer，TcpServer 从 connections_ 中删除，用 queueInLoop 把 connectDestroyed 推迟到本轮事件处理完以后，lambda 持有最后一个 shared_ptr，执行完后连接析构、关闭 fd">
  <defs><marker id="lf-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="lf-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="14" y="20" width="210" height="56" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="119.0" y="44.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">Channel::handleEvent</text><text x="119.0" y="60.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">tie：先 lock() 出 shared_ptr</text><line x1="224" y1="48" x2="242" y2="48" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lf-a)"/><rect x="244" y="20" width="210" height="56" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="349.0" y="44.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">handleRead 读到 0</text><text x="349.0" y="60.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">→ handleClose</text><line x1="454" y1="48" x2="472" y2="48" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lf-a)"/><rect x="474" y="20" width="210" height="56" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="579.0" y="44.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">TcpServer 删除</text><text x="579.0" y="60.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">connections_.erase</text><rect x="14" y="120" width="210" height="56" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="119.0" y="144.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">queueInLoop</text><text x="119.0" y="160.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">lambda 持有 conn</text><line x1="224" y1="148" x2="242" y2="148" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lf-a)"/><rect x="244" y="120" width="210" height="56" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="349.0" y="144.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">本轮事件处理完</text><text x="349.0" y="160.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">connectDestroyed</text><line x1="454" y1="148" x2="472" y2="148" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lf-a)"/><rect x="474" y="120" width="210" height="56" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="579.0" y="144.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">lambda 销毁</text><text x="579.0" y="160.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">最后一个引用：析构、关闭 fd</text><path d="M 584 76 C 584 100, 124 96, 124 118" style="fill:none;stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#lf-a)"/><text x="14.0" y="222.0" text-anchor="start" style="fill:var(--muted);font-size:11px">上面三步都还在 Channel::handleEvent 的调用栈中，此时绝不能析构；推迟到下面，才安全</text>
  </g>
</svg>
</figure>

`handleClose` 中也有一个同样的保护：

```cpp
void TcpConnection::handleClose()
{
    if (state_ == kDisconnected) return;
    state_ = kDisconnected;
    channel_->disableAll();
    TcpConnectionPtr guard(shared_from_this());           // 下面的回调中 TcpServer 会删掉它持有的指针
    if (connectionCallback_) connectionCallback_(guard);
    if (closeCallback_) closeCallback_(guard);
}
```

单元测试 `ManyClientsAndConnectionCount` 建立 20 个连接，检查服务器的连接数是 20；客户端全部关闭以后，检查连接数回到 0，并且在 ASan 下没有任何内存错误。

### 4.3 文件描述符用完了

`Acceptor` 负责接受新连接。有一种特殊情况：进程的文件描述符用完了（`EMFILE`），`accept` 失败，连接一直留在监听队列中。水平触发的 epoll 会不停地报告监听套接字可读，`accept` 不停地失败，CPU 跑满，什么也做不了。

tinynet 的做法是事先打开一个 `/dev/null` 占住一个 fd：

```cpp
if (errno == EMFILE) {
    // 先关掉预留的 fd，腾出一个位置把这个连接接受下来、立即关闭，再把预留的 fd 打开
    ::close(idleFd_);
    idleFd_ = ::accept(acceptFd_, nullptr, nullptr);
    ::close(idleFd_);
    idleFd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    LOG_ERROR("文件描述符用完了，拒绝了一个连接");
}
```

对方会看到连接被立即关闭，而不是一直卡在握手之后。更根本的办法是提高文件描述符的上限（`ulimit -n`），并限制最大连接数。

## 5. 常见错误

1. **一直关心可写事件**。CPU 空转到 100%。只在输出缓冲区有数据时关心。
2. **输出缓冲区有数据时直接 `write` 新数据**。数据乱序。
3. **在回调中直接 `delete` 连接**，或者在 `handleClose` 中立即析构。返回以后访问已经释放的内存。
4. **在析构之前就关闭了 fd**。fd 被新连接复用，旧的指针写到了新连接上。
5. **用 `close` 代替 `shutdown`**。输出缓冲区中没发完的数据丢失，对方收到的响应不完整。
6. **假设一次 `read` 就是一条完整的消息**。TCP 是字节流，必须在输入缓冲区中自己找消息的边界（第 5 篇的 HTTP 解析器）。

## 6. 小结

- 非阻塞 IO 中，每个连接需要输入、输出两个缓冲区；
- Buffer：`vector<char>` 加两个下标，读走数据只移动下标，空间不够先挪动再扩容，`readv` 加 64 KB 的栈空间一次读够；
- 写数据：先尝试直接写，写不完的放进输出缓冲区，只在这时关心可写事件；
- TcpConnection 用 `shared_ptr` 管理，fd 在析构时才关闭；删除连接用 `queueInLoop` 推迟到本轮事件处理完以后，`tie` 保证处理事件期间连接不被析构。

## 7. 练习

1. `Buffer` 的 `retrieve` 在全部读走时把两个下标都设回 `kCheapPrepend`。如果不这样做，会有什么影响？
2. 为什么 `readFd` 中临时空间是 64 KB？取 1 MB 会怎样？
3. 用 `nc` 连上回声服务器，发送数据，然后让 `nc` 停止读取（比如按 Ctrl+Z 暂停它），继续用另一个程序往这个连接写大量数据。服务器的内存会怎样变化？怎样用高水位回调保护服务器？

<details>
<summary>点击查看答案</summary>

1. 不重置的话，下标会一直往后移，直到写入时 `makeSpace` 发现后面的空间不够，才把数据挪到前面。功能上没有问题，但会多做一些挪动；而重置是 O(1) 的，最常见的情况（一次读到的数据被完整处理掉）几乎不需要挪动。

2. 64 KB 足够覆盖大多数情况下一次能读到的数据（内核的接收缓冲区默认是几十到几百 KB），同时栈上的 64 KB 对线程栈（Linux 默认 8 MB）来说很小。1 MB 的话，一次系统调用能读更多，但大部分时候用不上；另外 IO 线程的栈如果设得比较小，可能会溢出。

3. 回声服务器把收到的数据原样 `send` 回去，客户端不读，内核的发送缓冲区很快就满了，后面的数据全部堆在输出缓冲区中，内存持续增长。可以设置高水位回调：输出缓冲区超过比如 64 MB 时，`conn->forceClose()`；或者更温和一点，停止从这个连接读取（`disableReading`），等输出缓冲区降下来（`writeCompleteCallback`）再恢复读取，这就是**背压**（back pressure）。

</details>

下一篇：[定时器与空闲连接](/posts/srv-timer/)。
