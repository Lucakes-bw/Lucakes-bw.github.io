---
title: 断线检测与重连
date: 2026-10-29 10:00:00
updated: 2026-10-29 10:00:00
categories:
  - 客户端开发
  - 网络与数据
tags:
  - Qt
  - C++
  - TCP
  - 心跳
order: 2
description: 服务器进程被 kill -9，客户端立即收到 disconnected：操作系统替死掉的进程关闭了连接。可是如果服务器卡死了（用 SIGSTOP 暂停进程来模拟），连接仍然「正常」，客户端 8 秒里没有收到任何信号，发出的消息也像发出去了一样。连接一个没有回应的地址，等了 59.7 秒才报超时。本文给连接加上应用层的心跳：每 2 秒发一次 ping，5 秒收不到任何数据就认为断线，实测在服务器卡死后约 5 秒发现；再用指数退避加随机抖动自动重连，断线期间的消息先存着，连上后补发。实验中还出现了两个意外：服务器进程暂停时，重连居然「成功」了，因为握手是内核替它完成的；在判定断线前写进旧连接的那条消息永远丢了，客户端无从知道它有没有送到。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：知道客户端在各种断线情况下能否发现、多久能发现：服务器进程退出、服务器卡死、连接一个没有回应的地址；理解为什么需要应用层的心跳，会实现心跳超时；会用指数退避加随机抖动自动重连；会在断线期间缓存要发送的消息；理解「写进套接字」不等于「对方收到」，知道什么时候需要应用层的确认。
>
> **前置知识**：上一篇《QTcpSocket 与消息分帧》；计算机网络专栏《TCP 连接管理》（四次挥手、RST）、《TCP 的常见问题》（listen 的 backlog）、《TCP 的可靠传输》（重传）；Linux 基础专栏《信号》（`SIGSTOP`、`SIGCONT`）。
>
> **实验环境**：macOS 27.0，Qt 6.12.0。服务器是一个 Python 写的回显服务器：使用上一篇的「4 字节长度 + JSON」格式，收到 `ping` 回复 `pong`，收到其他消息原样发回。服务器和客户端都在本机。客户端每 3 秒发一条聊天消息。

## 1. 断线的几种情况

客户端需要知道连接是不是还活着：断了就提示用户、自动重连，而不是让用户对着一个「已连接」的界面发消息，却永远收不到回复。可是「断线」有很多种，客户端能不能发现、多快发现，差别很大。

### 1.1 服务器进程退出

服务器进程被 `kill -9` 杀掉，5 秒后重新启动。客户端用第 3 节的 `Connection` 类，先不开心跳：

```
client    0.0 s  已连接（第 1 次尝试）
client    3.1 s  收到回显：消息 1
client    3.7 s  断开：对方关闭了连接
client    3.7 s  584 ms 后进行第 2 次重连
client    4.3 s  断开：错误：Connection refused
client    4.3 s  1003 ms 后进行第 3 次重连
client    5.3 s  断开：错误：Connection refused
client    5.3 s  2336 ms 后进行第 4 次重连
client    7.6 s  断开：错误：Connection refused
client    7.6 s  3238 ms 后进行第 5 次重连
client   10.9 s  已连接（第 5 次尝试）
client   10.9 s  补发断线期间的 2 条消息
client   10.9 s  收到回显：消息 2
client   10.9 s  收到回显：消息 3
client   12.2 s  收到回显：消息 4
```

服务器被杀掉的同一时刻（3.7 秒），客户端就收到了 `disconnected`。进程即使是被强行杀死的，没有机会执行任何代码，**操作系统也会替它关闭所有的套接字**，向对方发出 FIN（或 RST），客户端立即就知道了。

之后每次重连都被拒绝（`Connection refused`）：服务器的端口上没有进程在监听，对方的内核直接回复 RST，同样立即就知道。重连的间隔逐渐变长，第 3 节解释。服务器重启后，第 5 次尝试连上了，断线期间的两条消息也补发了。

崩溃、正常退出、被杀掉，这类断线都是**能立即发现**的。

### 1.2 服务器卡死

