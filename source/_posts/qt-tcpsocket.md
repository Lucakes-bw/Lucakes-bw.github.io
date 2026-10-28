---
title: QTcpSocket 与消息分帧
date: 2026-10-29 09:00:00
updated: 2026-10-29 09:00:00
categories:
  - 客户端开发
  - 网络与数据
tags:
  - Qt
  - C++
  - TCP
  - Socket
order: 1
description: QTcpSocket 是异步的：connectToHost 立即返回，连上了发 connected 信号，数据到了发 readyRead 信号，一个线程就够了。可是 readyRead 和「一条消息」没有任何对应关系：服务器连续 send 三行，客户端一次 readyRead 收到了全部 36 字节；一行分两次发，就收到两次；4 MB 的数据，三次运行分别收到了 26、13、36 次 readyRead，每次的大小从 3 万字节到 150 万字节不等；从「气」字的三个字节中间切开，两段各自转成字符串，就出现了乱码「今天天�」「��不错」。本文给消息加上 4 字节的长度前缀，在客户端用一个缓冲区把字节流重新切成一条条完整的消息：三条粘在一起的能拆开，被拆成 2 字节、98 字节、834 字节三段的第四条也能拼回来。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用 `QTcpSocket` 连接服务器、发送和接收数据，理解它的异步工作方式和几个主要的信号；理解 `readyRead` 和「一条消息」没有对应关系，复现粘包、拆包和 UTF-8 字符被切断；会设计「长度前缀 + 正文」的消息格式，并在客户端正确地切分消息；知道要限制消息的最大长度。
>
> **前置知识**：计算机网络专栏《TCP Socket 入门》《TCP 的常见问题》（字节流没有边界）、《IO 多路复用》；本专栏第三章（事件循环、信号与槽）。
>
> **实验环境**：macOS 27.0，Qt 6.12.0，Release 构建。服务器和客户端都在本机（`127.0.0.1`）。用到 Qt Network 模块：`find_package(Qt6 REQUIRED COMPONENTS Network)`，链接 `Qt6::Network`。

## 1. 异步的 QTcpSocket

网络专栏中用 C 写的客户端是这样的：`connect()` 阻塞到连接建立，`recv()` 阻塞到数据到来。在界面程序中这样写，第三章已经看到了后果：阻塞期间界面完全卡死。服务器那边用 epoll 解决了这个问题：一个线程同时等待很多个连接，哪个有数据就处理哪个。

Qt 的事件循环本身就是这样一个「等待很多事情」的循环（第一章说过，它在 Linux 上的底层就是 `poll`），`QTcpSocket` 把套接字也交给事件循环去等：

| 操作 / 信号 | 作用 |
|---|---|
| `connectToHost(地址, 端口)` | 开始连接，**立即返回** |
| `connected()` | 连接建立了 |
| `write(数据)` | 把数据放进发送缓冲区，**立即返回**，事件循环在套接字可写时真正发送 |
| `readyRead()` | 有新数据可以读了，用 `read` / `readAll` 读出 |
| `bytesWritten(n)` | 又有 n 字节被发送出去了 |
| `disconnected()` | 连接关闭了 |
| `errorOccurred(错误)` | 出错了：连接被拒绝、主机找不到、超时…… |

所有的操作都不阻塞，结果都通过信号通知。所以在界面线程中直接使用 `QTcpSocket` 就可以，不需要另开线程。

一个最简单的客户端：

```cpp
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <cstdio>

// 错误的假设：每次 readyRead 正好是一条完整的消息
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QElapsedTimer clock;
    QTcpSocket socket;
    int reads = 0;
    qint64 total = 0;

    QObject::connect(&socket, &QTcpSocket::connected, [&] {
        std::printf("%5lld ms  已连接\n", clock.elapsed());
    });
    QObject::connect(&socket, &QTcpSocket::readyRead, [&] {
        QByteArray data = socket.readAll();
        ++reads;
        total += data.size();
        std::printf("%5lld ms  readyRead #%d，%lld 字节：「%s」\n", clock.elapsed(), reads,
                    (long long)data.size(), QString::fromUtf8(data).replace('\n', "\\n").toUtf8().data());
    });
    QObject::connect(&socket, &QTcpSocket::disconnected, [&] {
        std::printf("%5lld ms  连接关闭，共 %d 次 readyRead，%lld 字节\n", clock.elapsed(), reads, total);
        app.quit();
    });
    clock.start();
    socket.connectToHost("127.0.0.1", quint16(std::atoi(argv[1])));
    std::printf("%5lld ms  connectToHost 立即返回，状态 %d\n", clock.elapsed(), int(socket.state()));
    return app.exec();
}
```

