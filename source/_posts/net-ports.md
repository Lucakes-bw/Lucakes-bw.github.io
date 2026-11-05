---
title: 短连接与端口耗尽
date: 2026-11-11 10:30:00
updated: 2026-11-11 10:30:00
categories:
  - 计算机网络
  - 网络排障
tags:
  - 网络
  - TCP
order: 4
description: 客户端每个请求都新建一个连接，用完就关。每个关闭的连接都在客户端留下一个 TIME_WAIT，占着一个本地端口 60 秒，而本地端口一共只有 28232 个。本文实测：第 28233 个连接失败，错误是 Cannot assign requested address，越接近用完，建立连接越慢，从每秒 4.7 万次降到 3 千次。在默认设置下却没能复现，因为这台 2 GB 内存的机器最多只保留 8192 个 TIME_WAIT，多出来的被直接丢掉了。再看 tcp_tw_reuse 怎样解决、长连接快多少：往返 1 毫秒时，长连接每秒 720 个请求，短连接只有 388 个。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解一个 TCP 连接由四元组标识，客户端的本地端口从 `ip_local_port_range` 中分配；理解主动关闭的一方进入 TIME_WAIT，会占用本地端口 60 秒；会诊断 `EADDRNOTAVAIL`（`Cannot assign requested address`）；理解 `tcp_max_tw_buckets`、`tcp_tw_reuse` 的作用；知道为什么应该用长连接、连接池。
>
> **前置知识**：《TCP 连接管理》（四次挥手、TIME_WAIT 的作用）；《HTTP/1.1》（持久连接）；本章第 1 篇（连接跟踪表也会被短连接耗尽）。
>
> **实验环境**：同第 1 篇，没有延迟和限速（往返时间约 0.1 毫秒），最后一节加 1 毫秒的往返时间。

## 1. 端口从哪里来

一个 TCP 连接由**四元组**唯一标识：（本地 IP，本地端口，对方 IP，对方端口）。客户端 `connect` 时如果没有 `bind`，内核从一个范围中给它选一个本地端口：

```
net.ipv4.ip_local_port_range = 32768	60999
```

一共 60999 − 32768 + 1 = **28232** 个。客户端连接同一个服务器的同一个端口时，后三项都一样，只有本地端口不同，所以**同时最多只能有 28232 个连接**。

「同时」不只是指正在使用的连接。《TCP 连接管理》中看到，**先关闭连接的一方**要进入 TIME_WAIT 状态，等 60 秒（Linux 固定为 60 秒，不能用 `tcp_fin_timeout` 调整），确保对方没有收到的最后一个 ACK 可以重传、网络中残留的旧包已经消失。这 60 秒中，四元组仍然被占用，本地端口不能分配给新的连接去连同一个目标。

所以，客户端如果每个请求都新建连接、用完主动关闭，60 秒内最多只能建立约 28232 个连接，平均每秒 470 个。超过这个速度，端口就会用完。

## 2. 实验：每个请求一个连接

`churn server` 在 s 上运行：收到 1 字节就回复 1 字节，读到对方关闭以后再关闭，所以 TIME_WAIT 留在客户端。`churn client` 每个请求都新建连接，发 1 字节、收 1 字节、关闭：

```c
for (i = 0; i < n; i++) {
    int fd = connectTo(ip, port);
    if (fd < 0) {
        printf("第 %ld 次连接失败（%.2f 秒）：%s\n", i + 1, nowSeconds() - t0, strerror(errno));
        break;
    }
    CHECK(request(fd) == 0);   // 发 1 字节，收 1 字节
    close(fd);                 // 客户端先关闭
}
```

连续做 40000 次：

```
  5000 次，0.14 秒（这 5000 次每秒 35661 次）
 10000 次，0.25 秒（这 5000 次每秒 47318 次）
 ...
 40000 次，0.92 秒（这 5000 次每秒 39331 次）
短连接：成功 40000 次，0.92 秒，每秒 43594 次
c 上：timewait 8192
```

1 秒内 40000 个连接，全部成功，超过了 28232。而 TIME_WAIT 只有 8192 个，剩下的去哪里了？

```
net.ipv4.tcp_max_tw_buckets = 8192
```

`nstat` 中：

```
TcpExtTW                        8192
TcpExtTCPTimeWaitOverflow       11808
```

