---
title: 缓冲区与吞吐量：自动调优和它的陷阱
date: 2026-11-11 10:20:00
updated: 2026-11-11 10:20:00
categories:
  - 计算机网络
  - 网络排障
tags:
  - 网络
  - TCP
order: 3
description: 在一条 500 Mbit/s、往返 56 毫秒的链路上传 200 MB，什么都不设置，内核把接收缓冲区从 128 KB 自动调到 6 MB，跑出 400 Mbit/s。「为了更快」把 SO_RCVBUF 设成 1 MB，结果只有 35.6 Mbit/s，慢了 11 倍：设置了 SO_RCVBUF 就关掉了自动调优，而 1 MB 又被 rmem_max 截成了 416 KB。本文用窗口 ÷ 往返时间这个公式解释每一个结果，用 ss 看清缓冲区和窗口，再看发送缓冲区设小了会怎样，以及什么时候真的需要手动设置。
top_group_list: true
disableNunjucks: true
viz: [net-diag]
---

> **本文目标**：理解带宽时延积，会用「吞吐量 ≤ 窗口 ÷ 往返时间」估算一条连接的速度上限；理解接收缓冲区与接收窗口的关系；知道 Linux 的缓冲区自动调优（`tcp_rmem`、`tcp_wmem`），以及设置 `SO_RCVBUF`、`SO_SNDBUF` 会关掉它；会用 `ss -tim` 查看缓冲区、窗口和在途数据。
>
> **前置知识**：《流量控制与拥塞控制》（接收窗口、拥塞窗口、带宽时延积的概念）；《TCP 连接管理》（窗口扩大选项 `wscale`）；本章第 1 篇（`rmem_max`）。
>
> **实验环境**：同第 1 篇。在路由器 r 的两个出口上各加 25 毫秒延迟、限速 500 Mbit/s：`tc qdisc add dev r-s root netem delay 25ms rate 500mbit limit 10000`（r-c 同样），`ping` 测得往返时间 54～58 毫秒，平均 56 毫秒。拥塞控制算法是 CUBIC。

## 1. 窗口 ÷ 往返时间

TCP 的发送方最多只能有「一个窗口」的数据在途（发出了、还没收到确认），窗口取接收窗口和拥塞窗口中较小的那个。发出一个窗口的数据以后，要等一个往返时间，确认回来了才能继续发。所以一条连接的速度有一个上限：

**吞吐量 ≤ 窗口 ÷ 往返时间**

反过来，要跑满一条带宽为 B、往返时间为 RTT 的路径，窗口至少要有 **B × RTT**，这叫带宽时延积（BDP）。这条实验链路：500 Mbit/s × 0.056 秒 = 28 Mbit ≈ **3.5 MB**。

<div class="dsviz" data-viz="nd-bdp"></div>

接收窗口由接收方的**接收缓冲区**决定：接收方告诉发送方「我还能收多少」，最多是缓冲区中还空着的空间（还要扣掉一部分内核的开销）。所以接收缓冲区小于 BDP，连接就跑不满。

## 2. 什么都不设置：自动调优

`bulk sink` 在 s 上接收并丢弃数据，`bulk send` 在 c 上尽快发送 200 MB：

```
收到 200.0 MB，4.19 秒，400.2 Mbit/s；SO_RCVBUF 开始 131072，结束 6291456
```

400 Mbit/s，是带宽的 80%。注意接收缓冲区：连接刚建立时是 131072 字节（128 KB），结束时变成了 6291456 字节（6 MB）。这是 Linux 的**接收缓冲区自动调优**：内核测量每个往返时间里应用读走了多少数据，据此不断加大缓冲区，上限是 `tcp_rmem` 的第三个数：

```
net.ipv4.tcp_rmem = 4096	131072	6291456    # 最小、初始、自动调优的上限
net.ipv4.tcp_wmem = 4096	16384	4194304    # 发送缓冲区，同样会自动调大
```

传输进行到第 2 秒时，用 `ss -tim` 看接收方：

```
skmem:(r2408,rb6291456,...) cubic wscale:7,7 rtt:54.295/27.147 ... rcv_rtt:50 rcv_space:3149400 rcv_ssthresh:3759377
```