这个客户端做了一个很常见的错误假设：**每次 `readyRead` 收到的就是一条消息**。第 2 节逐一验证这个假设。

## 2. readyRead 不是一条消息

### 2.1 测试用的服务器

为了精确地控制发送的方式，测试服务器用 Python 写，几行就能控制每次 `send` 发什么、中间停多久：

```python
# 测试用的 TCP 服务器：按不同的模式发送数据
import socket, struct, sys, time, json

mode, port = sys.argv[1], int(sys.argv[2])
srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", port))
srv.listen()
conn, _ = srv.accept()
conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)   # 关掉 Nagle 算法，每次 send 立即发出
if mode == "burst":                  # 连续调用 3 次 send，每次一行
    for line in ["你好\n", "在吗？\n", "晚上一起吃饭\n"]:
        conn.sendall(line.encode())
elif mode == "slow":                 # 一行分成两半，中间停 200 ms
    conn.sendall("今天天".encode())
    time.sleep(0.2)
    conn.sendall("气不错\n".encode())
elif mode == "utf8":                 # 在「气」字的 3 个字节中间切开
    data = "今天天气不错\n".encode()
    conn.sendall(data[:10])
    time.sleep(0.2)
    conn.sendall(data[10:])
elif mode == "big":                  # 一次 send 4 MB
    conn.sendall(b"x" * (4 << 20))
time.sleep(0.5)
conn.close()
```

先启动服务器（例如 `python3 server.py burst 29001`），再运行客户端 `./naive 29001`。

### 2.2 三次 send，一次 readyRead

```
    0 ms  connectToHost 立即返回，状态 2
    1 ms  已连接
    1 ms  readyRead #1，36 字节：「你好\n在吗？\n晚上一起吃饭\n」
  506 ms  连接关闭，共 1 次 readyRead，36 字节
```

`connectToHost` 返回时状态是 2（`ConnectingState`，正在连接），1 毫秒后连接建立。服务器调用了三次 `send`，客户端只收到一次 `readyRead`，三行一起到了。客户端如果把每次 `readyRead` 当作一条消息，三条就变成了一条。这就是常说的「**粘包**」。

### 2.3 一次消息，两次 readyRead

```
    0 ms  connectToHost 立即返回，状态 2
    0 ms  已连接
    0 ms  readyRead #1，9 字节：「今天天」
  205 ms  readyRead #2，10 字节：「气不错\n」
  710 ms  连接关闭，共 2 次 readyRead，19 字节
```

一行「今天天气不错」被分成两次收到，客户端会把它当成两条消息：「今天天」和「气不错」。这叫「**拆包**」或「半包」。

### 2.4 汉字被切断

UTF-8 中一个汉字占 3 个字节。服务器从「气」字的 3 个字节中间切开发送：

```
    1 ms  readyRead #1，10 字节：「今天天�」
  206 ms  readyRead #2，9 字节：「��不错\n」
```

每段单独用 `QString::fromUtf8` 转换，「气」字的前 1 个字节和后 2 个字节都不是完整的字符，变成了替换字符 `�`。两段的文字都乱了。**字节流必须先切成完整的消息，再转成字符串**，不能对收到的每一段直接转换。

### 2.5 4 MB 的数据

服务器一次 `send` 4 MB，运行三次：

```
  506 ms  连接关闭，共 26 次 readyRead，4194304 字节
  508 ms  连接关闭，共 13 次 readyRead，4194304 字节
          每次的大小：最小 48996，最大 1545792
  506 ms  连接关闭，共 36 次 readyRead，4194304 字节
          每次的大小：最小 32664，最大 212316
```

数据一个字节都没少，但三次运行分别分成了 26、13、36 次到达，每次的大小从 3 万多字节到 150 多万字节不等。分成几次、每次多大，取决于两边的缓冲区大小、发送的速度、事件循环什么时候来读，**完全不可预测**。

这些现象的原因，网络专栏《TCP 的常见问题》第 1 节已经讲过：TCP 是**字节流**，只保证字节按顺序、不丢失地到达，不保留「每次 `send` 了多少」的边界。粘包、拆包不是 TCP 的毛病，而是应用程序要自己划分消息的边界。Qt 的 `readyRead` 只是告诉你「又来了一些字节」，和网络专栏中 `recv` 返回的字节数一样，不代表任何消息边界。