内核最多同时保留 `tcp_max_tw_buckets` 个 TIME_WAIT，超过的连接**直接关闭，跳过 TIME_WAIT**，`TCPTimeWaitOverflow` 记下了跳过的次数。它的默认值和内存有关，这台 2 GB 内存的虚拟机上只有 8192，远小于端口数，端口永远用不完；内存大的服务器上默认值也大（比如 65536 甚至更多），就会遇到端口耗尽。这个限制本来是为了防止大量 TIME_WAIT 占用内存，跳过 TIME_WAIT 也就放弃了它的保护。

## 3. 端口用完了

把 `tcp_max_tw_buckets` 调到 262144，模拟内存大的服务器，等 60 秒让之前的 TIME_WAIT 消失，再做一次：

```
  5000 次，0.14 秒（这 5000 次每秒 36408 次）
 10000 次，0.24 秒（这 5000 次每秒 46860 次）
 15000 次，0.60 秒（这 5000 次每秒 14065 次）
 20000 次，2.15 秒（这 5000 次每秒 3231 次）
 25000 次，3.83 秒（这 5000 次每秒 2974 次）
第 28233 次连接失败（4.96 秒）：Cannot assign requested address
短连接：成功 28232 次，4.96 秒，每秒 5690 次
c 上：timewait 28232
```

第 28233 次失败，正好是端口的总数。`connect` 返回 `EADDRNOTAVAIL`，错误信息是「Cannot assign requested address」，看上去像是 IP 地址的问题，其实是**没有可用的本地端口**了。之后的 60 秒内，每一个新连接都立刻失败。

<figure class="diagram">
<svg viewBox="0 0 700 250" role="img" aria-label="短连接每 5000 次的速度：端口占用越多，connect 找空闲端口越慢，从每秒 4.7 万次降到约 3000 次，第 28233 次时端口用完，connect 返回 EADDRNOTAVAIL">
  <defs><marker id="rte-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rte-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <line x1="90" y1="190.0" x2="610" y2="190.0" style="stroke:var(--border);stroke-width:1"/><text x="82.0" y="194.0" text-anchor="end" style="fill:var(--muted);font-size:10px">0</text><line x1="90" y1="156.0" x2="610" y2="156.0" style="stroke:var(--border);stroke-width:1"/><text x="82.0" y="160.0" text-anchor="end" style="fill:var(--muted);font-size:10px">10k</text><line x1="90" y1="122.0" x2="610" y2="122.0" style="stroke:var(--border);stroke-width:1"/><text x="82.0" y="126.0" text-anchor="end" style="fill:var(--muted);font-size:10px">20k</text><line x1="90" y1="88.0" x2="610" y2="88.0" style="stroke:var(--border);stroke-width:1"/><text x="82.0" y="92.0" text-anchor="end" style="fill:var(--muted);font-size:10px">30k</text><line x1="90" y1="54.0" x2="610" y2="54.0" style="stroke:var(--border);stroke-width:1"/><text x="82.0" y="58.0" text-anchor="end" style="fill:var(--muted);font-size:10px">40k</text><line x1="90" y1="20.0" x2="610" y2="20.0" style="stroke:var(--border);stroke-width:1"/><text x="82.0" y="24.0" text-anchor="end" style="fill:var(--muted);font-size:10px">50k</text><rect x="96.7" y="66.2" width="70.0" height="123.8" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="131.7" y="61.2" text-anchor="middle" style="fill:var(--muted);font-size:10px">36408</text><text x="131.7" y="206.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1～5000</text><rect x="183.3" y="30.7" width="70.0" height="159.3" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="218.3" y="25.7" text-anchor="middle" style="fill:var(--muted);font-size:10px">46860</text><text x="218.3" y="206.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">5001～10000</text><rect x="270.0" y="142.2" width="70.0" height="47.8" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="305.0" y="137.2" text-anchor="middle" style="fill:var(--muted);font-size:10px">14065</text><text x="305.0" y="206.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">10001～15000</text><rect x="356.7" y="179.0" width="70.0" height="11.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="391.7" y="174.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">3231</text><text x="391.7" y="206.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">15001～20000</text><rect x="443.3" y="179.9" width="70.0" height="10.1" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="478.3" y="174.9" text-anchor="middle" style="fill:var(--muted);font-size:10px">2974</text><text x="478.3" y="206.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">20001～25000</text><line x1="579.4" y1="20" x2="579.4" y2="190" style="stroke:var(--danger);stroke-width:2"/><text x="573.4" y="34.0" text-anchor="end" style="fill:var(--danger);font-size:11px">28232：端口用完</text><text x="94.0" y="14.0" text-anchor="start" style="fill:var(--muted);font-size:10px">每秒连接数</text><text x="350.0" y="240.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">第几个连接（tcp_max_tw_buckets 调到 262144，端口范围 32768～60999）</text>
  </g>
