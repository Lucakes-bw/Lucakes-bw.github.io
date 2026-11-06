---
title: 对端死了，多久才能发现
date: 2026-11-11 10:10:00
updated: 2026-11-11 10:10:00
categories:
  - 计算机网络
  - 网络排障
tags:
  - 网络
  - TCP
order: 2
description: 服务器断电、网线被拔、中间的防火墙悄悄删掉了连接，都不会有 FIN 或 RST 通知另一端。本文让 5 个客户端连上服务器，然后让路由器丢弃所有的包：什么也不发的连接 1000 秒后仍然以为一切正常；一直在发数据的连接，靠重传等了 950.9 秒（将近 16 分钟）才报错，期间 send 成功了 948 次；打开 keepalive 的空闲连接 25.4 秒发现；设置了 TCP_USER_TIMEOUT 的连接 13 秒发现。而一直在发送的连接即使打开了 keepalive，也要等满 16 分钟。服务器那边，5 个连接始终都是 ESTABLISHED。
top_group_list: true
disableNunjucks: true
viz: [net-diag]
---

> **本文目标**：理解为什么 TCP 发现不了对端「消失」；知道不同情况下要等多久：空闲的连接永远不会发现，有数据未确认的连接靠重传超时（约 15 分钟）；会用 keepalive（`TCP_KEEPIDLE`、`TCP_KEEPINTVL`、`TCP_KEEPCNT`）和 `TCP_USER_TIMEOUT` 缩短检测时间；理解半开连接；知道为什么应用层通常还需要自己的心跳和超时。
>
> **前置知识**：《TCP 连接管理》（FIN、RST、连接的状态）；《TCP 的可靠传输》（超时重传、RTO 翻倍）。
>
> **实验环境**：同第 1 篇。

## 1. 没有人通知你

TCP 连接正常关闭时，一方发 FIN；程序崩溃时，操作系统替它发 FIN；连接到一个已经不存在的连接上时，对方回复 RST。这些都是「对方告诉你」。但有很多情况，对方根本没有机会告诉你：

- 服务器断电、内核崩溃、虚拟机被强制关闭；
- 网线断了，或者中间的交换机、路由器坏了；
- 中间的防火墙、NAT 网关的连接表超时，悄悄删掉了这条连接，之后的包都被丢弃（第 1 篇的连接跟踪就会这样）。

这时连接的两端都还以为连接是好的。TCP 本身没有「定期检查对方在不在」的机制：**不发数据，就不会有任何包，也就不会发现任何问题。**

实验：`dead server` 在 s 上接受连接，读到的数据全部丢掉；5 个客户端以不同的方式连上去。2 秒后，在路由器上丢弃所有转发的包，模拟网络中断：

```bash
ip netns exec r iptables -A FORWARD -j DROP
```

| 客户端 | 做什么 | 设置 |
|---|---|---|
| A | 空闲，阻塞在 `recv` | 无 |
| B | 空闲，阻塞在 `recv` | keepalive：空闲 10 秒开始探测，每 5 秒一次，3 次 |
| C | 每秒 `send` 100 字节 | 无 |
| D | 每秒 `send` 100 字节 | `TCP_USER_TIMEOUT` = 10 秒 |
| E | 每秒 `send` 100 字节 | keepalive（同 B） |

完整的脚本见代码目录中的 `scripts/deadexp.sh`。结果（时间从连上算起，网络在第 2 秒断开）：

```
[D] 13.0 秒后 send 失败：Connection timed out（已经成功调用 send 13 次）
[B] 25.4 秒后 recv 返回 -1：Connection timed out
[C] 950.9 秒后 send 失败：Connection timed out（已经成功调用 send 948 次）
[E] 950.9 秒后 send 失败：Connection timed out（已经成功调用 send 948 次）
[A] 1002.1 秒后 recv 返回 0：对方关闭了连接
```

