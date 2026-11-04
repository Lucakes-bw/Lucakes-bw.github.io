---
title: 压测与性能分析
date: 2026-11-04 14:50:00
updated: 2026-11-04 14:50:00
categories:
  - 计算机网络
  - 服务器开发
tags:
  - 网络编程
  - 性能
  - wrk
  - perf
order: 6
description: 用 wrk 压测 tinynet 的 HTTP 服务器：单线程每秒 44 万个请求。时间花在哪里？perf 的火焰图显示 80% 在内核中，其中 write 一个系统调用就占了 62%，自己的代码只有 6%。所以优化的方向不是代码，而是系统调用的次数：用流水线让一次 write 发出 16 个响应，吞吐量变成 6 倍。最后和每个连接一个线程的服务器对比，并说明为什么在这台 2 核的虚拟机上，加 IO 线程反而变慢了。
disableNunjucks: true
---

> **本文目标**：会用 wrk 压测 HTTP 服务器，读懂它的结果；会用 perf 统计系统调用、生成火焰图，判断瓶颈在用户态还是内核；理解「减少系统调用」为什么是网络服务器最有效的优化；能正确解读多线程压测的结果。
>
> **前置知识**：[上一篇](/posts/srv-http/)；《C++工程实践》中的 [性能分析与优化](/posts/cppeng-profiling/)（perf 和火焰图的读法）。
>
> **测试环境**：Docker 中的 Ubuntu 24.04，运行在 Apple M6 上的虚拟机，**只有 2 个 CPU**；wrk 和服务器在同一台机器上，通过本地回环通信。这个环境的数字只能用来相互比较，不代表真实服务器的性能。

## 1. wrk

