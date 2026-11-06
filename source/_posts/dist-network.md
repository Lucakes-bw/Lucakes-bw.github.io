---
title: 网络不可靠：超时、重试与幂等
date: 2026-11-14 10:00:00
updated: 2026-11-14 10:00:00
categories:
  - 分布式系统
  - 分布式基础
tags:
  - 分布式系统
  - 网络
  - 幂等
order: 1
description: 分布式系统的第一个难题是网络：请求发出去之后，可能丢、可能慢、回复也可能丢，而客户端只能看到「超时」。本文用 C 写一个键值服务器 dkv，在容器之间用 tc netem 制造延迟和丢包实测：1% 的丢包让 p99 延迟从 1.4 毫秒变成 205 毫秒；超时就重试的加一操作，2000 次多加了 94 次；即使丢的是请求、不是回复，重试也同样造成了重复，因为关掉连接并不能撤回已经发出的请求；用客户端编号加序号去重之后，115 次重试全部被识别出来，计数器正好是 2000。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解分布式系统和单机程序的根本区别：部分失败；理解请求超时之后，客户端无法知道请求到底执行了没有；会用 `tc netem` 制造延迟和丢包，看懂 TCP 重传对尾延迟的影响；理解重试为什么会造成重复执行；会用「客户端编号 + 序号」实现幂等。
>
> **前置知识**：《计算机网络》专栏的《TCP 可靠传输》（重传与 RTO）和《Socket 编程》；《Linux 基础》专栏的《POSIX 线程》。本专栏的完整代码在 [projects/dkv](https://github.com/Lucakes-bw/Lucakes-bw.github.io/tree/main/projects/dkv)。
>
> **实验环境**：Docker 中的 4 个 Ubuntu 24.04 容器，在同一个网桥上：n1、n2、n3（10.77.0.11～13）运行节点，c1（10.77.0.21）运行客户端；内核 6.8，GCC 13.3；虚拟机 2 核、2 GB 内存。延迟和丢包用 `tc netem` 加在容器的网卡上，网络分区用 `iptables`。

## 1. 分布式系统难在哪里

一台机器上的程序，要么正常运行，要么整个崩溃。函数调用不会「调用了一半」，内存不会「有一部分写进去了，另一部分没收到」。

多台机器通过网络协作时，情况完全不同：**有的部分坏了，有的部分好好的**，而且你常常**不知道是哪一部分坏了**。这叫**部分失败**（partial failure），它是分布式系统所有难题的根源：

- 网络会丢包、延迟、乱序，还会把一部分机器和另一部分隔开（网络分区）；
- 机器会崩溃、重启，也会「卡住」几秒钟再恢复（垃圾回收、虚拟机迁移、换页）；
- 每台机器的时钟都不一样准。

本专栏的路线是：**先把这些问题一个个测出来，再学解决它们的算法**。第一章「分布式基础」用一个故意写得很朴素的键值存储 dkv 做实验：

1. 网络不可靠：超时、重试与幂等（本文）；
2. 故障检测：心跳与超时；
3. 主从复制：同步与异步；
4. 故障转移与脑裂；
5. 时钟与事件顺序；
6. 法定人数：读写都过半。

第一章最后会看到，朴素的主从复制加自动故障转移，在网络分区时会丢掉已经确认的写入，甚至出现两个主节点。下一章用 Raft 解决这些问题。

<figure class="diagram">
<svg viewBox="0 0 700 270" role="img" aria-label="实验环境：同一个 Docker 网桥 10.77.0.0/24 上的 4 个容器。n1、n2、n3 运行节点程序 kvd，c1 运行客户端 cli 和故障检测器 hb；延迟和丢包用 tc netem 加在容器的网卡上，网络分区用 iptables 制造">
  <defs><marker id="arch-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="arch-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="10" y="10" width="680" height="250" rx="8" style="fill:none;stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:5 4"/><text x="24.0" y="32.0" text-anchor="start" style="fill:var(--muted);font-size:11px">Docker 网桥 dnet：10.77.0.0/24</text><rect x="30" y="50" width="140" height="64" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="100.0" y="70.9" text-anchor="middle" style="fill:var(--fg);font-size:13px">n1  .11</text><text x="100.0" y="86.9" text-anchor="middle" style="fill:var(--muted);font-size:11px">kvd：键值表</text><text x="100.0" y="102.9" text-anchor="middle" style="fill:var(--muted);font-size:11px">主 / 从节点</text><rect x="190" y="50" width="140" height="64" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="260.0" y="70.9" text-anchor="middle" style="fill:var(--fg);font-size:13px">n2  .12</text><text x="260.0" y="86.9" text-anchor="middle" style="fill:var(--muted);font-size:11px">kvd：键值表</text><text x="260.0" y="102.9" text-anchor="middle" style="fill:var(--muted);font-size:11px">主 / 从节点</text><rect x="350" y="50" width="140" height="64" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="420.0" y="70.9" text-anchor="middle" style="fill:var(--fg);font-size:13px">n3  .13</text><text x="420.0" y="86.9" text-anchor="middle" style="fill:var(--muted);font-size:11px">kvd：键值表</text><text x="420.0" y="102.9" text-anchor="middle" style="fill:var(--muted);font-size:11px">主 / 从节点</text><rect x="530" y="50" width="140" height="64" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="600.0" y="70.9" text-anchor="middle" style="fill:var(--fg);font-size:13px">c1  .21</text><text x="600.0" y="86.9" text-anchor="middle" style="fill:var(--muted);font-size:11px">cli：读写、压测</text><text x="600.0" y="102.9" text-anchor="middle" style="fill:var(--muted);font-size:11px">hb：心跳检测</text><line x1="40.0" y1="150.0" x2="660.0" y2="150.0" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="100.0" y1="114.0" x2="100.0" y2="150.0" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="260.0" y1="114.0" x2="260.0" y2="150.0" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="420.0" y1="114.0" x2="420.0" y2="150.0" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="600.0" y1="114.0" x2="600.0" y2="150.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="350.0" y="170.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">网桥：同一台虚拟机内，往返约 0.02 ms</text><rect x="30" y="192" width="300" height="52" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="180.0" y="214.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">tc qdisc ... netem</text><text x="180.0" y="230.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">延迟、抖动、丢包（作用在发出的包上）</text><rect x="370" y="192" width="300" height="52" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="520.0" y="214.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">iptables -j DROP</text><text x="520.0" y="230.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">切断两个节点之间的网络（分区）</text>
  </g>
</svg>
</figure>

dkv 的节点程序 `kvd` 是一个内存中的键值表，用一个简单的文本协议，一行一条命令：

```
GET k            -> OK v | NF
PUT k v          -> OK
INCR k           -> OK n          把 k 的值加一，返回新值
```

每个客户端连接一个线程，所有操作在一把全局锁下执行。和本文有关的是 `INCR`：

```c
} else if (strncmp(line, "INCR ", 5) == 0) {
    Slot *s = lookup(k, 1);
    long val = s ? atol(s->val) + 1 : 1;
    snprintf(v, sizeof v, "%ld", val);
    setKey(k, v);
    snprintf(out, sizeof out, "OK %ld\n", val);
}
```

## 2. 网络有多慢

先看正常情况。c1 上的 `cli lat` 顺序发 2 万个 `GET`，每个等到回复再发下一个：

```
$ cli lat 10.77.0.11:7000 20000
get: n=20000 mean=0.019 p50=0.018 p90=0.022 p99=0.028 p99.9=0.037 max=0.053 ms
total 0.38 s, 52440 ops/s, >100ms: 0
```

同一台虚拟机中的两个容器，一次往返只要 18 微秒。真实机房中同一机架约 0.1 毫秒，跨机房几毫秒到几十毫秒。

接下来用 `tc netem` 在 n1 的网卡上加 1 毫秒的延迟，再加上 1%、5% 的丢包。netem 作用在发出的包上，所以这里延迟和丢掉的都是 n1 发回的回复：

```bash
tc qdisc replace dev eth0 root netem delay 1ms
tc qdisc replace dev eth0 root netem delay 1ms loss 1%
tc qdisc replace dev eth0 root netem delay 1ms loss 5%
```

每种情况发 5000 个请求：

| n1 发出的包 | 平均 | p50 | p99 | 最大 | 每秒 | 超过 100 ms |
|---|---|---|---|---|---|---|
| 不加 | 0.019 | 0.018 | 0.028 | 0.053 | 52440 | 0 |
| 延迟 1 ms | 1.376 | 1.391 | 1.466 | 14.5 | 726 | 0 |
| 延迟 1 ms，丢 1% | 3.787 | 1.390 | **205.5** | 415.7 | 264 | 56 |
| 延迟 1 ms，丢 5% | 11.470 | 1.395 | **212.3** | 623.4 | 87 | 236 |

（单位毫秒。加 1 毫秒延迟后实测是 1.39 毫秒，多出来的是 netem 定时器的精度。）

<figure class="diagram">
<svg viewBox="0 0 700 230" role="img" aria-label="顺序发 5000 个 GET 的延迟（对数坐标）。不加延迟时 p99 0.028 毫秒；n1 发出的包延迟 1 毫秒后 p99 1.47 毫秒；再丢 1% 的包，p99 变成 205 毫秒，最大 416 毫秒；丢 5%，p99 212 毫秒，最大 623 毫秒">
  <defs><marker id="lat-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="lat-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="180.0" y="40.0" text-anchor="end" style="fill:var(--fg);font-size:12px">不加</text><rect x="190.0" y="24.0" width="40.2" height="22.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><circle cx="213.0" cy="35.0" r="4" style="fill:var(--fg)"/><path d="M255.2,27 L255.2,43" style="stroke:var(--fg-2);stroke-width:2"/><text x="180.0" y="80.0" text-anchor="end" style="fill:var(--fg);font-size:12px">延迟 1 ms</text><rect x="190.0" y="64.0" width="195.0" height="22.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><circle cx="382.9" cy="75.0" r="4" style="fill:var(--fg)"/><path d="M474.5,67 L474.5,83" style="stroke:var(--fg-2);stroke-width:2"/><text x="180.0" y="120.0" text-anchor="end" style="fill:var(--fg);font-size:12px">延迟 1 ms，丢 1%</text><rect x="190.0" y="104.0" width="388.2" height="22.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><circle cx="382.9" cy="115.0" r="4" style="fill:var(--fg)"/><path d="M605.7,107 L605.7,123" style="stroke:var(--fg-2);stroke-width:2"/><text x="180.0" y="160.0" text-anchor="end" style="fill:var(--fg);font-size:12px">延迟 1 ms，丢 5%</text><rect x="190.0" y="144.0" width="389.4" height="22.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><circle cx="383.0" cy="155.0" r="4" style="fill:var(--fg)"/><path d="M621.5,147 L621.5,163" style="stroke:var(--fg-2);stroke-width:2"/><line x1="190.0" y1="182.0" x2="190.0" y2="188.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="190.0" y="204.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">0.01</text><line x1="280.0" y1="182.0" x2="280.0" y2="188.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="280.0" y="204.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">0.1</text><line x1="370.0" y1="182.0" x2="370.0" y2="188.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="370.0" y="204.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1</text><line x1="460.0" y1="182.0" x2="460.0" y2="188.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="460.0" y="204.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">10</text><line x1="550.0" y1="182.0" x2="550.0" y2="188.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="550.0" y="204.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">100</text><line x1="640.0" y1="182.0" x2="640.0" y2="188.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="640.0" y="204.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1000</text><line x1="190.0" y1="182.0" x2="640.0" y2="182.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="190.0" y="224.0" text-anchor="start" style="fill:var(--muted);font-size:11px">单位毫秒。条：p99    圆点：p50    竖线：最大值</text>
  </g>
</svg>
</figure>

只丢 1% 的包，中位数完全不变，p99 却从 1.5 毫秒变成了 205 毫秒，平均延迟变成 2.7 倍，吞吐量只剩三分之一多。原因是 TCP 的重传：一个包丢了，发送方要等**重传超时**（RTO）才重发，Linux 的 RTO 最小是 200 毫秒。所以丢一次就是 200 多毫秒，同一个包连丢两次就是 200 + 400 = 600 毫秒（RTO 每次翻倍），这就是表中的最大值 623 毫秒。

**丢包不是让所有请求变慢一点，而是让少数请求慢几百倍。** 一个页面如果要调用 100 个后端服务，只要其中一个碰上 p99，整个页面就慢了，这叫**尾延迟放大**。

## 3. 超时之后，请求执行了没有

客户端不能无限地等，总要设一个超时。问题是：**超时之后，客户端不知道发生了什么**。

<figure class="diagram">
<svg viewBox="0 0 700 250" role="img" aria-label="客户端等了 100 毫秒没有收到回复，有三种可能：请求在路上丢了，服务器没有执行；服务器还在处理；服务器执行完了，回复丢了。客户端看到的都一样，无法区分">
  <defs><marker id="timeout3-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="timeout3-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="120.0" y="22.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">① 请求丢了</text><text x="50.0" y="44.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">客户端</text><text x="190.0" y="44.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">服务器</text><line x1="50.0" y1="52.0" x2="50.0" y2="210.0" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="190.0" y1="52.0" x2="190.0" y2="210.0" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="50" y1="66" x2="130" y2="96" style="stroke:var(--fg-2);stroke-width:1.5"/><path d="M130,94 L142,106 M142,94 L130,106" style="stroke:var(--danger);stroke-width:2.2"/><text x="184.0" y="140.0" text-anchor="end" style="fill:var(--muted);font-size:11px">没收到</text><line x1="38" y1="196" x2="62" y2="196" style="stroke:var(--danger);stroke-width:2"/><text x="66.0" y="200.0" text-anchor="start" style="fill:var(--danger);font-size:11px">100 ms 超时</text><text x="348.0" y="22.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">② 服务器还在处理</text><text x="278.0" y="44.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">客户端</text><text x="418.0" y="44.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">服务器</text><line x1="278.0" y1="52.0" x2="278.0" y2="210.0" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="418.0" y1="52.0" x2="418.0" y2="210.0" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="278" y1="66" x2="416" y2="110" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#timeout3-a)"/><rect x="413" y="112" width="10" height="90" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="408.0" y="160.0" text-anchor="end" style="fill:var(--muted);font-size:11px">处理中…</text><line x1="266" y1="196" x2="290" y2="196" style="stroke:var(--danger);stroke-width:2"/><text x="294.0" y="200.0" text-anchor="start" style="fill:var(--danger);font-size:11px">100 ms 超时</text><text x="576.0" y="22.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">③ 回复丢了</text><text x="506.0" y="44.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">客户端</text><text x="646.0" y="44.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">服务器</text><line x1="506.0" y1="52.0" x2="506.0" y2="210.0" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="646.0" y1="52.0" x2="646.0" y2="210.0" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="506" y1="66" x2="644" y2="100" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#timeout3-a)"/><rect x="641" y="102" width="10" height="16" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="636.0" y="98.0" text-anchor="end" style="fill:var(--accent);font-size:11px">执行了</text><line x1="646" y1="122" x2="576" y2="152" style="stroke:var(--fg-2);stroke-width:1.5"/><path d="M564,150 L576,162 M576,150 L564,162" style="stroke:var(--danger);stroke-width:2.2"/><line x1="494" y1="196" x2="518" y2="196" style="stroke:var(--danger);stroke-width:2"/><text x="522.0" y="200.0" text-anchor="start" style="fill:var(--danger);font-size:11px">100 ms 超时</text><text x="350.0" y="238.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">在客户端看来三者完全一样；只有 ① 可以安全地重试</text>
  </g>
</svg>
</figure>

三种情况在客户端看来一模一样，都是「等了 100 毫秒，什么也没收到」：

1. 请求在路上丢了，服务器根本没收到；
2. 服务器收到了，正在处理（或者在排队、在等锁、在做垃圾回收）；
3. 服务器已经处理完了，回复在路上丢了。

情况 1 应该重试，情况 3 绝对不能重试（会执行两次），而客户端没有办法区分它们。这不是 dkv 的缺陷，而是网络的本质：**通过不可靠的信道，发送方永远无法确定对方是否收到了**（「两将军问题」）。

## 4. 重试会造成重复

最常见的做法是「超时就重试」。`cli incr` 做 2000 次加一，每次等 100 毫秒，超时就关掉连接、重新连接、重发同一个请求，直到收到回复：

```c
for (;;) {                                      /* 直到收到回复为止 */
    if (fd < 0) { fd = tcpConnect(addr, 1000); ... }
    if (call(fd, &r, req, resp, sizeof resp, tmo) > 0 && !strncmp(resp, "OK", 2)) break;
    retries++;                                  /* 超时：不知道执行了没有，只能重发 */
    close(fd);
    fd = -1;
}
```

全部做完后，等 1.5 秒让还在路上的旧请求都到达，再读出计数器。n1 发出的包丢 5%（丢的是回复）：

```
$ cli incr 10.77.0.11:7000 2000 100 0 1
incr: n=2000 mean=8.906 p50=1.391 p90=1.425 p99=107.767 p99.9=1161.721 max=1162.922 ms
increments 2000, retries 94, 17.8 s, counter = 2094, extra = 94
```

**重试了 94 次，计数器就多加了 94。** 回复丢了，说明请求已经执行过；重试让它又执行了一次。如果这是扣款，用户就被多扣了 94 次。

p99.9 是 1162 毫秒：重新连接时，如果 SYN 的回复（SYN-ACK）也丢了，TCP 要等 1 秒才重发 SYN。

那么丢的是**请求**而不是回复时，重试应该是安全的吧？把丢包改到 c1 的网卡上（丢 c1 发出的包，也就是请求）：

```
$ cli incr 10.77.0.11:7000 2000 100 0 3        # c1 发出的包丢 5%
incr: n=2000 mean=7.405 p50=1.390 p90=1.415 p99=107.585 p99.9=209.968 max=1163.400 ms
increments 2000, retries 96, 14.8 s, counter = 2096, extra = 96
```

**还是一次重试多加一次。** 这是我没有预料到的：请求丢了，客户端 100 毫秒后超时，关掉连接，重连后重发；但是**被关掉的旧连接上，TCP 还会在 200 毫秒时重传那个请求**。`close` 只是说「我不再发新数据了」，已经交给内核的数据会照常发完，最后才发 FIN。于是旧请求晚到了 100 多毫秒，还是被执行了。

所以「请求丢了就可以安全重试」也不成立：**一个请求一旦发出，就不能指望撤回它**。它可能在任何时候到达，哪怕客户端已经放弃了。

## 5. 幂等：让重复执行没有副作用

既然无法避免重复发送，就让**重复执行和执行一次的效果一样**，这叫**幂等**（idempotent）。

有些操作天然幂等：`PUT k 5` 执行两次，结果还是 5；`GET` 执行多少次都一样。`INCR`、「扣款 10 元」、「追加一条消息」不是。

让不幂等的操作变成幂等的标准做法，是给每个请求一个**唯一的编号**，服务器记住执行过的编号：

- 每个客户端有一个编号 `cid`，它的每个请求有一个递增的序号 `seq`，重试时序号不变；
- 服务器为每个客户端记住最后执行的序号和结果；收到的序号不大于记录的，说明是重复的，直接返回记录的结果，不再执行。

```c
} else if (dedup && n == 3 && dd[cid % NCLI].cid == cid && seq <= dd[cid % NCLI].seq) {
    st.dupHits++;                                  /* 重试：这个请求已经执行过 */
    snprintf(out, sizeof out, "OK %ld\n", dd[cid % NCLI].result);
} else {
    ...                                            /* 执行，然后记下 seq 和结果 */
    if (dedup && n == 3) { dd[cid % NCLI].cid = cid; dd[cid % NCLI].seq = seq; dd[cid % NCLI].result = val; }
}
```

请求变成 `INCR k cid seq`。同样丢 5% 的回复：

```
$ cli incr 10.77.0.11:7000 2000 100 1 2
incr: n=2000 mean=9.459 p50=1.391 p90=1.422 p99=107.723 p99.9=1162.861 max=1271.245 ms
increments 2000, retries 115, 18.9 s, counter = 2000, extra = 0
```

服务器的统计中 `dup=115`：**115 次重试全部被识别出来**，计数器正好是 2000。

<figure class="diagram">
<svg viewBox="0 0 700 290" role="img" aria-label="带编号的请求如何去重：客户端 7 的第 42 个请求 INCR，服务器执行后计数器变成 5，并记下「客户端 7 最后执行到序号 42，结果是 5」；回复丢了，客户端超时后用同一个序号重试；服务器发现序号 42 已经执行过，直接返回记录的 5，不再加一">
  <defs><marker id="dedup-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="dedup-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="120.0" y="22.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">客户端 7</text><text x="470.0" y="22.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">服务器</text><line x1="120.0" y1="32.0" x2="120.0" y2="270.0" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="470.0" y1="32.0" x2="470.0" y2="270.0" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="120" y1="50" x2="468" y2="78" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#dedup-a)"/><text x="294.0" y="56.0" text-anchor="middle" style="fill:var(--link);font-size:11px">INCR k 7 42</text><rect x="484" y="64" width="200" height="46" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="584.0" y="83.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">执行：k = 5</text><text x="584.0" y="99.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">记下：7 → (42, 5)</text><line x1="470" y1="100" x2="290" y2="124" style="stroke:var(--fg-2);stroke-width:1.5"/><path d="M276,122 L288,134 M288,122 L276,134" style="stroke:var(--danger);stroke-width:2.2"/><text x="300.0" y="150.0" text-anchor="start" style="fill:var(--danger);font-size:11px">回复丢了</text><line x1="106" y1="160" x2="134" y2="160" style="stroke:var(--danger);stroke-width:2"/><text x="100.0" y="164.0" text-anchor="end" style="fill:var(--danger);font-size:11px">超时</text><line x1="120" y1="176" x2="468" y2="204" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#dedup-a)"/><text x="294.0" y="182.0" text-anchor="middle" style="fill:var(--link);font-size:11px">重试：INCR k 7 42（序号不变）</text><rect x="484" y="190" width="200" height="46" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="584.0" y="209.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">42 ≤ 42：已经执行过</text><text x="584.0" y="225.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">不再加一，返回 5</text><line x1="468" y1="236" x2="122" y2="260" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#dedup-a)"/><text x="295.0" y="240.0" text-anchor="middle" style="fill:var(--link);font-size:11px">OK 5</text>
  </g>
</svg>
</figure>

这就是「**至少一次** + 去重 = **恰好一次的效果**」。消息队列（Kafka 的幂等生产者）、支付接口（Stripe 的 `Idempotency-Key` 请求头）、Raft 的客户端协议，用的都是这个方法。真正实现时还要注意：

- **去重表也要持久化和复制**。服务器重启或者换了主节点，去重表丢了，重试就又会重复执行。dkv 的去重表只在主节点的内存中，第 3 篇的复制并不复制它；
- 每个客户端只记最后一个序号，要求客户端**一次只有一个未完成的请求**（或者记住一个窗口）；
- 去重表会一直增长，要有过期策略，比如客户端长时间不活动就清除。

## 6. 超时设多少

超时太短，正常但稍慢的请求被当成失败，徒增重试和重复；太长，真的出了问题要等很久才发现。

- 根据实测的延迟分布来设，比如 p99 的几倍，而不是拍脑袋；本文 1% 丢包时 p99 已经是 205 毫秒，100 毫秒的超时会让大约 1% 的请求被重试；
- 重试要**指数退避**并加**随机抖动**：服务器过载变慢时，所有客户端同时重试，会把它彻底压垮（重试风暴）；
- 只对幂等的操作自动重试，或者先让操作幂等，再重试。

## 7. 常见错误

1. **以为丢包只是让请求慢一点**。1% 的丢包几乎不影响中位数，却让 p99 变成 200 毫秒。
2. **超时就当作失败**。超时的意思是「不知道」，请求可能已经执行了。
3. **以为关掉连接就撤回了请求**。实测中丢的是请求，重试照样造成了重复：旧请求被 TCP 重传，晚到了。
4. **对不幂等的操作盲目重试**。2000 次加一多加了 94 次。
5. **去重表只放在内存中**。重启或主节点切换后去重失效。

## 8. 小结

- 分布式系统的根本难题是部分失败：一部分坏了，另一部分不知道；
- 1% 的丢包让 p99 从 1.5 毫秒变成 205 毫秒，这是 TCP 最小 200 毫秒的重传超时；连丢两次 623 毫秒；
- 超时之后无法区分「请求丢了」「还在处理」「回复丢了」；
- 超时重试的加一操作，2000 次多加了 94 次；丢请求时也多加了 96 次，因为关掉的连接上请求还会被重传；
- 客户端编号 + 序号去重，115 次重试全部识别，计数器正好 2000。

## 9. 练习

1. 丢包率 5%，一个请求和它的回复各是一个包，不考虑重连。一次请求遇到至少一次丢包的概率是多少？和实测中超过 100 毫秒的比例 236 / 5000 对比。
2. 去重表为每个客户端只记最后一个序号。如果客户端同时发出序号 7 和 8 两个请求，8 先到达并执行，7 后到达，会发生什么？
3. `PUT k v` 是幂等的，那么「超时就重试 PUT」总是安全的吗？考虑另一个客户端在中间也写了 k。

<details>
<summary>点击查看答案</summary>

1. 请求和回复都不丢的概率是 0.95 × 0.95 = 0.9025，至少丢一个的概率约 9.75%。实测只有 4.7%，因为实验中只在 n1 上加了丢包，丢的只有回复，概率就是 5%，和 4.7% 相符（少数丢掉的包是纯 ACK，不影响延迟）。

2. 服务器执行 8 之后记录 seq = 8；7 到达时 7 ≤ 8，被当成重复，返回 8 的结果，7 实际上没有执行。所以「只记最后一个序号」要求每个客户端一次只有一个未完成的请求。要支持并发，就要记住一个窗口内哪些序号执行过（类似 TCP 的接收窗口）。

3. 不总是安全。客户端 A 发出 PUT k 1 后超时；客户端 B 写入 PUT k 2 并收到确认；A 的旧请求晚到，或者 A 的重试到达，把 k 又改回 1。B 的写入被覆盖了，而在 B 看来自己的写入已经成功。单独看 PUT 是幂等的，但它和其他操作交错时，重复执行改变了顺序。解决方法同样是给请求编号或者带上版本号（只有版本匹配才写入，类似比较并交换）。

</details>