## 3. 消息分帧

把字节流重新切成一条条消息，叫作**分帧**（framing）。常用的方法有两种：

| 方法 | 做法 | 优缺点 |
|---|---|---|
| 分隔符 | 每条消息以 `\n` 等特殊字符结尾 | 简单，适合文本协议（HTTP 的头部、Redis）；正文中不能出现分隔符，否则要转义 |
| 长度前缀 | 先发正文的长度，再发正文 | 正文可以是任意字节（图片、文件）；读取时知道还差多少 |

聊天消息中可能有换行，也可能要传图片，所以用长度前缀：每条消息是 **4 字节的长度（大端）+ 这么多字节的 JSON 正文**。

```
┌────────────┬──────────────────────────────────────┐
│ 00 00 00 21 │ {"from":"张三","text":"你好"}           │
│  长度 = 33   │ 33 字节的 UTF-8 JSON                    │
└────────────┴──────────────────────────────────────┘
```

长度用**大端**（网络字节序）表示，和网络专栏中 `htonl` / `ntohl` 的约定一致，发送方和接收方的机器字节序不同也不会出错。正文用 JSON，扩展字段方便，下一章的聊天协议也会用它。

### 3.1 接收：缓冲区加循环

接收方的思路是：收到的字节都先追加到一个缓冲区；然后**循环**检查缓冲区中是否有一条完整的消息，有就取出来，没有就等下一次 `readyRead`。

```cpp
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QtEndian>

// 消息格式：4 字节大端的长度 + 这么多字节的 JSON 正文
class MessageReader : public QObject
{
    Q_OBJECT
public:
    explicit MessageReader(QTcpSocket *socket) : m_socket(socket)
    {
        connect(socket, &QTcpSocket::readyRead, this, &MessageReader::onReadyRead);
    }

signals:
    void message(const QJsonObject &msg);

private:
    void onReadyRead()
    {
        m_buffer += m_socket->readAll();
        std::printf("  readyRead：缓冲区现在 %lld 字节\n", (long long)m_buffer.size());
        for (;;) {
            if (m_buffer.size() < 4)
                return;                                   // 长度还没收全
            quint32 len = qFromBigEndian<quint32>(m_buffer.constData());
            if (len > 1 << 20) {                          // 超过 1 MB：对方出错或者恶意，断开
                std::printf("  消息长度 %u 不合理，断开连接\n", len);
                m_socket->abort();
                return;
            }
            if (m_buffer.size() < 4 + qsizetype(len))
                return;                                   // 正文还没收全，等下一次 readyRead
            QByteArray body = m_buffer.mid(4, len);
            m_buffer.remove(0, 4 + len);                  // 取走一条完整的消息
            emit message(QJsonDocument::fromJson(body).object());
        }
    }

    QTcpSocket *m_socket;
    QByteArray m_buffer;
};
```

要点：

- **两个「还没收全」**：缓冲区不到 4 字节时，连长度都不知道；知道了长度，正文可能还没到齐。两种情况都直接返回，数据留在缓冲区中，等下一次 `readyRead` 追加。
- **`for (;;)` 循环**：一次 `readyRead` 可能带来好几条消息（2.2 节的粘包），处理完一条要接着检查下一条，直到不够一条为止。只处理一条就返回是常见的错误：剩下的消息要等下一次 `readyRead` 才处理，而如果对方不再发送，就永远不会有下一次了。
- **`qFromBigEndian`** 把 4 个大端字节转成本机的整数，`<QtEndian>` 中的函数，相当于 `ntohl`。
- **限制最大长度**：长度是对方发来的，不能盲目相信。如果对方（出了 bug 的服务器，或者恶意的攻击者）发来一个 `0xFFFFFFFF`，客户端就会一直等 4 GB 的数据，缓冲区越来越大。超过合理的上限就断开连接。
- 把分帧的逻辑封装成一个类，对外只发出 `message` 信号，使用者就再也不用关心字节流了。

### 3.2 实验

服务器先把三条消息一次发出（粘包），再把第四条（300 个字，约 900 字节）拆成 3 段，每段间隔 100 毫秒发出（拆包），第一段只有 2 个字节，连长度都不完整：