<figure class="diagram">
<svg viewBox="0 0 700 250" role="img" aria-label="网络在第 2 秒断开以后，5 个客户端发现问题的时间（从连上算起，横轴按对数刻度）：TCP_USER_TIMEOUT 13 秒，空闲 + keepalive 25.4 秒，一直在发送的连接（无论是否开 keepalive）950.9 秒；空闲且不开 keepalive 的连接 1000 秒后仍未发现">
  <defs><marker id="tml-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="tml-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="220.0" y="39.0" text-anchor="end" style="fill:var(--fg);font-size:12px">D 发送 + USER_TIMEOUT</text><rect x="230.0" y="24.0" width="150.9" height="20.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="388.9" y="39.0" text-anchor="start" style="fill:var(--muted);font-size:11px">13.0 秒</text><text x="220.0" y="73.0" text-anchor="end" style="fill:var(--fg);font-size:12px">B 空闲 + keepalive</text><rect x="230.0" y="58.0" width="190.3" height="20.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="428.3" y="73.0" text-anchor="start" style="fill:var(--muted);font-size:11px">25.4 秒</text><text x="220.0" y="107.0" text-anchor="end" style="fill:var(--fg);font-size:12px">C 一直发送</text><rect x="230.0" y="92.0" width="403.5" height="20.0" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="625.5" y="107.0" text-anchor="end" style="fill:var(--fg);font-size:11px">950.9 秒</text><text x="220.0" y="141.0" text-anchor="end" style="fill:var(--fg);font-size:12px">E 发送 + keepalive</text><rect x="230.0" y="126.0" width="403.5" height="20.0" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="625.5" y="141.0" text-anchor="end" style="fill:var(--fg);font-size:11px">950.9 秒</text><text x="220.0" y="175.0" text-anchor="end" style="fill:var(--fg);font-size:12px">A 空闲</text><rect x="230.0" y="160.0" width="406.5" height="20.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="628.5" y="175.0" text-anchor="end" style="fill:var(--fg);font-size:11px">1000 秒仍未发现</text><line x1="230.0" y1="196" x2="230.0" y2="202" style="stroke:var(--border-2)"/><text x="230.0" y="216.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1 秒</text><line x1="365.5" y1="196" x2="365.5" y2="202" style="stroke:var(--border-2)"/><text x="365.5" y="216.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">10 秒</text><line x1="470.9" y1="196" x2="470.9" y2="202" style="stroke:var(--border-2)"/><text x="470.9" y="216.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1 分</text><line x1="630.3" y1="196" x2="630.3" y2="202" style="stroke:var(--border-2)"/><text x="630.3" y="216.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">15 分</text><line x1="230" y1="196" x2="650" y2="196" style="stroke:var(--border-2)"/><text x="20.0" y="242.0" text-anchor="start" style="fill:var(--muted);font-size:11px">重传：RTO 0.2 秒起翻倍，最大 120 秒，15 次后放弃；keepalive 只在没有未确认数据时工作</text>
  </g>
</svg>
</figure>

逐个来看。

<div class="dsviz" data-viz="nd-dead"></div>

## 2. 空闲的连接：永远不会发现

A 阻塞在 `recv` 上，1000 秒过去了，什么也没发生。它最后返回 0，是因为实验结束时恢复了网络、关掉了服务器，服务器发出的 FIN 才到达。如果网络一直不恢复，A 会**永远**等下去。

这在实际中很常见：一个数据库连接池中的空闲连接，中间的防火墙在它空闲 1 小时后删掉了连接记录；程序下次从池中取出这个连接、发出查询，才开始重传，等十几分钟才报错。

## 3. 有数据在发：等重传超时，约 15 分钟

C 每秒发送 100 字节。网络断了以后，`send` 仍然**每次都成功**，一共成功了 948 次：`send` 只是把数据放进内核的发送缓冲区（《缓冲区与连接的生命周期》），什么时候真正送到，程序不知道。

内核在不断地重传。在断开后的几个时刻用 `ss -tio` 看 C 的连接：

| 断开后 | 重传次数 | 当前的 RTO | 下次重传 |
|---|---|---|---|
| 30 秒 | 7 | 25.7 秒 | 22 秒后 |
| 120 秒 | 9 | 102.9 秒 | 1 分 27 秒后 |
| 300 秒 | 10 | 120 秒 | 31 秒后 |
| 600 秒 | 13 | 120 秒 | 1 分 39 秒后 |