[wrk](https://github.com/wg/wrk) 是一个 HTTP 压测工具：用少数几个线程（它自己也是事件驱动的）维持大量连接，不停地发请求，统计吞吐量和延迟。

```bash
wrk -t1 -c100 -d10s http://127.0.0.1:8000/
```

- `-t1`：wrk 用 1 个线程（这台虚拟机只有 2 个核，留一个给服务器）；
- `-c100`：保持 100 个连接；
- `-d10s`：持续 10 秒。

默认使用持久连接，每个连接收到响应后立即发下一个请求。用 Release 构建的 `http_server`（0 个 IO 线程，所有连接都在主线程中处理）：

```
    Latency   123.63us   38.43us   1.32ms   90.59%
Requests/sec: 426585.89
```

每秒 42.7 万个请求，平均延迟 124 微秒（标准差 38 微秒，最大 1.32 毫秒）。几次运行之间在 42 万到 44 万之间波动。

## 2. 时间花在哪里

### 2.1 用户态还是内核

压测进行时，用 perf 对服务器进程采样 6 秒（编译时加了 `-fno-omit-frame-pointer`，构建类型是 `RelWithDebInfo`），先按模块统计：

```
$ perf report --no-children --sort dso
    80.45%  [kernel.kallsyms]
     9.09%  libc.so.6
     5.98%  http_server
     3.94%  libstdc++.so.6.0.33
     0.54%  [vdso]
```

**80% 的时间在内核中**。libc 的 9% 主要是 `write`、`readv` 这些系统调用的包装函数；tinynet 和 HTTP 的代码（`http_server`）加上 C++ 标准库，一共只有 10% 左右。

这意味着：即使把用户态的代码优化到零，吞吐量最多也只能提高 10% 多一点。

### 2.2 火焰图

<figure>
<a href="/img/net-server/flame-http.svg"><img src="/img/net-server/flame-http.svg" alt="HTTP 服务器在压测时的火焰图：EventLoop::loop 之上几乎全是 TcpConnection::handleRead，其中右边一大块是 send 调用的 write，占 62%，它的上面一直延伸到内核的 tcp_sendmsg、ip_output，再到接收方向的 net_rx_action、tcp_v4_rcv；左边一块是 readv，占 18%" width="1000"></a>
<figcaption>HTTP 服务器在压测时的火焰图（点击图片打开交互版本）</figcaption>
</figure>

从下往上看：

- `EventLoop::loop` → `Channel::handleEvent` → `TcpConnection::handleRead`，占了 94%：几乎所有的时间都在处理「收到数据」这个事件；
- `handleRead` 之上分成两块：左边的 `Buffer::readFd`（其中 `readv` 系统调用占 18%），右边的 `HttpServer::onMessage`（76%）；
- `onMessage` 中，`TcpConnection::send` 占了 63%，几乎全部是 `write` 系统调用（62%）；HTTP 的解析和生成响应只是旁边很窄的几条；
- `write` 之上是内核的 TCP 发送路径：`tcp_sendmsg` → `tcp_write_xmit` → `ip_output` → ……再往上，竟然是 `net_rx_action`、`tcp_v4_rcv` 这些**接收**方向的函数。

最后一点值得解释：在本地回环上，发送的数据包不经过网卡，内核在发送的系统调用中，**直接**就把它交给了接收方的协议栈处理（软中断，`do_softirq`）。所以服务器的 `write` 中，包含了 wrk 那一端接收这个响应的大部分工作。这是回环测试的一个特点：发送方要为接收方付出代价，测出的数字和真实的网络环境不一样。

### 2.3 系统调用的次数

再用 perf 统计 3 秒内各种系统调用的次数：

```
$ perf stat -e 'syscalls:sys_enter_epoll_pwait,syscalls:sys_enter_readv,syscalls:sys_enter_write' -p <pid> -- sleep 3
            347448      syscalls:sys_enter_epoll_pwait
           1314393      syscalls:sys_enter_readv
           1314393      syscalls:sys_enter_write
```

3 秒处理了大约 132 万个请求，所以：

- 每个请求一次 `readv`、一次 `write`，正好；
- `epoll_wait` 只有请求数的四分之一：每次 `epoll_wait` 平均返回将近 4 个就绪的连接。负载越高，每次返回的就绪连接越多，事件循环的开销就被分摊得越少，这是事件驱动的服务器在高负载下效率反而更高的原因之一。

## 3. 减少系统调用：流水线

既然时间几乎都花在系统调用上，而每个请求恰好需要一次读、一次写，那么要提高吞吐量，就要让**一次系统调用处理多个请求**。

HTTP/1.1 的**流水线**正好可以做到：客户端一次发送 16 个请求，服务器一次 `readv` 读到全部 16 个，上一篇的 `onMessage` 把它们逐个解析、把 16 个响应攒在一起，一次 `write` 发出去。wrk 用一个 Lua 脚本发送流水线的请求：

```lua
-- wrk 脚本：每次发送 16 个连在一起的请求（HTTP 流水线）
init = function(args)
   local r = {}
   for i = 1, 16 do r[i] = wrk.format(nil, "/") end
   req = table.concat(r)
end

request = function()
   return req
end
```

```
$ wrk -t1 -c100 -d8s -s bench/pipeline.lua http://127.0.0.1:8000/
    Latency   325.62us  184.57us   5.01ms   65.19%
Requests/sec: 2727334.92
```

**每秒 273 万个请求，是原来的 6.2 倍**。服务器的代码一行没改，只是每个系统调用做的事情多了。

现实中，浏览器几乎都不使用 HTTP/1.1 的流水线（队头阻塞等问题，见《HTTP/2 与 HTTP/3》），所以这个数字不能直接拿来说「服务器能处理 273 万请求每秒」。但它说明了网络服务器优化的一个基本方向：**批量**。同样的思路还有：

- 一次 `writev` 发送多块数据，而不是多次 `write`；
- Linux 的 `sendmmsg` / `recvmmsg` 一次收发多个 UDP 包；
- io_uring 一次提交多个 IO 请求；
- 响应攒在一起再发（tinynet 的 `onMessage` 已经这样做了）。

## 4. 和每个连接一个线程对比

`bench/thread_per_conn.cpp` 是一个对照组：阻塞 IO，每个连接一个线程，返回完全相同的响应。分别用 100 和 1000 个连接压测：

| 服务器 | 连接数 | 请求每秒 | 平均延迟 | 延迟标准差 | 线程数 | 内存 |
|---|---|---|---|---|---|---|
| tinynet | 100 | 440,733 | 118 µs | 31 µs | 1 | 3.6 MB |
| 每个连接一个线程 | 100 | 333,917 | 161 µs | 115 µs | 101 | 4.5 MB |
| tinynet | 1000 | 468,105 | 1.07 ms | 0.17 ms | 1 | 6.4 MB |
| 每个连接一个线程 | 1000 | 312,549 | 1.57 ms | 0.45 ms | 1001 | 16.3 MB |

（线程数和内存是在压测进行到一半时，从 `/proc/<pid>/` 中读到的。）

- 吞吐量：tinynet 高 32% 到 50%；
- 延迟的波动：每个连接一个线程的标准差是 tinynet 的 3 到 4 倍。1000 个线程争抢 2 个 CPU 核，谁先运行由调度器决定，有的请求很快，有的要等很久；
- 资源：1000 个连接需要 1001 个线程。实际占用的内存还不算多（线程栈是按需分配的），但线程切换的开销随线程数增长，到了一万、十万个连接，线程模型就难以为继了（《同时服务多个客户端》的 C10K 问题）。

这个对比也不是对每个连接一个线程完全公平：它没有用任何连接池或者线程池。但它说明了事件驱动的核心优势：**一个线程同时服务大量连接，没有线程切换，延迟稳定**。

## 5. 为什么加 IO 线程反而变慢了

最后看多线程的效果：

| IO 线程 | 请求每秒 | 平均延迟 |
|---|---|---|
| 0（全部在主线程） | 426,586 | 124 µs |
| 1 | 416,827 | 126 µs |
| 2 | 392,333 | 133 µs |

加了 IO 线程，吞吐量不升反降。这不是 tinynet 的多线程有问题，而是**这台虚拟机只有 2 个 CPU 核**：

- wrk 本身要占满一个核（它要构造请求、解析响应，还有回环上发送方要付出的内核开销）；
- 服务器只剩一个核可用。0 个 IO 线程时，一个线程独占这个核；加了 IO 线程，几个线程在同一个核上来回切换，多出的只是开销。另外，1 个 IO 线程时，主线程只负责接受连接，几乎闲着，所有连接都在那 1 个 IO 线程中，和 0 个 IO 线程的情况本质上一样。

所以**多线程的压测结果，必须结合 CPU 的核数来看**。要真正测出多个 IO 线程的效果，需要更多的核，并且最好把 wrk 放在另一台机器上（同时避开回环测试中发送方替接收方干活的问题）。在 8 核以上的机器上，muduo 这类 one loop per thread 的库，吞吐量可以随 IO 线程数接近线性地增长，直到网卡或者内核的协议栈成为瓶颈。

`Connection: close` 的情况（每个请求一个新连接）也值得一看：

```
== IO 线程 0：wrk -t1 -c100 -d10s -H Connection: close
    Latency   353.63us  231.57us   4.40ms   68.18%
Requests/sec:  91481.84
```

只有持久连接的五分之一多一点：每个请求都要经历三次握手、四次挥手，`accept`、`close` 这些系统调用，以及内核为每个新连接分配、释放的资源。这就是 HTTP/1.1 默认使用持久连接的原因。

<figure class="diagram">
<svg viewBox="0 0 700 220" role="img" aria-label="压测结果：持久连接每秒 42.7 万个请求，每个请求一个连接 9.1 万，流水线 16 个请求 272.7 万，每个连接一个线程的服务器 33.4 万">
  <defs><marker id="rp-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rp-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="200.0" y="37.0" text-anchor="end" style="fill:var(--fg);font-size:12px">tinynet 持久连接</text><rect x="210" y="16" width="62.6" height="28" rx="4" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="278.6" y="35.0" text-anchor="start" style="fill:var(--fg);font-size:12px">42.7 万/秒</text><text x="200.0" y="81.0" text-anchor="end" style="fill:var(--fg);font-size:12px">tinynet 每个请求一个连接</text><rect x="210" y="60" width="13.4" height="28" rx="4" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="229.4" y="79.0" text-anchor="start" style="fill:var(--fg);font-size:12px">9.1 万/秒</text><text x="200.0" y="125.0" text-anchor="end" style="fill:var(--fg);font-size:12px">tinynet 流水线 ×16</text><rect x="210" y="104" width="400.0" height="28" rx="4" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="616.0" y="123.0" text-anchor="start" style="fill:var(--fg);font-size:12px">272.7 万/秒</text><text x="200.0" y="169.0" text-anchor="end" style="fill:var(--fg);font-size:12px">每个连接一个线程</text><rect x="210" y="148" width="49.0" height="28" rx="4" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="265.0" y="167.0" text-anchor="start" style="fill:var(--fg);font-size:12px">33.4 万/秒</text><text x="14.0" y="206.0" text-anchor="start" style="fill:var(--muted);font-size:11px">100 个连接，wrk 1 个线程，服务器 0 个 IO 线程；2 核的虚拟机，本地回环</text>
  </g>
</svg>
</figure>

## 6. 常见错误

1. **压测工具和服务器在同一台机器上，却不考虑 CPU 的分配**。两者争抢 CPU，结果反映的是调度，而不是服务器本身。
2. **只看平均值**。平均延迟一样，标准差和最大值可能差很多，用户感受到的往往是那些最慢的请求。
3. **在用户态的代码上花大力气优化**。先用 perf 看看时间是不是真的花在用户态：网络服务器的瓶颈经常在内核的系统调用中。
4. **把流水线、回环的测试结果当成真实的性能**。它们只适合用来比较不同的版本，或者找出瓶颈。
5. **压测时连接数超过了文件描述符的上限**。wrk 报告大量的 `Socket errors`。先 `ulimit -n` 调高上限。

## 7. 小结

- wrk 用少量线程维持大量连接，测量吞吐量和延迟分布；
- 先判断瓶颈在用户态还是内核：tinynet 的 HTTP 服务器 80% 的时间在内核中，`write` 一个系统调用就占 62%；
- 优化方向是减少系统调用：流水线让一次读写处理 16 个请求，吞吐量变成 6.2 倍；
- 事件驱动和每个连接一个线程相比：吞吐量更高，延迟更稳定，线程数不随连接数增长；
- 多线程的压测结果要结合 CPU 核数来解读。

到这里，这一章的网络库就完成了：事件循环、缓冲区、连接的生命周期、定时器、多线程，最后是一个 HTTP 服务器和它的性能分析。完整的代码在 [projects/net-server/tinynet](https://github.com/Lucakes-bw/Lucakes-bw.github.io/tree/main/projects/net-server/tinynet)。想继续深入，推荐阅读陈硕的《Linux 多线程服务端编程》（muduo 的设计思路），以及 Redis 的 `ae.c`（一个更小的事件循环）。

## 8. 练习

1. 用 `wrk --latency` 输出延迟的分布（50%、75%、90%、99%），比较 tinynet 和每个连接一个线程在 1000 个连接时的 99% 延迟。
2. `TcpConnection::sendInLoop` 中，输出缓冲区为空时会直接 `write`。如果改成总是先放进输出缓冲区、等可写事件再发送，系统调用的次数和吞吐量会怎样变化？
3. 如果有一台多核的机器，设计一个实验，测出 IO 线程数从 1 增加到 8 时吞吐量的变化。需要注意哪些事情？

<details>
<summary>点击查看答案</summary>

1. 在同样的环境中测了一次（1000 个连接，8 秒）：

   | 分位 | tinynet | 每个连接一个线程 |
   |---|---|---|
   | 50% | 1.11 ms | 1.77 ms |
   | 75% | 1.13 ms | 2.15 ms |
   | 90% | 1.15 ms | 2.45 ms |
   | 99% | 2.21 ms | 2.80 ms |

   tinynet 的 50% 到 90% 几乎是一条水平线（1.11 到 1.15 毫秒），延迟非常集中；每个连接一个线程从中位数开始就慢 60%，并且越往后分散得越开。两者的 99% 延迟差距反而小一些：tinynet 偶尔也会被慢的时候拖住（比如一轮 `epoll_wait` 返回了很多连接，排在最后的要等前面的处理完）。

2. 每个响应都要多一次 `epoll_wait` 返回可写事件，再加上两次 `epoll_ctl`（打开、关闭对可写事件的关注），系统调用的次数大约变成原来的两倍多，吞吐量会明显下降。直接写是一个重要的优化：大部分时候内核的发送缓冲区有足够的空间，一次就能写完。

3. 要点：wrk 放在另一台机器上（或者至少用 `taskset` 把 wrk 和服务器绑在不同的核上），避免争抢 CPU；连接数要明显多于 IO 线程数，让每个线程都有足够的连接；每个配置运行多次取中位数；同时观察每个核的 CPU 使用率（`mpstat -P ALL 1`），确认瓶颈在哪里。如果服务器的线程都已经跑满，吞吐量却不再增长，说明瓶颈在内核或者网卡，可以考虑 `SO_REUSEPORT`（每个线程一个监听套接字）、网卡多队列等。

</details>
