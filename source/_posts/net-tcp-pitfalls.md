---
title: TCP 的常见问题
date: 2026-10-24 15:40:00
updated: 2026-10-24 15:40:00
categories:
  - 计算机网络
  - TCP/IP协议
tags:
  - 计算机网络
  - TCP
  - Nagle
order: 5
description: 网络编程中有几个和 TCP 有关的问题，几乎每个人都会遇到。本文逐一实测：分三次发送的消息，对方两次就收完了，200000 字节的一次发送却要读 137 次，这就是「粘包」和「拆包」；每个请求分两次发送，延迟从 1.34 毫秒变成了 44 毫秒，抓包看清是 Nagle 算法和延迟确认在互相等待；listen 的 backlog 满了以后，新的连接等了 3 秒还连不上。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 TCP 是字节流，没有消息边界，知道怎样在应用层划分消息；理解 Nagle 算法和延迟确认，以及它们一起造成的 40 毫秒延迟，会用 `TCP_NODELAY` 解决；理解 listen 的 backlog 和全连接队列。
>
> **前置知识**：本章前几篇；《文件描述符与系统调用》中「`read` 不一定读满」。
>
> **实验环境**：Ubuntu 24.04 容器，Linux 内核 6.8，c —— r —— s 的网络，r 上只加了 1 ms 的延迟。为了简短，服务器和客户端用 Python 写，套接字的用法和 C 语言一一对应。

## 1. 字节流没有边界

### 1.1 「粘包」和「拆包」

服务器每次 `recv` 最多读 65536 字节，记录每次读到了多少：

```python
# stream_srv.py：每次 recv 最多读 65536 字节，统计每次读到了多少
import socket
from collections import Counter
srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("10.0.2.2", 8000)); srv.listen()
conn, _ = srv.accept()
sizes = []
while True:
    data = conn.recv(65536)
    if not data:
        break
    sizes.append(len(data))
    if len(sizes) <= 3:
        print("第 %d 次 recv：%d 字节 %r" % (len(sizes), len(data), data[:30]))
rest = sizes[3:]
print("之后又 recv 了 %d 次，共 %d 字节，每次的大小：%s" % (len(rest), sum(rest), dict(Counter(rest).most_common())))
```

客户端先用 3 次 `send` 发送 3 条短消息，停 0.5 秒，再用一次 `sendall` 发送 200000 字节：

```python
# stream_cli.py：先连续发送 3 条短消息，再一次发送 200000 字节
import socket, time
s = socket.create_connection(("10.0.2.2", 8000))
s.send(b"hello\n")
s.send(b"how are you\n")
s.send(b"bye\n")
time.sleep(0.5)
s.sendall(b"x" * 200000)
s.close()
```

```
第 1 次 recv：6 字节 b'hello\n'
第 2 次 recv：16 字节 b'how are you\nbye\n'
第 3 次 recv：2896 字节 b'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx'
之后又 recv 了 136 次，共 197104 字节，每次的大小：{1448: 135, 1624: 1}
```

（运行两次，结果完全相同。）

- 发送了 3 条消息，接收方只读了 2 次：第 2 条和第 3 条**粘**在了一起；
- 一次发送的 200000 字节，接收方读了 **137** 次才读完，大部分是 1448 字节（一个报文段的数据量），这是被**拆**开了。

这就是常说的「粘包」和「拆包」。其实 TCP 中并没有「包」：上一章说过，TCP 是**字节流**，它只保证字节的顺序，不保留应用每次 `send` 的边界。发送方多次写入的数据可能合在一个报文段中发出，一次写入的数据也可能分成多个报文段；接收方每次 `recv` 读到的，只是此刻接收缓冲区中已有的数据，最多不超过指定的大小。`send` 的次数、报文段的个数、`recv` 的次数三者之间没有任何对应关系。

UDP 则不同（第 2.1 篇）：发送一个数据报，就收到一个完整的数据报，边界是保留的。

### 1.2 应用层划分消息

所以使用 TCP 的应用，必须自己规定消息的边界，常用的方法有三种：