</svg>
</figure>

还有一个容易被忽略的现象：**还没用完时，建立连接就已经变慢了**。前 10000 个每秒 4.7 万次，到 2 万个以后只有每秒 3000 次左右，慢了十几倍。`connect` 选端口时，从一个随机的位置开始，逐个检查端口是否可用；被占用的端口越多，要检查的次数就越多。所以在端口耗尽之前，客户端的 CPU 会先升高、请求变慢，这是一个预警信号。

诊断的方法：

```bash
ss -s                              # 看 TCP 中 timewait 的数量
ss -tan state time-wait | wc -l    # 同上
ss -tan state time-wait dst 10.0.2.2:8080 | wc -l   # 只看连到某个目标的
```

TIME_WAIT 的数量接近端口范围的大小，而且客户端报 `EADDRNOTAVAIL`，就是这个问题。

## 4. 解决办法

**tcp_tw_reuse**。允许新的连接复用处于 TIME_WAIT 的四元组，前提是那个 TIME_WAIT 已经存在了 1 秒以上、双方都开启了 TCP 时间戳（用时间戳区分新旧连接的包）。默认值 2 表示只对回环地址生效，改成 1：

```bash
sysctl -w net.ipv4.tcp_tw_reuse=1
```

```
 25000 次，0.60 秒（这 5000 次每秒 34317 次）
 30000 次，1.32 秒（这 5000 次每秒 6978 次）
 35000 次，1.43 秒（这 5000 次每秒 46710 次）
 40000 次，1.53 秒（这 5000 次每秒 46751 次）
短连接：成功 40000 次，1.53 秒，每秒 26062 次
```

40000 次全部成功。中间有一段变慢：端口快用完时，只能复用存在超过 1 秒的 TIME_WAIT。接着立刻再做一次（所有端口都处于 TIME_WAIT），40000 次用了 2.80 秒，每秒 14275 次，仍然能用，只是慢一些。它只影响主动发起连接的一方，对服务器接受连接没有作用。

**扩大端口范围**。比如 `1024 65535`，能多出一倍多，但要避开本机服务使用的端口。

**连接多个目标**。四元组中对方的 IP 和端口不同，本地端口就可以重复使用。连接 10 个后端，就有 10 × 28232 个可用的四元组。

**不推荐的做法**：`SO_LINGER` 设为 0 让 `close` 直接发 RST、不进入 TIME_WAIT。旧连接的包可能被新连接收到，而且对方会看到「连接被重置」的错误。早期内核的 `tcp_tw_recycle` 在 NAT 后面会导致连接失败，已经在 4.12 中删除了。

**根本的办法：用长连接**。HTTP/1.1 的持久连接、数据库的连接池、RPC 框架的连接复用，都是为了不在每个请求上建立和关闭连接。

## 5. 长连接快多少

`churn reuse` 在一个连接上发完所有的请求。在这个往返时间约 0.1 毫秒的实验网络中：

```
长连接：40000 次请求，0.69 秒，每秒 58054 次
短连接：成功 28231 次，4.80 秒，每秒 5881 次
```

短连接这一次也撞到了端口耗尽（28231 次，比上一次少一个，因为长连接自己也留下了一个 TIME_WAIT），平均速度被拖慢了。端口充足时，短连接每秒约 4.7 万次，长连接快 20% 左右，差别不大，因为这里的往返时间太短了。

在路由器上加 0.5 毫秒的单程延迟，往返时间约 1.3 毫秒，接近同一个机房内两台服务器之间的情况：

```
长连接：3000 次请求，4.17 秒，每秒 720 次
短连接：成功 3000 次，7.73 秒，每秒 388 次
```