```python
def frame(obj):
    body = json.dumps(obj, ensure_ascii=False).encode()
    return struct.pack(">I", len(body)) + body          # 4 字节大端长度 + 正文

data = b"".join(frame({"from": "张三", "text": t}) for t in ["你好", "在吗？", "晚上一起吃饭"])
conn.sendall(data)
last = frame({"from": "李四", "text": "好的，七点见" * 50})
for part in (last[:2], last[2:100], last[100:]):
    time.sleep(0.1)
    conn.sendall(part)
```

```cpp
QTcpSocket socket;
MessageReader reader(&socket);
int n = 0;
QObject::connect(&reader, &MessageReader::message, [&](const QJsonObject &m) {
    QString text = m["text"].toString();
    std::printf("消息 #%d 来自 %s：%s ……\n", ++n, qPrintable(m["from"].toString()), qPrintable(text.left(12)));
});
socket.connectToHost("127.0.0.1", port);
```

```
  readyRead：缓冲区现在 135 字节
消息 #1 来自 张三：你好
消息 #2 来自 张三：在吗？
消息 #3 来自 张三：晚上一起吃饭
  readyRead：缓冲区现在 2 字节
  readyRead：缓冲区现在 100 字节
  readyRead：缓冲区现在 934 字节
消息 #4 来自 李四：好的，七点见好的，七点见…（共 300 字）
```

- 第一次 `readyRead` 带来了 135 字节，循环从中切出了三条完整的消息。
- 第四条消息分三次到达：只有 2 字节时，连长度都读不出来，等待；到 100 字节时，长度知道了，正文还差很多，继续等待；到 934 字节时正文到齐，切出一条消息。300 个字一个都不少，也没有乱码。

### 3.3 发送

发送就简单多了，拼好长度和正文，一次 `write`：

```cpp
void sendMessage(QTcpSocket *socket, const QJsonObject &msg)
{
    QByteArray body = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    QByteArray header(4, Qt::Uninitialized);
    qToBigEndian<quint32>(quint32(body.size()), header.data());
    socket->write(header + body);
}
```

`write` 只是把数据放进 `QTcpSocket` 的发送缓冲区，立即返回，由事件循环在套接字可写时发出去。所以即使一次写入很大的数据，也不会阻塞界面。`bytesToWrite()` 返回还没有发出的字节数，如果它一直增长，说明对方接收得太慢（或者网络断了），第三章 3.3 篇说的「队列堆积」在这里同样会发生，需要限制。

> Qt 还提供了 `QDataStream` 的「事务」（`startTransaction` / `commitTransaction`）来做同样的事：读取失败时自动回滚，等数据收全再读。它要求两端都用 `QDataStream` 的格式，适合两端都是 Qt 程序的场合。自己定义长度前缀的格式更通用，服务器可以用任何语言实现（下一章接的是网络专栏中用 C 写的服务器）。

## 4. 小结

- `QTcpSocket` 是异步的：`connectToHost`、`write` 立即返回，连接建立、数据到达、连接关闭、出错都通过信号通知，在界面线程中直接使用即可。
- `readyRead` 只表示「又来了一些字节」。实验中三条消息在一次 `readyRead` 中到达，一条消息分两次到达，4 MB 的数据每次运行的分段都不同。
- UTF-8 的字符可能被切断，必须先切出完整的消息，再转成字符串。
- 用「4 字节大端长度 + 正文」分帧：收到的字节追加到缓冲区，循环取出所有完整的消息，不完整的留到下一次。长度来自对方，要限制上限。

现在客户端能可靠地收发消息了，可是前提是网络一直是通的。下一篇看连接断开的各种情况：服务器崩溃、网线被拔、服务器卡死，客户端各要多久才能发现，以及怎样自动重连。

## 练习

1. 改写 3.1 节的 `MessageReader`，用分隔符 `\n` 分帧（提示：`m_buffer.indexOf('\n')`）。如果聊天消息中本身就有换行，会出什么问题？怎样解决？
2. 3.1 节中每取出一条消息就调用一次 `m_buffer.remove(0, ...)`，会把后面的数据整体往前移动。如果一次收到 1 万条很短的消息，这样做的总代价是多少？怎样改进？（提示：记录一个读取位置，最后再一次性删除。）
3. 让服务器发送一个长度为 `0xFFFFFFFF` 的消息头，验证客户端会断开连接。如果去掉长度检查，客户端的内存会怎样变化？
4. 用 `QDataStream` 的 `startTransaction` / `commitTransaction` 重写接收的逻辑，服务器用 Qt 的 `QTcpServer` 实现。