`rb6291456` 是当前的接收缓冲区，已经到了上限；`rcv_space` 是内核估计的「每个往返时间需要接收的数据量」，约 3 MB，正好接近 BDP。发送方：

```
rtt:60.556/0.382 cwnd:2721 send 521Mbps delivery_rate 449Mbps unacked:2507
```

拥塞窗口 2721 段，在途 2507 段 × 1448 字节 ≈ 3.6 MB，和 BDP 一致；`delivery_rate`（实际的送达速度）449 Mbit/s。

**默认的设置在这条链路上已经很好了**，不需要任何调整。

## 3. 陷阱：设置了 SO_RCVBUF

很多网络程序、调优文章会建议「把套接字缓冲区设大一点」。试试在接收方把 `SO_RCVBUF` 设成 1 MB（对监听套接字设置，accept 得到的连接会继承）：

```c
int rcvbuf = 1048576;
setsockopt(lfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);
```

```
收到 200.0 MB，47.11 秒，35.6 Mbit/s；SO_RCVBUF 开始 425984，结束 425984
```

**比什么都不设置慢了 11 倍。**两个原因叠在了一起：

1. 和第 1 篇的 UDP 一样，`SO_RCVBUF` 受 `net.core.rmem_max`（212992）限制，再乘以 2，实际只有 425984 字节，而不是 1 MB；
2. **设置了 `SO_RCVBUF`，这个套接字的自动调优就被关掉了**：缓冲区从头到尾固定在 425984 字节，再也不会长到 6 MB。

用公式验证：35.6 Mbit/s × 0.056 秒 ≈ 249 KB，这是实际的窗口，约为缓冲区的 58%（其余留给内核管理数据的开销）。

几种设置的对比：

| 接收方的设置 | 实际的接收缓冲区 | 吞吐量 |
|---|---|---|
| 什么都不设置（自动调优） | 128 KB → 6 MB | 400.2 Mbit/s |
| `SO_RCVBUF` = 64 KB | 128 KB（固定） | 11.0 Mbit/s |
| `SO_RCVBUF` = 1 MB | 416 KB（固定，被 `rmem_max` 截断） | 35.6 Mbit/s |
| `tcp_rmem` 的上限改为 1 MB | 128 KB → 1 MB | 88.7 Mbit/s |
| `rmem_max` 调到 16 MB，`SO_RCVBUF` = 8 MB | 16 MB（固定） | 401.2 Mbit/s |

<figure class="diagram">
<svg viewBox="0 0 700 230" role="img" aria-label="接收方不同设置下的吞吐量（500 Mbit/s、往返 56 毫秒）：自动调优 400 Mbit/s；SO_RCVBUF 设为 1 MB 实际只有 416 KB 且不再自动调整，只有 35.6 Mbit/s；64 KB 只有 11 Mbit/s">
  <defs><marker id="rcv-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="rcv-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="200.0" y="35.0" text-anchor="end" style="fill:var(--fg);font-size:12px">自动调优（默认）</text><rect x="210.0" y="20.0" width="240.1" height="20.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="458.1" y="35.0" text-anchor="start" style="fill:var(--muted);font-size:11px">400.2 Mbit/s（128 KB → 6 MB）</text><text x="200.0" y="69.0" text-anchor="end" style="fill:var(--fg);font-size:12px">rmem_max 16M + SO_RCVBUF 8M</text><rect x="210.0" y="54.0" width="240.7" height="20.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="458.7" y="69.0" text-anchor="start" style="fill:var(--muted);font-size:11px">401.2 Mbit/s（16 MB 固定）</text><text x="200.0" y="103.0" text-anchor="end" style="fill:var(--fg);font-size:12px">tcp_rmem 上限 1 MB</text><rect x="210.0" y="88.0" width="53.2" height="20.0" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="271.2" y="103.0" text-anchor="start" style="fill:var(--muted);font-size:11px">88.7 Mbit/s（128 KB → 1 MB）</text><text x="200.0" y="137.0" text-anchor="end" style="fill:var(--fg);font-size:12px">SO_RCVBUF = 1 MB</text><rect x="210.0" y="122.0" width="21.4" height="20.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="239.4" y="137.0" text-anchor="start" style="fill:var(--muted);font-size:11px">35.6 Mbit/s（416 KB 固定）</text><text x="200.0" y="171.0" text-anchor="end" style="fill:var(--fg);font-size:12px">SO_RCVBUF = 64 KB</text><rect x="210.0" y="156.0" width="6.6" height="20.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="224.6" y="171.0" text-anchor="start" style="fill:var(--muted);font-size:11px">11.0 Mbit/s（128 KB 固定）</text><line x1="510.0" y1="14" x2="510.0" y2="190" style="stroke:var(--border-2);stroke-dasharray:4 4"/><text x="510.0" y="206.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">带宽 500</text><text x="20.0" y="224.0" text-anchor="start" style="fill:var(--muted);font-size:11px">吞吐量 ≈ 窗口 ÷ 往返时间，实测窗口约为接收缓冲区的 58%</text>
  </g>