长连接快了将近一倍。每个短连接的请求都要先花一个往返时间做三次握手，再花一个往返时间完成请求：两个往返时间对一个。如果是 HTTPS，TLS 握手还要再加一到两个往返时间（《HTTPS 与 TLS》），差距更大。再加上短连接给连接跟踪表、TIME_WAIT 带来的负担，**能用长连接的地方就不要用短连接**。

## 6. 常见错误

1. **看到「Cannot assign requested address」就去查 IP 地址**。在高频率建立连接的客户端上，这几乎总是本地端口用完了。
2. **以为 TIME_WAIT 在服务器上才是问题**。谁先关闭，TIME_WAIT 就在谁那里；客户端主动关闭时，端口耗尽在客户端。
3. **用 `tcp_fin_timeout` 缩短 TIME_WAIT**。它控制的是 FIN_WAIT_2 的超时，Linux 的 TIME_WAIT 固定 60 秒。
4. **只在测试机上验证，没有遇到问题**。小内存的机器 `tcp_max_tw_buckets` 小，TIME_WAIT 被直接丢弃，问题不会出现；到了大内存的生产服务器上才出现。
5. **用 `SO_LINGER` 为 0 来「解决」TIME_WAIT**。
6. **HTTP 客户端每个请求都新建连接**。很多语言的 HTTP 库默认会复用连接，但前提是每次复用同一个客户端对象，并把响应体读完、关闭。

## 7. 小结

- 连接同一个目标时，客户端最多有 28232 个本地端口可用；主动关闭的一方的 TIME_WAIT 占用端口 60 秒；
- 实测：调大 `tcp_max_tw_buckets` 后，第 28233 个连接失败，`EADDRNOTAVAIL`；用完之前建立连接就从每秒 4.7 万次降到 3000 次；
- 默认的 `tcp_max_tw_buckets` 和内存有关，这台机器上是 8192，超过的 TIME_WAIT 被直接跳过（`TCPTimeWaitOverflow`），所以在小内存的机器上复现不了；
- `tcp_tw_reuse = 1` 让客户端复用存在超过 1 秒的 TIME_WAIT，实测 40000 次全部成功；
- 往返 1.3 毫秒时，长连接每秒 720 个请求，短连接 388 个；根本的办法是长连接和连接池。

## 8. 练习

1. 一个服务每秒向同一个 Redis 发起 800 个新连接，每次用完就关闭。按默认的端口范围，它能正常运行吗？
2. 为什么 `churn server` 要等读到对方关闭以后才关闭，而不是回复完就关闭？如果服务器先关闭，客户端还会端口耗尽吗？服务器上会怎样？
3. 本文的实验中，第 1 篇的连接跟踪表会不会先于端口耗尽而满？（连接跟踪表默认 65536 条，记录在连接关闭后保留 120 秒）

<details>
<summary>点击查看答案</summary>

1. 不能长期正常运行。每个连接关闭后在客户端留下 60 秒的 TIME_WAIT，稳定时同时存在 800 × 60 = 48000 个，超过了 28232 个端口（前提是 `tcp_max_tw_buckets` 足够大）。大约 35 秒后端口就会用完，之后新连接失败。应该改用连接池；临时的办法是开启 `tcp_tw_reuse`、扩大端口范围。

2. 为了让客户端先关闭，把 TIME_WAIT 留在客户端，复现客户端端口耗尽的问题。如果服务器先关闭，TIME_WAIT 留在服务器上，客户端的连接直接关闭、端口立即释放，客户端不会端口耗尽。服务器上会积累大量 TIME_WAIT，但服务器的四元组中客户端的端口各不相同，不会互相冲突，只是占用一些内存（受 `tcp_max_tw_buckets` 限制）。这也是 HTTP 服务器经常主动关闭空闲连接的原因之一。

3. 实验中的连接经过路由器 r，但 r 上没有使用连接跟踪的规则，所以没有连接跟踪表。如果有，记录在连接关闭后保留 120 秒，比 TIME_WAIT 的 60 秒还长；表的上限 65536 大于端口数 28232，所以这个实验中仍然是端口先耗尽。但如果有多个客户端经过同一个网关，网关上的连接跟踪表就会先满（第 1 篇的计算：每秒约 546 个新连接）。

</details>