RTO 从 200 毫秒开始每次翻倍：0.2、0.4、0.8、……、25.6、51.2、102.4 秒，然后停在上限 **120 秒**（`TCP_RTO_MAX`）。重传的次数达到 `net.ipv4.tcp_retries2`（默认 **15**）时，内核放弃这个连接，之后的 `send` 返回 `ETIMEDOUT`。把这 15 次的等待加起来约 925 秒，实测是断开后约 949 秒，**将近 16 分钟**。

这期间 `ss` 显示 `unacked:1`：只有一段数据在途。重传超时以后，拥塞窗口降到 1，内核只重传最早的那一段，后面的数据都在发送缓冲区中排队。如果程序发得更多，发送缓冲区满了以后 `send` 会阻塞，程序就卡在 `send` 上。

**`tcp_retries2` 可以调小**，比如 8 次约 100 秒。但它影响这台机器上所有的连接，包括那些只是暂时网络抖动的连接。

## 4. keepalive：给空闲的连接定期发探测

B 打开了 **keepalive**：连接空闲一段时间后，内核自动发一个不带数据的探测包（序号是已经确认过的最后一个字节，对方必须回复 ACK）。连续几次没有回应，就关闭连接。

```c
int one = 1;
setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one);
int idle = 10, intvl = 5, cnt = 3;
setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof idle);    // 空闲多少秒后开始探测
setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof intvl); // 探测的间隔
setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof cnt);       // 连续几次没有回应就放弃
```

预期：空闲 10 秒后第一次探测，之后每 5 秒一次，3 次都没有回应，在 10 + 3 × 5 = 25 秒时放弃。实测 25.4 秒，`recv` 返回 `ETIMEDOUT`。

只打开 `SO_KEEPALIVE` 而不设置后面三个参数，用的是系统的默认值：

```
net.ipv4.tcp_keepalive_time = 7200     # 空闲 2 小时才开始探测
net.ipv4.tcp_keepalive_intvl = 75
net.ipv4.tcp_keepalive_probes = 9
```

2 小时 + 9 × 75 秒，约 2 小时 11 分钟。这么长的时间，连中间设备的连接超时（常见的是几分钟到 1 小时）都防不住。**打开 keepalive，一定要同时设置这三个参数。**

E 也打开了同样的 keepalive，结果却和 C 一样，950.9 秒才报错：**keepalive 只在连接空闲（没有未确认的数据）时才工作**。E 一直在发数据，数据没有得到确认，内核在重传，就不会发 keepalive 探测，只能等重传超时。

## 5. TCP_USER_TIMEOUT：数据多久没被确认就放弃

D 设置了 `TCP_USER_TIMEOUT`：

```c
int ms = 10000;
setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &ms, sizeof ms);
```

含义是：**已经发出的数据，超过这么多毫秒还没有得到确认，就关闭连接**，不必等满 `tcp_retries2` 次重传。D 在连上后 13.0 秒（断开后约 11 秒）报错：10 秒的期限到了以后，在下一次重传的时刻被检查到。

它还有一个作用：和 keepalive 一起使用时，也限制了 keepalive 探测的总时间。所以常见的组合是：

- keepalive（比如 30 秒、10 秒、3 次）负责发现**空闲**连接的问题；
- `TCP_USER_TIMEOUT`（比如 30 秒）负责发现**有数据在途**时的问题。

## 6. 另一端：半开连接

整个实验过程中，s 上一直有 5 个 `ESTABLISHED` 的连接，包括 B、D 这些客户端早已放弃的连接：客户端关闭连接时发出的包也被丢弃了，服务器不知道。服务器从不发数据，也没有打开 keepalive，所以这 5 个连接会一直留在那里，占着文件描述符和内存。

这种一端已经不在了、另一端还以为连接正常的连接，叫**半开连接**（half-open）。服务器上大量这样的连接，会耗尽文件描述符（《缓冲区与连接的生命周期》4.3 节）。所以服务器也需要打开 keepalive，或者在应用层实现空闲超时（《定时器与空闲连接》）。

## 7. 应用层的心跳

既然 TCP 有 keepalive，为什么 Redis、MySQL 的客户端，gRPC、WebSocket 等协议还要在应用层自己发心跳（ping/pong）？