</svg>
</figure>

每一行都可以用公式解释：固定的缓冲区 × 约 58% ÷ 56 毫秒。128 KB 只够 11 Mbit/s；1 MB 的自动调优上限只够 89 Mbit/s；要跑满 3.5 MB 的 BDP，缓冲区要 6 MB 左右。最后一行说明，手动设置不是不行，但要同时调大 `rmem_max`，还要设得比 BDP 大不少；可是 BDP 取决于对方在哪里（同机房 0.5 毫秒，跨国 200 毫秒），程序很难事先知道。

**结论：除非有明确的理由，不要设置 `SO_RCVBUF`、`SO_SNDBUF`，让内核自动调优。**要提高上限，改的是 `tcp_rmem`、`tcp_wmem` 的第三个数（以及 `rmem_max`、`wmem_max`，它们只限制手动设置的值）。

## 4. 发送缓冲区

发送方也一样：发出去的数据在确认之前不能丢掉，要留在发送缓冲区中以便重传，所以**在途的数据不能超过发送缓冲区**。把发送方的 `SO_SNDBUF` 设成 64 KB（实际 128 KB），接收方用默认设置：

```
发送 100 MB，42.34 秒，19.8 Mbit/s；SO_SNDBUF 131072
```

`ss` 显示 `skmem:(...,tb131072,...,w133952,...)`、`unacked:91`：发送缓冲区占满了，在途只有 91 段（约 130 KB）。130 KB ÷ 56 毫秒 ≈ 19 Mbit/s，和实测一致。重复传 20 MB 两次，是 21.7 和 21.1 Mbit/s。（另外有一次传 20 MB 得到了 137.6 Mbit/s，之后没能复现，原因没有查清，这里不采用。）

发送缓冲区还影响另一件事：`send` 只有在缓冲区有空间时才会成功（第 2 篇中 `send` 在断网后还成功了 948 次，就是因为数据都进了发送缓冲区）。缓冲区越大，程序越早「以为发完了」。

## 5. 什么时候需要手动调整

- **高带宽、长距离的链路**（跨洲的数据中心之间，10 Gbit/s × 150 毫秒 ≈ 190 MB）：默认的上限 6 MB 远远不够，要调大 `tcp_rmem`、`tcp_wmem` 的上限。这时还要注意拥塞控制算法，丢包对 CUBIC 的影响很大（《流量控制与拥塞控制》）；
- **大量的连接、内存紧张**：每个连接的缓冲区都可能自动长到几 MB，一万个连接就是几十 GB。可以调小上限，或者看 `net.ipv4.tcp_mem`（所有 TCP 连接加起来的内存上限）；
- **需要控制延迟的场景**：发送缓冲区太大，数据在本机排队，最新的数据要等前面的发完。可以用 `TCP_NOTSENT_LOWAT` 限制「还没发出去」的数据量，而不是缩小整个发送缓冲区。

排查「吞吐量上不去」时的顺序：