另一种情况是服务器进程还在，但不再处理任何事情了：死锁、死循环、机器负载太高。用 `SIGSTOP` 暂停服务器进程来模拟，8 秒后用 `SIGCONT` 恢复，同样不开心跳：

```
harness: 第 4 秒暂停服务器（SIGSTOP）
harness: 第 12 秒恢复服务器（SIGCONT）
client    0.0 s  已连接（第 1 次尝试）
client    3.0 s  收到回显：消息 1
client   11.4 s  收到回显：消息 2
client   11.4 s  收到回显：消息 3
client   12.0 s  收到回显：消息 4
client   15.0 s  收到回显：消息 5
```

（客户端比测试脚本晚 0.5 秒启动，所以它的时间比脚本的时间少 0.5 秒左右。）

服务器暂停的 8 秒里，客户端**没有收到任何信号**：没有 `disconnected`，也没有错误。第 6 秒和第 9 秒发出的消息 2、3，`write` 都成功了。直到服务器恢复，两条回显才一起到达。

原因是：进程暂停了，但**内核没有暂停**。客户端发来的数据，服务器的内核照常接收、照常回复 ACK，放在接收缓冲区里等进程来读。在客户端的 TCP 看来，一切正常。

这里的「卡死」只持续了 8 秒。如果是死锁，它会一直持续下去，客户端会永远显示「已连接」，用户发的消息永远没有回复。

### 1.3 对方完全没有回应

最难发现的是对方的机器直接从网络上消失了：网线被拔、Wi-Fi 断开、对方的机器断电，或者中间的路由器、防火墙丢掉了所有的包。这时不会有 FIN，也不会有 RST，什么都没有。

在本机上不好模拟拔网线，但可以模拟一个类似的情况：连接一个**不会有任何回应**的地址（`10.255.255.1` 是一个私有地址，在实验的网络中不存在，发过去的 SYN 石沉大海）：

```cpp
QTcpSocket s;
QObject::connect(&s, &QTcpSocket::errorOccurred, [&] {
    std::printf("%.1f s 后 errorOccurred：%s\n", clock.elapsed() / 1000.0, qPrintable(s.errorString()));
});
clock.start();
s.connectToHost("10.255.255.1", 80);
```

```
59.7 s 后 errorOccurred：Connection timed out
```

等了将近 60 秒才放弃。这期间内核一直在按指数退避重传 SYN（网络专栏《TCP 的可靠传输》中的重传机制），Qt 只是在等内核的结果。对一个界面程序来说，让用户看着「正在连接」转一分钟是不能接受的。

已经建立的连接遇到这种情况更糟：如果客户端没有发送数据，**它永远不会知道**连接已经断了；如果发送了数据，内核要重传十几次、几分钟到十几分钟才会放弃。

> TCP 有一个保活（keepalive）机制，`socket.setSocketOption(QAbstractSocket::KeepAliveOption, 1)` 可以打开。但它的默认参数是连接空闲 2 小时后才开始探测，对客户端来说太慢了。修改探测间隔需要使用各个操作系统不同的套接字选项，所以实际的程序一般在应用层自己做心跳。

### 1.4 小结一下

| 情况 | 客户端能否发现 | 多久 |
|---|---|---|
| 服务器进程退出、崩溃、被杀 | 能，收到 FIN / RST | 立即 |
| 服务器端口上没有程序（重连时） | 能，`Connection refused` | 立即 |
| 服务器卡死（进程在，但不处理） | **不能**，内核照常接收数据 | 永远 |
| 网络中断、对方断电 | 发送数据时最终能发现 | 几分钟到十几分钟；不发送就永远不知道 |
| 连接一个没有回应的地址 | 能 | 约 60 秒 |

后三种情况，只能由应用层自己检测。

## 2. 应用层心跳

**心跳**（heartbeat）的思路很简单：客户端定期发一个 `ping`，服务器回一个 `pong`；如果**太久没有收到对方的任何数据**，就认为连接已经断了，主动关闭并重连。