- **keepalive 只能证明对方的内核还活着**。对方的进程可能卡死了（死锁、长时间的 GC），内核仍然会回复 keepalive 的 ACK，连接看起来正常，请求却永远没有回复；
- **它的参数是每个连接用 `setsockopt` 设置的**，很多语言和库没有暴露这些选项，默认的 2 小时又没用；
- 应用层的心跳可以顺便测量往返时间、检查服务的健康状态。

实践中的做法：

1. **每个请求都要有超时**：等回复最多等多久，超过就放弃这个请求（甚至这个连接），而不是阻塞在 `recv` 上；
2. 长时间空闲的连接，用应用层心跳或者 keepalive 保持活跃、及时发现问题；
3. 从连接池中取出长时间空闲的连接时，先检查一下（很多连接池有「借出时校验」的选项）。

## 8. 常见错误

1. **以为对方死了，连接会马上报错**。没有 FIN、没有 RST 时，空闲的连接永远不知道，有数据的连接要等十几分钟。
2. **以为 `send` 成功就是对方收到了**。断网后 `send` 还成功了 948 次。
3. **只打开 `SO_KEEPALIVE`**。默认要空闲 2 小时才开始探测。
4. **以为 keepalive 能发现所有的问题**。有数据未确认时它不工作；对方的进程卡死时它也发现不了。
5. **读写没有超时**。阻塞在 `recv` 上的线程可能永远不会返回。
6. **只在客户端处理，不管服务器上的半开连接**。

## 9. 小结

- 对方断电、网络中断时没有任何通知，空闲的连接永远不会发现（实验中 1000 秒都没发现）；
- 有未确认的数据时，靠重传超时：RTO 从 0.2 秒翻倍到 120 秒，重传 15 次后放弃，实测约 16 分钟，期间 `send` 一直成功；
- keepalive 只对空闲的连接有效，三个参数都要设置；实测 10 秒、5 秒、3 次的设置在 25.4 秒发现；
- `TCP_USER_TIMEOUT` 限制数据未被确认的最长时间，实测 10 秒的设置在 11 秒左右发现；
- 服务器上会留下半开连接；应用层还需要请求超时和心跳。

## 10. 练习

1. 一个服务通过长连接访问数据库，每天凌晨流量很低。早上第一批请求常常要等十几分钟才报错，之后就恢复正常。可能是什么原因？怎么解决？
2. 把 keepalive 设成空闲 60 秒、间隔 10 秒、3 次，再把 `TCP_USER_TIMEOUT` 设成 20 秒。连接空闲时网络断了，多久能发现？
3. 为什么说「keepalive 的探测成功，不代表服务正常」？举一个例子。

<details>
<summary>点击查看答案</summary>

1. 很可能是中间的防火墙或 NAT 网关在连接空闲一段时间后删除了连接记录（常见的空闲超时是几分钟到 1 小时）。连接池中的连接两端都以为连接正常，早上第一次使用时发出查询，数据包被中间设备丢弃，客户端开始重传，等满 `tcp_retries2` 次（约 15 分钟）才报错。解决：打开 keepalive 并把空闲时间设得比中间设备的超时短（比如 60 秒），让连接一直有流量；或者连接池定期检查空闲连接、限制连接的最长空闲时间；同时给查询设置超时，并用 `TCP_USER_TIMEOUT` 缩短发现故障的时间。

2. 只看 keepalive，是 60 + 3 × 10 = 90 秒。设置了 `TCP_USER_TIMEOUT` 以后，内核在 keepalive 的定时器中检查：已经发出过探测、而且距离上次收到对方的数据已经超过 20 秒，就关闭连接。第一个探测在 60 秒发出，到第 70 秒该发第二个探测时，这个条件已经满足，于是关闭。用本章的程序实测（`dead client ... idle ka=60,10,3 ut=20000`，连上 1 秒后断网）：72.9 秒后 `recv` 返回 `ETIMEDOUT`，比只用 keepalive 的 90 秒早了约 17 秒。

3. keepalive 的探测和回复都由内核完成，只要对方的内核还在运行、网络是通的，就会回复。比如数据库进程因为死锁卡住了，或者 Java 程序在做长时间的垃圾回收，它们的内核仍然正常回复 keepalive，连接看起来是好的，但发出去的请求永远得不到回复。所以需要应用层的心跳（由对方的程序回复）和请求超时。

</details>