1. 用 `ping` 量往返时间，算出 BDP；
2. 用 `ss -tim` 看连接的 `rb`/`tb`（缓冲区）、`cwnd`、`unacked`、`rtt`、`delivery_rate`；
3. 在途数据（`unacked` × MSS）远小于 BDP：看是缓冲区（`rb`、`tb` 是不是固定在一个小值上）还是拥塞窗口（有没有丢包、重传）限制了它；
4. 用公式估算，对得上，就找到了原因。

## 6. 常见错误

1. **「为了更快」设置 `SO_RCVBUF`、`SO_SNDBUF`**。关掉了自动调优，还可能被 `rmem_max`、`wmem_max` 悄悄截断，结果更慢。
2. **设置之后不用 `getsockopt` 检查实际的值**。
3. **只看带宽，不看往返时间**。同样的缓冲区，同机房跑得满，跨国就跑不满。
4. **调大了 `rmem_max`，就以为自动调优的上限也变大了**。自动调优看的是 `tcp_rmem` 的第三个数。

## 7. 小结

- 吞吐量 ≤ 窗口 ÷ 往返时间；要跑满一条路径，窗口至少要有带宽时延积（这里约 3.5 MB）；
- Linux 自动调整接收和发送缓冲区，实测接收缓冲区从 128 KB 长到 6 MB，吞吐量 400 Mbit/s；
- 设置 `SO_RCVBUF` 会关掉自动调优，而且受 `rmem_max` 限制：请求 1 MB 实际 416 KB，吞吐量只有 35.6 Mbit/s，慢了 11 倍；
- 实测的窗口约为接收缓冲区的 58%；每一种设置的吞吐量都可以用公式算出来；
- 发送缓冲区限制在途的数据量，128 KB 时只有约 20 Mbit/s；
- 除非有明确的理由，让内核自动调优；需要更大的上限时改 `tcp_rmem`、`tcp_wmem`。

## 8. 练习

1. 两个数据中心之间 1 Gbit/s，往返 80 毫秒。用默认的 `tcp_rmem`（上限 6 MB），一条 TCP 连接最多能跑多快？要跑满带宽，上限至少要设成多少？
2. 一个文件传输程序在同机房（往返 0.3 毫秒）能跑满 1 Gbit/s，换到跨国的线路（往返 180 毫秒，带宽同样是 1 Gbit/s）只有 10 Mbit/s 左右。用 `ss` 看到接收方的 `rb` 一直是 425984。最可能的原因是什么？
3. 第 3 节中，把 `tcp_rmem` 的上限改为 1 MB 后实测 88.7 Mbit/s。用本文的公式验证这个结果。

<details>
<summary>点击查看答案</summary>

1. 带宽时延积 = 1 Gbit/s × 0.08 秒 = 80 Mbit = 10 MB。缓冲区上限 6 MB，按本文实测约 58% 用作窗口，窗口约 3.5 MB，吞吐量上限约 3.5 MB × 8 ÷ 0.08 秒 ≈ 350 Mbit/s。要跑满 1 Gbit/s，窗口要 10 MB，缓冲区上限至少要 10 ÷ 0.58 ≈ 17 MB，一般设成 32 MB 左右留出余量。

2. `rb` 固定在 425984 = 2 × 212992，说明程序设置了 `SO_RCVBUF`（被 `rmem_max` 截断），自动调优被关掉了。同机房 BDP 只有 1 Gbit/s × 0.0003 秒 ≈ 37 KB，416 KB 绰绰有余；跨国 BDP 约 22 MB，416 KB × 58% ≈ 240 KB 的窗口只能跑到 240 KB × 8 ÷ 0.18 秒 ≈ 11 Mbit/s，和现象一致。去掉程序中的 `SO_RCVBUF` 设置，并调大 `tcp_rmem` 的上限。

3. 自动调优把缓冲区调到了上限 1 MB（实测结束时 `SO_RCVBUF` 是 1048576）。窗口约为缓冲区的 58%，约 600 KB；吞吐量上限 ≈ 600 KB × 8 ÷ 0.056 秒 ≈ 86 Mbit/s，和实测的 88.7 Mbit/s 基本一致。

</details>
