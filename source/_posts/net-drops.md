---
title: 内核在哪里丢包
date: 2026-11-11 10:00:00
updated: 2026-11-11 10:00:00
categories:
  - 计算机网络
  - 网络排障
tags:
  - 网络
  - Linux
  - bpftrace
order: 1
description: 包在路上丢了，应用只会看到「慢」或者「没有回应」。包可能丢在接收方的套接字缓冲区、中间路由器的队列、防火墙，也可能丢在连接跟踪表里，每个地方都有自己的计数器。本文在实验网络中依次制造这四种丢包，看 nstat、tc、iptables、dmesg 各自记下了什么，再用 bpftrace 跟踪内核的 kfree_skb，让内核直接报告每个包为什么被丢、在哪个函数中被丢。实测：10 万个 UDP 数据报只收到 782 个；接收缓冲区要 8 MB，实际只给了 416 KB；连接跟踪表满了以后，新连接的 SYN 被悄悄丢弃，一个新连接都建不起来。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：知道一个包从发送到被程序读到要经过哪些可能丢包的地方；会用 `nstat`、`ip -s link`、`tc -s qdisc`、`iptables -v` 查看各处的丢包计数；会用 bpftrace 跟踪 `skb:kfree_skb`，按丢包原因和位置统计；理解接收缓冲区的上限 `rmem_max`、连接跟踪表的容量和超时。
>
> **前置知识**：《UDP》《TCP 的常见问题》（全连接队列与 backlog）；《网络排查工具箱》（排查的顺序）；《Linux 基础》专栏的《bpftrace：不改代码看清程序在做什么》。本章的完整代码在 [projects/net-diag](https://github.com/Lucakes-bw/Lucakes-bw.github.io/tree/main/projects/net-diag)。
>
> **实验环境**：Ubuntu 24.04 容器（以 `--privileged` 运行），内核 6.8，bpftrace 0.20.2。用三个网络命名空间搭成实验网络：客户端 c（10.0.1.2）— 路由器 r — 服务器 s（10.0.2.2），见代码目录中的 `scripts/lab.sh`；虚拟网卡关闭了分段卸载（《TCP 的可靠传输》3.2 节）。

## 1. 包会在哪里被丢

《网络排查工具箱》讲的是「连不上」：一层一层地确认哪里断了。还有一类问题更难查：大部分包都能到，但有一部分被丢了。TCP 会重传，应用看到的是变慢、偶尔超时；UDP 不重传，数据直接没了。而丢包的地方一般不会报错，只会在某个计数器上加一。

一个包从发送程序到接收程序，可能在这些地方被丢弃：

<figure class="diagram">
<svg viewBox="0 0 700 300" role="img" aria-label="一个包从发送程序到接收程序要经过的地方，每一处都可能丢包，并有各自的计数器：发送方的 qdisc；路由器上的防火墙、连接跟踪和出口队列；接收方的网卡、防火墙、监听队列和套接字接收缓冲区">
  <defs><marker id="pth-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="pth-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="20.0" y="22.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">发送方 c</text><text x="255.0" y="22.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">路由器 r</text><text x="490.0" y="22.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">接收方 s</text><rect x="20" y="34" width="190" height="38" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="115.0" y="57.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">程序 sendto / write</text><rect x="20" y="84" width="190" height="38" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="115.0" y="107.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">协议栈</text><line x1="115" y1="72" x2="115" y2="82" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#pth-a)"/><rect x="20" y="134" width="190" height="38" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="115.0" y="149.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">qdisc 队列</text><text x="115.0" y="164.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">tc -s qdisc</text><line x1="115" y1="122" x2="115" y2="132" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#pth-a)"/><rect x="255" y="34" width="190" height="38" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="350.0" y="49.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">网卡接收</text><text x="350.0" y="64.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">ip -s link</text><rect x="255" y="84" width="190" height="38" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="350.0" y="99.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">防火墙</text><text x="350.0" y="114.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">iptables -v</text><line x1="350" y1="72" x2="350" y2="82" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#pth-a)"/><rect x="255" y="134" width="190" height="38" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="350.0" y="149.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">连接跟踪</text><text x="350.0" y="164.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">dmesg</text><line x1="350" y1="122" x2="350" y2="132" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#pth-a)"/><rect x="255" y="184" width="190" height="38" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="350.0" y="199.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">出口 qdisc</text><text x="350.0" y="214.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">tc -s qdisc</text><line x1="350" y1="172" x2="350" y2="182" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#pth-a)"/><rect x="490" y="34" width="190" height="38" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="585.0" y="49.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">网卡接收</text><text x="585.0" y="64.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">ip -s link</text><rect x="490" y="84" width="190" height="38" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="585.0" y="99.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">防火墙、连接跟踪</text><text x="585.0" y="114.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">iptables -v</text><line x1="585" y1="72" x2="585" y2="82" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#pth-a)"/><rect x="490" y="134" width="190" height="38" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="585.0" y="149.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">监听队列</text><text x="585.0" y="164.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">nstat</text><line x1="585" y1="122" x2="585" y2="132" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#pth-a)"/><rect x="490" y="184" width="190" height="38" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="585.0" y="199.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">套接字接收缓冲区</text><text x="585.0" y="214.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">nstat</text><line x1="585" y1="172" x2="585" y2="182" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#pth-a)"/><rect x="490" y="234" width="190" height="38" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="585.0" y="257.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">程序 recv / read</text><line x1="585" y1="222" x2="585" y2="232" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#pth-a)"/><line x1="212" y1="155" x2="253" y2="53" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#pth-a)"/><line x1="447" y1="203" x2="488" y2="53" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#pth-a)"/><text x="350.0" y="290.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">黄色框：可能丢包的地方；小字：在哪里看计数。bpftrace 的 skb:kfree_skb 能看到所有这些地方的丢包</text>
  </g>
</svg>
</figure>

| 位置 | 原因 | 在哪里看 |
|---|---|---|
| 网卡和驱动 | 接收环形队列满了、校验错误 | `ip -s link`（`dropped`、`missed`）、`ethtool -S` |
| 流量控制队列（qdisc） | 发送得比链路快，队列满了 | `tc -s qdisc`（`dropped`） |
| 防火墙 | 规则丢弃 | `iptables -L -v -n`（每条规则的计数） |
| 连接跟踪 | 表满了 | `dmesg`（`nf_conntrack: table full`） |
| TCP 的监听队列 | 程序 accept 得太慢 | `nstat`（`TcpExtListenOverflows`） |
| 套接字接收缓冲区 | 程序读得太慢 | `nstat`（`UdpRcvbufErrors`、`TcpExtTCPRcvQDrop`） |

计数器分散在很多地方，而且不在发送方，也不一定在接收方，可能在中间的某台路由器上。内核 5.17 开始给丢包加上了**原因**：内核释放一个没能送达的包时调用 `kfree_skb`，同时记下一个 `enum skb_drop_reason`，并触发 `skb:kfree_skb` 跟踪点。用 bpftrace 统计这个跟踪点，就能在一台机器上看到所有的丢包，以及每一个的原因和位置：

```c
// drops.bt：按丢包原因和丢弃它的内核函数统计
tracepoint:skb:kfree_skb
/args.reason > 2/                    // 2 是 NOT_SPECIFIED（没有说明原因），不统计
{
    @drops[args.reason, ksym(args.location)] = count();
}
```

`args.reason` 是一个数字，名字在 `/sys/kernel/tracing/events/skb/kfree_skb/format` 中；内核 6.8 定义了 95 种。`args.location` 是调用 `kfree_skb` 的代码地址，`ksym` 把它换成函数名。

另一个常用的工具是 `nstat`：它显示 `/proc/net/snmp`、`/proc/net/netstat` 中的计数器**自上次运行以来的增量**，比 `netstat -s` 的累计值更容易看出变化。下面每个实验之前先运行一次 `nstat -n`（只记录，不显示），实验之后再运行 `nstat`。实验网络中的三台「机器」是三个网络命名空间，每个命名空间有自己的一套计数器，用 `ip netns exec 名字 命令` 在其中运行。

## 2. 接收缓冲区满了

`drops udprecv` 在 s 上接收 UDP，每收到一个数据报就停 100 微秒，模拟处理得比较慢；`drops udpsend` 在 c 上尽快发送 10 万个 100 字节的数据报：

```bash
ip netns exec s ./build/drops udprecv 9000 0 100 6      # 端口、接收缓冲区（0 表示默认）、每个处理多少微秒、运行几秒
ip netns exec c ./build/drops udpsend 10.0.2.2 9000 100000
```

```
接收缓冲区 212992 字节，每个数据报处理 100 微秒
发送了 100000 个数据报，用了 0.112 秒
收到 782 个数据报
```

10 万个只收到 782 个。发送方没有任何错误：UDP 的 `sendto` 返回成功，只说明数据报交给了本机的内核。s 上的计数器：

```
UdpInDatagrams                  782
UdpInErrors                     99218
UdpRcvbufErrors                 99218
```

bpftrace 的结果：

```
@drops[16, __udp_queue_rcv_skb]: 99218
```

16 是 `SOCKET_RCVBUFF`：数据报到达时，这个套接字的接收缓冲区已经满了，在 `__udp_queue_rcv_skb` 中被丢弃。99218 和 `UdpRcvbufErrors` 完全一致，c 和 r 上都没有丢包。

发送方 0.112 秒发完 10 万个，每秒近 90 万个；接收方每个要处理 100 多微秒，每秒最多几千个。默认的接收缓冲区 212992 字节，几百个数据报就装满了（缓冲区按每个数据报实际占用的内存计算，包括内核管理它的结构，远大于 100 字节）。

**加大接收缓冲区**。用 `SO_RCVBUF` 请求 8 MB：

```
接收缓冲区 425984 字节，每个数据报处理 100 微秒
...
收到 888 个数据报
UdpRcvbufErrors                 99112
```

要的是 8 MB，实际只有 425984 字节。`SO_RCVBUF` 的值被 `net.core.rmem_max`（这里是 212992）限制，内核再把它乘以 2（多出的一半留给内核的管理开销），**不报任何错误**。用 `getsockopt` 读回来才知道。

把 `rmem_max` 调到 32 MB（`sysctl -w net.core.rmem_max=33554432`，实验后改回）再试：

```
接收缓冲区 16777216 字节，每个数据报处理 100 微秒
...
收到 17580 个数据报
UdpRcvbufErrors                 82420
```

缓冲区真的有 16 MB 了，收到的多了 20 倍，但仍然丢了 82%。**缓冲区只能吸收突发，不能弥补速度的差距**：发送方持续比接收方快，再大的缓冲区也会满。真正的解决办法是让接收方处理得更快（多线程、`recvmmsg` 批量接收、`SO_REUSEPORT` 多个套接字分担），或者让发送方控制速度。

## 3. 路由器的队列满了

去掉接收方的处理延迟，改为在路由器 r 发往 s 的出口上限速 10 Mbit/s：

```bash
ip netns exec r tc qdisc add dev r-s root tbf rate 10mbit burst 32kbit limit 30000
```

`tbf`（令牌桶）按 10 Mbit/s 放行，来不及发的包在队列中排队，队列最多 30000 字节，超过就丢。同样发送 10 万个：

```
收到 1178 个数据报
```

这次 s 上的 `UdpRcvbufErrors` 是 0：**接收方什么也没丢，它根本没收到**。bpftrace（在虚拟机上运行，三个命名空间中的丢包都能看到）：

```
@drops[52, __dev_xmit_skb]: 98822
```

52 是 `QDISC_DROP`，在 `__dev_xmit_skb`（把包交给网卡的发送队列）中被丢弃。在 r 上看队列的统计：

```
$ tc -s qdisc show dev r-s
qdisc tbf 8009: root refcnt 3 rate 10Mbit burst 4Kb lat 20.7ms
 Sent 167276 bytes 1178 pkt (dropped 98822, overlimits 102264 requeues 0)
```

发出 1178 个，丢弃 98822 个，加起来正好 10 万。真实的网络中，这就是瓶颈链路上的路由器或交换机：入口快、出口慢，队列一满就丢。TCP 会把这种丢包当作拥塞的信号，降低发送速度（《流量控制与拥塞控制》）；UDP 的程序要自己控制发送速度。

**排查时要沿着路径一跳一跳地看**。只看两端，会得出「包在网络里消失了」的结论。

## 4. 防火墙

在 r 上丢弃发往 8080 端口的 TCP 包，然后从 c 连接：

```bash
ip netns exec r iptables -A FORWARD -p tcp --dport 8080 -j DROP
ip netns exec c timeout 5 ./build/dead client 10.0.2.2 8080 idle
```

客户端 5 秒内一直卡在 `connect` 中，没有任何输出。bpftrace：

```
@drops[8, nft_do_chain]: 5
```

8 是 `NETFILTER_DROP`，在 `nft_do_chain` 中被丢弃（Ubuntu 24.04 的 `iptables` 命令底层用的是 nftables）。5 秒内丢了 5 个包，都是客户端重发的 SYN。规则的计数器也记下了：

```
$ ip netns exec r iptables -L FORWARD -v -n
 pkts bytes target     prot opt in     out     source               destination
    5   300 DROP       6    --  *      *       0.0.0.0/0            0.0.0.0/0            tcp dpt:8080
```

`DROP` 和 `REJECT` 的区别在这里很明显：`REJECT` 会回复一个 RST 或 ICMP，客户端立刻知道「连接被拒绝」；`DROP` 什么也不回，客户端只能一次次重发，直到超时。

## 5. 连接跟踪表满了

这是生产环境中很常见、又很难一眼看出来的一种丢包。只要用到了 NAT，或者防火墙规则中有 `-m conntrack`（`--ctstate ESTABLISHED` 这种「放行已建立的连接」的写法到处都是），内核就会为每一条经过的连接在**连接跟踪表**中记一条。表的大小有上限，满了以后，新连接的包会被丢弃。

在 r 上加一条用到连接跟踪的规则，把表的上限调小（实验后改回 65536）：

```bash
ip netns exec r iptables -A FORWARD -m conntrack --ctstate INVALID -j DROP
sysctl -w net.netfilter.nf_conntrack_max=1000      # 这个参数是全局的，要在宿主（虚拟机）的命名空间中设置
```

先把上限设为 1000，用 `churn client` 快速建立并关闭 990 个连接（每个连接发 1 字节、收 1 字节，第 4 篇会详细用到这个程序）：

```
短连接：成功 990 次，0.05 秒，每秒 21005 次
net.netfilter.nf_conntrack_count = 990
```

连接都已经关闭了，表中却还有 990 条：连接关闭以后，它在表中的记录还要按 TIME_WAIT 状态保留 `nf_conntrack_tcp_timeout_time_wait` = **120 秒**。这时把上限降到 900，再建一个新连接，8 秒内一直卡在 `connect` 中。`dmesg`：

```
nf_conntrack: nf_conntrack: table full, dropping packet
```

8 秒内出现了 7 条，每条对应客户端发出（或重发）的一个 SYN。bpftrace 看到的是：

```
@drops[8, nf_hook_slow]: 7
```

仍然是 `NETFILTER_DROP`，但位置是 `nf_hook_slow`，而不是上一节的规则链：包没有被任何规则丢弃，而是在连接跟踪这个环节就被拒绝了。`iptables` 的规则计数器中**看不到**这些丢包，只有 `dmesg` 和 drop reason 能说明问题。

算一笔账：表的默认上限是 65536，每个关闭的连接还要占 120 秒，所以**持续每秒超过 65536 / 120 ≈ 546 个新连接，表就会满**。一台做 NAT 的网关、一个 Kubernetes 节点、一个每个请求都新建连接的 HTTP 客户端，都很容易超过这个数。现象是：大部分连接正常，新连接间歇性地超时，CPU、内存、带宽都不高。解决办法是加大 `nf_conntrack_max`（每条记录约几百字节内存），缩短超时，或者对不需要跟踪的流量用 `-t raw -j NOTRACK` 跳过，最根本的是用连接池减少新建连接（第 4 篇）。

## 6. 不是每个 kfree_skb 都是丢包

用 bpftrace 跟踪时还会看到一些和网络问题无关的原因。比如做第 5 节的实验时，同时出现了：

```
@drops[82, sk_stream_kill_queues]: 787
@drops[37, tcp_rcv_state_process]: 2
```

82 是 `QUEUE_PURGE`：关闭套接字时，内核清掉它的队列中还没用完的包；37 是 `TCP_CLOSE`：连接已经关闭后又收到的包。它们是连接正常关闭时的副产品。统计时要关注的是和现象对得上的原因，以及数量在随问题一起增长的那些。

另外，正常处理完的包是用 `consume_skb` 释放的，不会触发 `kfree_skb`；早期的内核没有丢包原因，只能根据 `location` 判断，看到的原因都是 `NOT_SPECIFIED`。

## 7. 常见错误

1. **只在两端找丢包**。中间的路由器队列、防火墙、连接跟踪都会丢，两端的计数器都是 0。
2. **以为 UDP 的 `sendto` 成功就是发出去了**。它只说明交给了本机内核，之后的任何一处都可能丢。
3. **设置了 `SO_RCVBUF` 不检查实际的值**。超过 `rmem_max` 会被悄悄截断。
4. **指望加大缓冲区解决持续的速度差**。缓冲区只能吸收突发。
5. **防火墙规则用了 conntrack，却不关心连接跟踪表的容量**。表满时新连接被丢弃，规则计数器上看不出来。
6. **把 bpftrace 统计到的每个 kfree_skb 都当成问题**。`QUEUE_PURGE`、`TCP_CLOSE` 等是正常关闭的副产品。

## 8. 小结

- 丢包可能发生在网卡、qdisc、防火墙、连接跟踪、监听队列、套接字缓冲区，每处有自己的计数器：`ip -s link`、`tc -s qdisc`、`iptables -v`、`dmesg`、`nstat`；
- 内核 5.17 起，`skb:kfree_skb` 跟踪点带有丢包原因，bpftrace 可以按原因和位置统计所有的丢包；
- 实测：10 万个 UDP 数据报，接收方处理慢时只收到 782 个（`SOCKET_RCVBUFF`）；路由器出口限速时只到达 1178 个（`QDISC_DROP`）；
- `SO_RCVBUF` 被 `rmem_max` 截断，8 MB 只给了 416 KB；加到 16 MB 仍丢 82%，缓冲区不能弥补速度差；
- 连接跟踪表满了，新连接的 SYN 被悄悄丢弃；关闭的连接还要占用 120 秒，默认上限下每秒超过约 546 个新连接就会满。

## 9. 练习

1. 一个 UDP 服务偶尔丢数据。在服务器上 `nstat` 看到 `UdpRcvbufErrors` 在增长，`ip -s link` 的 `dropped` 不变。问题在哪里？有哪些解决办法？
2. 一台 NAT 网关上，`dmesg` 中不断出现 `nf_conntrack: table full`。`nf_conntrack_max` 是 262144，大部分连接都是短连接。估算网关每秒最多能支持多少个新连接？除了加大上限，还有什么办法？
3. 为什么第 3 节限速以后，发送方仍然 0.1 秒就「发完」了 10 万个数据报？

<details>
<summary>点击查看答案</summary>

1. 包已经到达了这台机器，网卡和驱动没有丢（`dropped` 不变），是在交给套接字时因为接收缓冲区满了被丢弃的：程序读得不够快，或者有突发。办法：检查程序为什么慢（第 1 篇的方法、是否在接收线程中做了耗时的处理）；加大 `SO_RCVBUF`，同时调高 `net.core.rmem_max`，并用 `getsockopt` 确认实际的值；用多个线程或 `SO_REUSEPORT` 多个套接字分担接收；用 `recvmmsg` 一次接收多个数据报，减少系统调用。

2. 每个关闭的短连接在表中还要保留约 120 秒（TIME_WAIT 的超时），所以持续的新建速度上限约为 262144 / 120 ≈ 2185 个每秒（还没算正在使用的连接）。其他办法：缩短 `nf_conntrack_tcp_timeout_time_wait` 等超时；对不需要 NAT 和状态跟踪的流量在 raw 表中 `NOTRACK`；让客户端用长连接、连接池，减少新建的连接数。

3. `sendto` 只是把数据报交给本机内核。c 发往 r 的虚拟网卡没有限速，数据报很快就离开了 c；限速和排队发生在 r 的出口，r 的队列满了就丢弃，这些都和 c 无关，c 也收不到任何通知。

</details>