```cpp
m_pingTimer.setInterval(2000);       // 每 2 秒发一次 ping
m_deadTimer.setInterval(5000);       // 5 秒没收到任何数据就认为断线
m_deadTimer.setSingleShot(true);

connect(&m_pingTimer, &QTimer::timeout, this, [this] { writeFrame({{"type", "ping"}}); });
connect(&m_deadTimer, &QTimer::timeout, this, [this] {
    emit log("5 秒没有收到任何数据，认为连接已断开");
    onDisconnected("心跳超时");
    m_socket.abort();                 // abort 也会发出 disconnected，重连已经安排好了，会被忽略
});

void Connection::onReadyRead()
{
    if (m_heartbeat)
        m_deadTimer.start();          // 收到任何数据都说明连接还活着，重新计时
    // ……上一篇的分帧
}
```

- 判断的依据是「**收到任何数据**」，而不只是 `pong`：正常的聊天消息同样说明连接是活的。
- 超时时间（5 秒）要比发送间隔（2 秒）长得多，允许丢掉一两个心跳，避免网络稍有波动就误判断线。
- 用 `abort()` 而不是 `disconnectFromHost()`：后者会先把发送缓冲区中的数据发完、再走四次挥手，可是对方已经没有回应了，等不到结果。`abort()` 立即丢弃连接。

最初写这段代码时，注释写的是「`abort` 不会发出 `disconnected`」，所以先调用 `abort()`，再自己调用 `onDisconnected("心跳超时")`。运行后，日志中的断线原因却是「对方关闭了连接」：`abort()` 关闭一个已连接的套接字时，同样会发出 `disconnected`，这个信号先到，把原因记成了别的。所以要先记录原因、安排重连，再 `abort()`。

再做一次 1.2 节的「服务器卡死」实验，这次打开心跳：

```
client    0.0 s  已连接（第 1 次尝试）
client    3.0 s  收到回显：消息 1
client    8.3 s  5 秒没有收到任何数据，认为连接已断开
client    8.3 s  断开：心跳超时
client    8.3 s  573 ms 后进行第 2 次重连
client    8.8 s  已连接（第 2 次尝试）
client   11.7 s  收到回显：消息 3
client   12.0 s  收到回显：消息 4
client   15.0 s  收到回显：消息 5
```

服务器被暂停后，客户端再也收不到 `pong`；从最后一次收到数据算起 5 秒，到第 8.3 秒，客户端判定断线。卡死的服务器，现在能在 5 秒左右发现了。

但是这个结果中还有两个意外。

### 2.1 意外一：「连上」了一个卡死的服务器

8.8 秒时，重连**成功了**，而服务器这时还是暂停的。原因是 TCP 的三次握手是**内核**完成的，不需要服务器进程参与：内核收到 SYN，回复 SYN+ACK，握手完成后把连接放进 `listen` 的队列中，等进程来 `accept`（网络专栏《TCP 的常见问题》中的 backlog）。服务器进程暂停了，没有 `accept`，但客户端已经收到了 `connected`。

所以「**连接成功」不代表「服务可用」**。这次的新连接上，心跳照样会检测：如果服务器 5 秒内还没恢复，就会再次判定断线。实验中服务器在第 11.5 秒左右恢复，处理了积压的数据，没有触发第二次超时。实际的程序中，连接建立后通常还要等服务器回复一条「欢迎」或者「登录成功」的消息，才把状态显示为「已连接」。

### 2.2 意外二：消息 2 丢了

回显中有消息 1、3、4、5，**没有消息 2**。

消息 2 是在第 6 秒发出的，这时连接还没有被判定断线，`send` 直接把它写进了旧的套接字，`write` 也成功返回了。它被服务器的内核收下，放在了接收缓冲区中。第 8.3 秒，客户端 `abort()` 了这个连接，服务器恢复后，这个连接已经被重置，缓冲区中的数据随之丢弃。

客户端没有任何办法知道消息 2 的命运。`write` 成功只代表数据进了**本机的**发送缓冲区；即使 TCP 收到了 ACK，也只代表对方的**内核**收到了，不代表对方的**程序**处理了。第 3 节的「断线期间缓存消息」只能处理「已知断线时」发的消息，管不了「断线了但还不知道」的这段时间。

需要保证送达的消息（聊天消息、支付请求），只能在应用层做**确认**：