| 方法 | 做法 | 例子 |
|---|---|---|
| 分隔符 | 每条消息以特定的字符结尾，接收方读到它才算一条 | 以 `\n` 结尾的文本协议；HTTP 的头部以 `\r\n\r\n` 结束 |
| 长度前缀 | 每条消息前面加上固定长度的头部，写明正文有多少字节 | 很多二进制协议；HTTP 的 `Content-Length` |
| 固定长度 | 每条消息的长度都一样 | 第 2 节实验中的请求，每个都是 110 字节 |

无论哪种方法，接收方都要**先把数据积累在自己的缓冲区中**，凑够一条完整的消息再处理，剩下的留给下一条。第 2 节的服务器就是这样做的：`buf` 中够 110 字节了，才处理一个请求。练习 1 用长度前缀实现一次。

## 2. Nagle 算法和延迟确认

### 2.1 40 毫秒

一个常见的「请求—响应」程序：客户端分两次发送一个请求的头部（10 字节）和正文（100 字节），然后等待服务器的回复。服务器收齐 110 字节后回复 `ok`：

```python
# rpc_srv.py：读到一个完整的请求（10 字节的头 + 100 字节的正文）后，回复 "ok"
import socket
srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("10.0.2.2", 8000)); srv.listen()
conn, _ = srv.accept()
buf = b""
while True:
    data = conn.recv(4096)
    if not data:
        break
    buf += data
    while len(buf) >= 110:
        buf = buf[110:]
        conn.send(b"ok")
```

```python
# rpc_cli.py [nodelay]：每个请求分两次 send（头和正文），然后等回复，重复 20 次
import socket, sys, time
s = socket.create_connection(("10.0.2.2", 8000))
if len(sys.argv) > 1:
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)   # 关闭 Nagle 算法
times = []
for i in range(20):
    t0 = time.time()
    s.send(b"H" * 10)          # 请求头
    s.send(b"B" * 100)         # 请求正文
    s.recv(2)                  # 等待回复
    times.append((time.time() - t0) * 1000)
print("%s：平均 %.2f 毫秒，最短 %.2f，最长 %.2f" % ("TCP_NODELAY" if len(sys.argv) > 1 else "默认（Nagle）", sum(times) / len(times), min(times), max(times)))
print("  前 6 次：", " ".join("%.1f" % t for t in times[:6]))
```

网络的往返时间只有约 1 毫秒，运行结果：

```
默认（Nagle）：平均 43.89 毫秒，最短 2.48，最长 48.64
  前 6 次： 2.5 47.1 47.7 48.5 44.4 44.4
TCP_NODELAY：平均 1.34 毫秒，最短 1.21，最长 1.44
  前 6 次： 1.4 1.3 1.2 1.3 1.4 1.3
```

默认设置下，除了第一次，每个请求都要 **44 毫秒左右**，是往返时间的 40 多倍；设置了 `TCP_NODELAY` 后，只要 **1.34 毫秒**。

### 2.2 抓包看原因

在 s 上抓包（第 2 个请求，只保留了时间的秒以下部分）：

```
40.385743 10.0.1.2.40768 > 10.0.2.2.8000: Flags [.], ack 3, ...                  ← c 确认上一个回复
40.385745 10.0.1.2.40768 > 10.0.2.2.8000: Flags [P.], seq 110:120, ack 3, ...      ← 请求头，10 字节
40.427451 10.0.2.2.8000 > 10.0.1.2.40768: Flags [.], ack 120, ...                  ← 41.7 毫秒后，s 才确认
40.428859 10.0.1.2.40768 > 10.0.2.2.8000: Flags [P.], seq 120:220, ack 3, ...      ← c 这才发出正文，100 字节
40.428896 10.0.2.2.8000 > 10.0.1.2.40768: Flags [.], ack 220, ...
40.429026 10.0.2.2.8000 > 10.0.1.2.40768: Flags [P.], seq 3:5, ack 220, ...        ← 回复 ok
```

请求头发出后，正文并没有紧跟着发出，而是等了 41.7 毫秒，直到 s 的确认到达。这是两个各自合理的机制碰在了一起：

- **Nagle 算法**（发送方）：如果还有已发出但未被确认的数据，新的**小**数据先不发，攒到确认回来（或者攒够一个满的报文段）再发。它的目的是避免网络中充满大量只有几个字节数据的小包（每个包都有 40 字节的头部）。c 发出请求头后，正文就因为请求头还没被确认而被扣住了；
- **延迟确认**（接收方）：收到数据后不立即确认，等待一小段时间（Linux 中最少约 40 毫秒），看看有没有数据要发给对方，好把确认「捎带」在数据中，减少纯确认包的数量。s 收到请求头时，请求还不完整，它没有回复要发，于是推迟了确认。

**发送方在等确认，接收方在等数据**，直到接收方的延迟确认计时器到期，才打破僵局，每个请求因此多出了约 40 毫秒。第一个请求很快，是因为连接刚建立时，Linux 的接收方处于「快速确认」模式，还没有开始延迟确认。

### 2.3 解决方法

- **合并写入**：把头部和正文拼在一起，用一次 `send` 发出（或者用 `writev` 一次写入多个缓冲区）。这是最好的办法，还能减少系统调用；
- **`TCP_NODELAY`**：关闭 Nagle 算法，小数据立即发出。对延迟敏感的程序（如交互式的远程终端、游戏、数据库客户端、RPC 框架）通常都会设置它；
- 不要试图在接收方关闭延迟确认来解决，那样会让确认包的数量大增，治标不治本。

## 3. listen 的 backlog

服务器调用 `listen` 后，三次握手由内核完成，不需要程序参与。握手完成的连接进入一个队列（**全连接队列**），等待程序用 `accept` 取走。`listen` 的参数 **backlog** 就是这个队列的长度。如果程序 `accept` 得太慢，队列满了会怎样？

```python
# backlog_srv.py：listen 的 backlog 为 2，并且从不调用 accept
import socket, time
srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("10.0.2.2", 8000)); srv.listen(2)
time.sleep(30)
```

```python
# backlog_cli.py：依次发起 8 个连接，每个最多等 3 秒
import socket, time
for i in range(1, 9):
    s = socket.socket(); s.settimeout(3)
    t0 = time.time()
    try:
        s.connect(("10.0.2.2", 8000))
        print("连接 %d：connect 成功，用时 %.2f 秒" % (i, time.time() - t0))
    except Exception as e:
        print("连接 %d：%s，用时 %.2f 秒" % (i, e, time.time() - t0))
    globals()["keep%d" % i] = s           # 不关闭，保持连接
time.sleep(2)
```

```
连接 1：connect 成功，用时 0.00 秒
连接 2：connect 成功，用时 0.00 秒
连接 3：connect 成功，用时 0.00 秒
连接 4：timed out，用时 3.01 秒
连接 5：timed out，用时 3.01 秒
连接 6：timed out，用时 3.00 秒
连接 7：timed out，用时 3.01 秒
连接 8：timed out，用时 3.01 秒
```

在 s 上查看监听的套接字：

```
State  Recv-Q Send-Q Local Address:Port Peer Address:PortProcess
LISTEN 3      2           10.0.2.2:8000      0.0.0.0:*
```

对于监听的套接字，`ss` 的 `Recv-Q` 是全连接队列中**等待 accept 的连接数**，`Send-Q` 是 **backlog**。backlog 为 2，队列中有 3 个（Linux 允许比 backlog 多 1 个）。

- 前 3 个连接，握手由内核完成，`connect` 立即成功。注意，**connect 成功并不代表服务器程序已经 accept 了这个连接**，此时程序甚至还不知道它的存在；
- 队列满了以后，内核**丢弃**新来的 SYN（而不是回复 RST），客户端只能按第 2.2 篇的规律不断重发 SYN，3 秒后被客户端自己设置的超时放弃。如果服务器只是暂时忙，之后 accept 了一些连接，重发的 SYN 就能成功，客户端只是慢了一些；
- `nstat` 中 `TcpExtListenOverflows` 和 `TcpExtListenDrops` 都是 24：队列满时被丢弃的 SYN（包括重发的）的次数。服务器上这两个计数在增长，说明程序 accept 得太慢，或者 backlog 设得太小。

backlog 的实际上限还受 `net.core.somaxconn` 限制（Linux 5.4 以后默认 4096）。高并发的服务器除了增大 backlog，更重要的是尽快 accept，这是第 4 章多进程、多线程、epoll 要解决的问题。