1. 客户端给每条消息一个唯一的编号，发送后放进一个「等待确认」的列表；
2. 服务器处理完后，回复一条带同样编号的确认消息；
3. 客户端收到确认后，把消息从列表中删除；重连后，把列表中还没确认的消息重新发送一遍；
4. 消息可能被发送两次（服务器处理了、确认还没到就断了），所以服务器要根据编号去重。

这正是很多聊天软件中消息旁边那个「发送中」的小圈圈：它在等服务器的确认。第 5 章的聊天客户端会实现它。

## 3. 自动重连

### 3.1 指数退避和随机抖动

断线后立即重连，连不上就再立即重连，看起来最快，其实很糟糕：服务器宕机的这段时间里，客户端每秒尝试成百上千次，白白消耗电量和网络；如果有一万个客户端，服务器恢复的那一刻会同时收到一万个连接请求，可能刚启动就又被压垮了。

常用的做法是**指数退避**（exponential backoff）加**随机抖动**（jitter）：

- 第 1 次失败后等 0.5 秒，第 2 次 1 秒，然后 2、4、8 秒……每次翻倍，设一个上限（这里是 30 秒）；
- 每次的等待时间再随机加减 20%，让大量客户端的重连时间错开，不要同时涌向服务器；
- 连接成功后，计数清零。

```cpp
// 指数退避：0.5、1、2、4、8 秒……最多 30 秒，再加上 ±20% 的随机抖动
void Connection::scheduleReconnect()
{
    int base = std::min(500 * (1 << std::min(m_attempt, 10)), 30000);
    int jitter = int(base * (QRandomGenerator::global()->bounded(0.4) - 0.2));
    ++m_attempt;
    emit log(QString("%1 ms 后进行第 %2 次重连").arg(base + jitter).arg(m_attempt + 1));
    m_retryTimer.start(base + jitter);
}
```

1.1 节的输出中，等待时间依次是 584、1003、2336、3238 毫秒，围绕 500、1000、2000、4000 上下浮动。网络专栏中 TCP 重传 SYN 用的也是指数退避。

`std::min(m_attempt, 10)` 防止移位的位数过大：`1 << 40` 对 `int` 是未定义行为（C 语言专栏《运算符与表达式》）。

### 3.2 完整的 Connection 类

```cpp
// 带心跳和自动重连的连接
class Connection : public QObject
{
    Q_OBJECT
public:
    Connection(QString host, quint16 port, bool heartbeat);
    ~Connection() override { m_socket.disconnect(this); }   // 析构时套接字关闭，不要再触发重连
    void send(const QJsonObject &msg);      // 断线时先存起来，连上后再发
    void start() { connectNow(); }

signals:
    void message(const QJsonObject &msg);
    void log(const QString &text);

private:
    void connectNow();
    void onReadyRead();
    void onDisconnected(const QString &why);
    void scheduleReconnect();
    void writeFrame(const QJsonObject &msg);

    QString m_host;
    quint16 m_port;
    bool m_heartbeat;
    QTcpSocket m_socket;
    QByteArray m_buffer;
    QTimer m_pingTimer;        // 定时发 ping
    QTimer m_deadTimer;        // 太久没收到任何数据就认为断线
    QTimer m_retryTimer;
    int m_attempt = 0;
    QList<QJsonObject> m_pending;   // 断线期间要发的消息
};
```

```cpp
Connection::Connection(QString host, quint16 port, bool heartbeat)
    : m_host(std::move(host)), m_port(port), m_heartbeat(heartbeat)
{
    // ……第 2 节的定时器设置

    connect(&m_socket, &QTcpSocket::connected, this, [this] {
        emit log(QString("已连接（第 %1 次尝试）").arg(m_attempt + 1));
        m_attempt = 0;
        m_buffer.clear();                 // 旧连接中没收完的半条消息要丢掉
        if (m_heartbeat) {
            m_pingTimer.start();
            m_deadTimer.start();
        }
        if (!m_pending.isEmpty())
            emit log(QString("补发断线期间的 %1 条消息").arg(m_pending.size()));
        for (const QJsonObject &m : std::exchange(m_pending, {}))
            writeFrame(m);
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, &Connection::onReadyRead);
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] { onDisconnected("对方关闭了连接"); });
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_socket.state() != QAbstractSocket::ConnectedState)
            onDisconnected("错误：" + m_socket.errorString());
    });
    connect(&m_retryTimer, &QTimer::timeout, this, &Connection::connectNow);
}

void Connection::connectNow()
{
    m_socket.abort();
    m_socket.connectToHost(m_host, m_port);
}

void Connection::send(const QJsonObject &msg)
{
    if (m_socket.state() == QAbstractSocket::ConnectedState)
        writeFrame(msg);
    else
        m_pending << msg;
}

void Connection::onDisconnected(const QString &why)
{
    if (m_retryTimer.isActive())
        return;                           // 已经在等待重连了（disconnected 和 errorOccurred 可能都会来）
    m_pingTimer.stop();
    m_deadTimer.stop();
    emit log("断开：" + why);
    scheduleReconnect();
}
```

几个细节：

- **一次断线可能收到两个信号**：连接被对方重置时，`errorOccurred` 和 `disconnected` 都会发出。`onDisconnected` 开头检查「是不是已经在等待重连了」，避免安排两次重连、`m_attempt` 加两次。
- **重连时清空接收缓冲区**：旧连接中可能收到了半条消息，新连接的数据不能接在它后面。
- **`std::exchange(m_pending, {})`** 把待发送的列表取出来，同时把成员清空（C++14 专栏《C++14 标准库的补充》）。
- **析构函数中断开套接字的信号**：最初没有这一行，程序退出时打印出了 `QObject::startTimer: Timers cannot be started from another thread`。原因是 `Connection` 析构时，成员 `m_socket` 被关闭，发出了 `disconnected`，触发了 `scheduleReconnect()`，这时程序已经在退出，定时器已经无法启动。析构时先断开和自己的连接，就不会在半析构的状态下处理信号了。

## 4. 小结

- 服务器进程退出（包括崩溃、被杀）时，操作系统会关闭它的连接，客户端立即收到 `disconnected`；端口上没有程序时，连接立即被拒绝。
- 服务器卡死时，内核照常接收数据、回复 ACK，客户端收不到任何信号；网络中断时，不发数据就永远不知道；连接一个没有回应的地址，要等约 60 秒才超时。TCP 的 keepalive 默认太慢。
- 应用层心跳：定期发 `ping`，太久没有收到任何数据就 `abort()` 并重连。实验中卡死的服务器在约 5 秒后被发现。
- 握手由内核完成，「连上」不代表服务可用，要等服务器的应用层回复。
- `write` 成功不代表对方收到。判定断线之前写进旧连接的消息可能丢失，需要保证送达的消息要做应用层确认，重连后重发没有确认的消息，服务器按编号去重。
- 重连用指数退避加随机抖动，避免频繁重试和大量客户端同时涌向服务器；断线期间的消息先缓存，连上后补发。

TCP 长连接适合聊天这种需要服务器主动推送的场合。客户端的另一类常见需求是请求一个 Web API：登录、拉取联系人列表、上传头像。下一篇用 `QNetworkAccessManager` 发 HTTP 请求，处理 JSON。

## 练习

1. 给第 3 节的 `Connection` 加一个「连接超时」：`connectToHost` 之后 5 秒还没有 `connected`，就 `abort()` 并安排重连。再运行 1.3 节的实验，现在要多久才开始重试？
2. 实现第 2.2 节的应用层确认：每条聊天消息带一个递增的 `id`，测试服务器处理后回复 `{"type": "ack", "id": ...}`。重新做「服务器卡死」的实验，确认消息 2 在重连后被重发了。服务器怎样避免同一条消息被处理两次？
3. 心跳的间隔和超时应该怎样选？太短和太长分别有什么问题？如果客户端是手机上的程序，还要考虑什么？
4. 把重连的随机抖动去掉，启动 100 个客户端连接同一个服务器，然后重启服务器，统计服务器在恢复后的第一秒内收到了多少个连接请求。加上抖动后呢？