## 4. 常见错误

1. **假设一次 `recv` 就能收到一条完整的消息**：在本机测试时消息小、网络快，常常碰巧成立，到了真实的网络中就会出错。
2. **假设一次 `recv` 只收到一条消息**：可能同时收到好几条，后面的被忽略了。
3. **小块多次写入，导致 40 毫秒的延迟**：合并写入，或者设置 `TCP_NODELAY`。
4. **以为 `connect` 成功就代表服务器已经接受了连接**：它只代表内核完成了握手。
5. **服务器偶尔连不上，查不出原因**：看看 `ss -ltn` 的 Recv-Q 是否接近 Send-Q，以及 `nstat` 的 `ListenOverflows`。

## 5. 小结

- TCP 是字节流，不保留发送的边界：实测 3 条消息 2 次就读完，200000 字节读了 137 次；应用要用分隔符、长度前缀或固定长度来划分消息
- Nagle 算法扣住小数据等确认，延迟确认推迟确认等数据，两者相遇时每个请求多出约 40 毫秒：实测 44 毫秒，设置 `TCP_NODELAY` 后 1.34 毫秒
- 握手由内核完成，完成的连接进入全连接队列等待 accept；队列满时新的 SYN 被丢弃，客户端要等重发；`ss -ltn` 的 Recv-Q/Send-Q 是队列中的连接数和 backlog

## 6. 练习

1. 用「4 字节长度前缀 + 正文」的格式实现发送和接收消息的函数，用第 1 节的方式测试：连续发送 3 条消息，再发一条 200000 字节的消息，接收方应该正好收到 4 条完整的消息。
2. 第 2 节的客户端如果把头部和正文合在一起用一次 `send` 发送，不设置 `TCP_NODELAY`，延迟会是多少？
3. 为什么 Nagle 算法要等待「未被确认的数据」，而不是简单地等待一个固定的时间？

<details>
<summary>点击查看答案</summary>

1. ```python
   # framing.py：4 字节长度前缀（网络字节序）+ 正文
   import socket, struct

   def send_msg(sock, data):
       sock.sendall(struct.pack("!I", len(data)) + data)    # 头部和正文一次写入

   def recv_exact(sock, n):
       buf = b""
       while len(buf) < n:                                   # 一次 recv 可能不够，循环直到凑够 n 字节
           chunk = sock.recv(n - len(buf))
           if not chunk:
               raise ConnectionError("连接在消息中途关闭")
           buf += chunk
       return buf

   def recv_msg(sock):
       (length,) = struct.unpack("!I", recv_exact(sock, 4))
       return recv_exact(sock, length)
   ```

   `struct.pack("!I", n)` 把长度打包成 4 字节的大端整数（`!` 表示网络字节序，第 4.1 篇讲字节序）。关键是 `recv_exact`：一次 `recv` 读到的可能比需要的少，要循环读到够数为止，这和《文件描述符与系统调用》中 `read` 的处理方法一样。测试：发送 hello、how are you、bye 和 200000 个 x：

   ```
   第 1 条消息：5 字节 b'hello'
   第 2 条消息：11 字节 b'how are you'
   第 3 条消息：3 字节 b'bye'
   第 4 条消息：200000 字节 b'xxxxxxxxxxxxxxxxxxxx'
   ```

   不论 TCP 把数据怎样合并、拆分，接收方得到的都是 4 条完整的消息。

2. 只把客户端的两次 `send` 改成一次 `s.send(b"H" * 10 + b"B" * 100)`：

   ```
   一次发送：平均 1.34 毫秒，最短 1.17，最长 1.44
     前 6 次： 1.4 1.3 1.3 1.2 1.4 1.4
   ```

   和设置 `TCP_NODELAY` 一样快。整个请求一次发出，没有「已发出未确认的小数据」，Nagle 算法不会扣住任何东西；服务器收到的是完整的请求，立即回复，确认也就捎带在回复中了。
3. 等待确认，相当于让发送小数据的速度自动适应网络：往返时间短的局域网中，确认很快回来，几乎不影响延迟；往返时间长的网络中，每个往返时间最多发出一个小包，自动减少了小包的数量。固定的时间则无法同时适应两种情况。

</details>
