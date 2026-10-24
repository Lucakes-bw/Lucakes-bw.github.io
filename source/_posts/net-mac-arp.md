---
title: 数据链路层：MAC 与 ARP
date: 2026-10-24 15:00:00
updated: 2026-10-24 15:00:00
categories:
  - 计算机网络
  - 网络模型
tags:
  - 计算机网络
  - 以太网
  - ARP
order: 2
description: 同一个网络中的主机怎样找到彼此？本文用 Linux 网络命名空间和网桥搭一个有 3 台主机和 1 台交换机的局域网，抓包看 ARP 怎样把 IP 地址解析成 MAC 地址；对比交换机和集线器，第三台主机分别能收到 1 个和 6 个包；配有交换机学习 MAC 地址的逐步演示；最后实测 MTU：1472 字节的 ping 能发出去，1473 字节就报错，3000 字节的包被切成了 3 片。
top_group_list: true
disableNunjucks: true
viz: [net-switch]
---

> **本文目标**：理解 MAC 地址和以太网帧；理解交换机怎样学习 MAC 地址并转发帧，以及它和集线器的区别；理解 ARP 怎样把 IP 地址解析成 MAC 地址；了解 MTU 和 IP 分片。学会用网络命名空间搭建实验网络，本专栏后面会一直使用这种方法。
>
> **前置知识**：上一篇的分层和封装；《进程与系统监控》中的 `/proc`。
>
> **实验环境**：Ubuntu 24.04 容器，iproute2 6.1，tcpdump 4.99。

## 1. MAC 地址

**MAC 地址**是网卡的硬件地址，48 位，写成 6 个十六进制字节，如 `aa:b0:81:06:e4:db`。它的用途是在**同一个网络**（同一个链路）中标识一个网卡：

- 前 3 个字节是厂商编号（OUI），由 IEEE 分配，后 3 个字节由厂商自己分配，所以出厂的 MAC 地址在全世界是唯一的；
- 第一个字节的倒数第二位如果是 1，表示这是**本地管理**的地址（不是出厂的），虚拟网卡通常使用这种地址。上一篇的 tshark 输出中有一行 `LG bit: Locally administered address`，说的就是这一位；
- `ff:ff:ff:ff:ff:ff` 是**广播地址**，发往它的帧，网络中的每台主机都会收下。

上一篇讲过，MAC 地址只在一个网络内有效，经过路由器就会更换。那么同一个网络中的主机之间，帧是怎样送到的？

## 2. 搭一个实验网络

Linux 的**网络命名空间**（network namespace）可以让一组进程拥有独立的网卡、IP 地址、路由表和 ARP 表，就像一台独立的主机；**veth** 是成对的虚拟网卡，从一端发进去的帧会从另一端出来，就像一根网线；**网桥**（bridge）是内核中的虚拟交换机。用它们搭一个有 3 台主机和 1 台交换机的局域网：

```bash
#!/bin/bash
# lan.sh：用网络命名空间模拟 3 台主机，用一个 Linux 网桥模拟交换机
set -e
ip link add br0 type bridge
ip link set br0 up
for i in 1 2 3; do
    ip netns add h$i
    ip link add veth$i type veth peer name eth0 netns h$i   # 一根「网线」：一端在主机里，一端插在交换机上
    ip link set veth$i master br0
    ip link set veth$i up
    ip netns exec h$i ip link set eth0 address 02:00:00:00:00:0$i   # 固定的 MAC，便于阅读
    ip netns exec h$i ip addr add 10.0.0.$i/24 dev eth0
    ip netns exec h$i ip link set eth0 up
    ip netns exec h$i ip link set lo up
done
```

```
            ┌──────────── br0（交换机） ────────────┐
          veth1                veth2                veth3
            │                    │                    │
       h1 eth0              h2 eth0              h3 eth0
   10.0.0.1             10.0.0.2             10.0.0.3
   02:00:00:00:00:01    02:00:00:00:00:02    02:00:00:00:00:03
```

`ip netns exec h1 命令` 在主机 h1 中执行命令。三台主机的 MAC 地址都以 `02` 开头，也就是本地管理的地址。（这些操作需要 `NET_ADMIN` 等权限，所以实验容器启动时加上了它们，网络命名空间只存在于容器中。）

## 3. ARP：从 IP 地址找到 MAC 地址

h1 要 ping 10.0.0.2。IP 层知道目的 IP 地址，但以太网帧需要目的 **MAC** 地址，h1 并不知道它。这时就要用 **ARP**（Address Resolution Protocol，地址解析协议）。

一开始 h1 的 ARP 表（也叫邻居表）是空的。在 h2 和 h3 上同时抓包（只抓 ARP 和 ICMP），然后让 h1 ping h2 两次：

```bash
ip netns exec h1 ip neigh                # 空
ip netns exec h2 tcpdump -i eth0 -nn -e "arp or icmp" &
ip netns exec h3 tcpdump -i eth0 -nn -e "arp or icmp" &
ip netns exec h1 ping -c 2 10.0.0.2
```

h2 抓到的：

```
07:06:41.356373 02:00:00:00:00:01 > ff:ff:ff:ff:ff:ff, ethertype ARP (0x0806), length 42: Request who-has 10.0.0.2 tell 10.0.0.1, length 28
07:06:41.356390 02:00:00:00:00:02 > 02:00:00:00:00:01, ethertype ARP (0x0806), length 42: Reply 10.0.0.2 is-at 02:00:00:00:00:02, length 28
07:06:41.356392 02:00:00:00:00:01 > 02:00:00:00:00:02, ethertype IPv4 (0x0800), length 98: 10.0.0.1 > 10.0.0.2: ICMP echo request, id 269, seq 1, length 64
07:06:41.356395 02:00:00:00:00:02 > 02:00:00:00:00:01, ethertype IPv4 (0x0800), length 98: 10.0.0.2 > 10.0.0.1: ICMP echo reply, id 269, seq 1, length 64
07:06:42.392124 02:00:00:00:00:01 > 02:00:00:00:00:02, ethertype IPv4 (0x0800), length 98: 10.0.0.1 > 10.0.0.2: ICMP echo request, id 269, seq 2, length 64
07:06:42.392147 02:00:00:00:00:02 > 02:00:00:00:00:01, ethertype IPv4 (0x0800), length 98: 10.0.0.2 > 10.0.0.1: ICMP echo reply, id 269, seq 2, length 64
```

1. h1 发出 **ARP 请求**：「谁是 10.0.0.2？请告诉 10.0.0.1」。目的 MAC 是广播地址 `ff:ff:ff:ff:ff:ff`，以太网的类型是 `0x0806`（ARP）；
2. h2 发现问的是自己，回复 **ARP 应答**：「10.0.0.2 在 02:00:00:00:00:02」。应答是**单播**，直接发给 h1；
3. h1 知道了 MAC 地址，发出 ping；第二次 ping 时不再需要 ARP。

tshark 解析出的 ARP 请求和应答（都是 28 字节，加上 14 字节以太网头共 42 字节）：

```
Address Resolution Protocol (request)
    Hardware type: Ethernet (1)
    Protocol type: IPv4 (0x0800)
    Hardware size: 6
    Protocol size: 4
    Opcode: request (1)
    Sender MAC address: 02:00:00:00:00:01 (02:00:00:00:00:01)
    Sender IP address: 10.0.0.1
    Target MAC address: 00:00:00_00:00:00 (00:00:00:00:00:00)
    Target IP address: 10.0.0.2

Address Resolution Protocol (reply)
    ...
    Opcode: reply (2)
    Sender MAC address: 02:00:00:00:00:02 (02:00:00:00:00:02)
    Sender IP address: 10.0.0.2
    Target MAC address: 02:00:00:00:00:01 (02:00:00:00:00:01)
    Target IP address: 10.0.0.1
```

请求中的目标 MAC 是全 0，因为这正是要问的；应答把发送方和目标对调，填上了答案。

### 3.1 ARP 表

```bash
ip netns exec h1 ip neigh
ip netns exec h2 ip neigh
```

```
10.0.0.2 dev eth0 lladdr 02:00:00:00:00:02 REACHABLE
10.0.0.1 dev eth0 lladdr 02:00:00:00:00:01 REACHABLE
```

h1 记住了 h2 的 MAC 地址。有意思的是 **h2 也记住了 h1 的**：ARP 请求中带着发送方的 IP 和 MAC，h2 顺便记了下来，因为它马上就要回复 h1。

这些记录不会永久保存。50 秒后再看 h1 的表：

```
10.0.0.2 dev eth0 lladdr 02:00:00:00:00:02 STALE
```

状态从 `REACHABLE`（可达）变成了 `STALE`（过期）。`/proc/sys/net/ipv4/neigh/eth0/base_reachable_time_ms` 是 30000，即一条记录在大约 30 秒（实际是 15～45 秒之间的随机值）内没有得到确认就会过期。过期的记录仍然可以使用，但下次使用时内核会重新确认对方是否还在。网络中的设备会更换（比如 IP 地址被分配给了另一台机器），ARP 表必须能够更新。

> **ARP 欺骗**：ARP 没有任何验证，任何主机都可以发出「10.0.0.254（网关）在我这里」的应答，让其他主机把发往网关的流量都交给它。这是局域网中常见的攻击方式，公共 Wi-Fi 中要格外小心，使用 HTTPS 可以防止内容被窃听或篡改（第 3.3 篇）。

## 4. 交换机

### 4.1 h3 抓到了什么

再看同一时间 h3 抓到的包：

```
07:06:41.356372 02:00:00:00:00:01 > ff:ff:ff:ff:ff:ff, ethertype ARP (0x0806), length 42: Request who-has 10.0.0.2 tell 10.0.0.1, length 28
```

只有一个包：广播的 ARP 请求。h1 和 h2 之间的 ARP 应答和 4 个 ping 包，h3 都没有收到。这是因为**交换机**会学习每个 MAC 地址在哪个端口，单播帧只发往目的主机所在的端口：

```bash
bridge fdb show br br0 | grep -v permanent
```

```
02:00:00:00:00:01 dev veth1 master br0
02:00:00:00:00:02 dev veth2 master br0
02:00:00:00:00:03 dev veth3 master br0
```

这就是交换机的 **MAC 地址表**（转发表）：三台主机分别在 veth1、veth2、veth3 端口上。交换机的工作规则很简单：

1. **学习**：收到一个帧，记下「它的**源** MAC 在收到它的这个端口」；
2. **转发**：查目的 MAC，如果在表中，只从对应的端口发出；
3. **泛洪**：如果目的 MAC 不在表中，或者是广播，就从其他所有端口发出。

下面的演示逐步展示了这个过程：

<div class="dsviz" data-viz="switch-learn"></div>

### 4.2 集线器

早期的局域网使用**集线器**（hub），它不学习，把每个帧都发往所有端口。把网桥的老化时间设为 0，它就不再记录 MAC 地址，相当于一个集线器：

```bash
ip link set br0 type bridge ageing_time 0
```

同样让 h1 ping h2 两次，h3 抓到的包：

```
07:06:56.443251 02:00:00:00:00:01 > ff:ff:ff:ff:ff:ff, ethertype ARP (0x0806), length 42: Request who-has 10.0.0.2 tell 10.0.0.1, length 28
07:06:56.443294 02:00:00:00:00:02 > 02:00:00:00:00:01, ethertype ARP (0x0806), length 42: Reply 10.0.0.2 is-at 02:00:00:00:00:02, length 28
07:06:56.443307 02:00:00:00:00:01 > 02:00:00:00:00:02, ethertype IPv4 (0x0800), length 98: 10.0.0.1 > 10.0.0.2: ICMP echo request, id 299, seq 1, length 64
07:06:56.443327 02:00:00:00:00:02 > 02:00:00:00:00:01, ethertype IPv4 (0x0800), length 98: 10.0.0.2 > 10.0.0.1: ICMP echo reply, id 299, seq 1, length 64
07:06:57.492762 02:00:00:00:00:01 > 02:00:00:00:00:02, ethertype IPv4 (0x0800), length 98: 10.0.0.1 > 10.0.0.2: ICMP echo request, id 299, seq 2, length 64
07:06:57.492808 02:00:00:00:00:02 > 02:00:00:00:00:01, ethertype IPv4 (0x0800), length 98: 10.0.0.2 > 10.0.0.1: ICMP echo reply, id 299, seq 2, length 64
```

h3 收到了全部 6 个包，MAC 地址表也是空的。h3 的网卡发现这些帧的目的 MAC 不是自己，正常情况下会丢弃；tcpdump 能看到它们，是因为抓包时网卡处于**混杂模式**，所有的帧都收下。

集线器的问题很明显：每个帧都占用所有线路的带宽，任何一台主机都能窃听别人的通信。交换机解决了这两个问题，所以集线器已经被淘汰了。（实验后用 `ageing_time 30000` 恢复了默认值，单位是百分之一秒，即 300 秒。）

但即使是交换机，**广播**也会到达每一台主机。一个交换机连接的所有主机，以及通过交换机互相连接的所有主机，构成一个**广播域**。广播域太大时，ARP 等广播会占用大量带宽，所以大的网络要用路由器（或者 VLAN）划分成多个小的网络，第 1.4 篇会讲路由器。

## 5. MTU 和分片

以太网帧能装下的数据有上限，叫作 **MTU**（Maximum Transmission Unit），以太网通常是 1500 字节：

```bash
ip netns exec h1 ip link show eth0 | grep -o "mtu [0-9]*"
```

```
mtu 1500
```

MTU 是对 IP 数据报大小的限制，即 IP 头 + IP 数据最多 1500 字节（不含 14 字节的以太网头）。ping 的数据部分最多就是 1500 − 20（IP 头）− 8（ICMP 头）= **1472** 字节。用 `-M do` 禁止分片，试一下 1472 和 1473：

```bash
ip netns exec h1 ping -c 1 -M do -s 1472 10.0.0.2
ip netns exec h1 ping -c 1 -M do -s 1473 10.0.0.2
```

```
1480 bytes from 10.0.0.2: icmp_seq=1 ttl=64 time=0.054 ms
PING 10.0.0.2 (10.0.0.2) 1473(1501) bytes of data.
ping: local error: message too long, mtu=1500
```

1473 字节时，整个 IP 数据报是 1501 字节，超过了 MTU，发送失败。如果允许分片（`-M dont`），发送 3000 字节：

```bash
ip netns exec h1 ping -c 1 -M dont -s 3000 10.0.0.2
```

h2 上 `tcpdump -v` 看到的请求：

```
IP (tos 0x0, ttl 64, id 39755, offset 0, flags [+], proto ICMP (1), length 1500)
    10.0.0.1 > 10.0.0.2: ICMP echo request, id 345, seq 1, length 1480
IP (tos 0x0, ttl 64, id 39755, offset 1480, flags [+], proto ICMP (1), length 1500)
    10.0.0.1 > 10.0.0.2: ip-proto-1
IP (tos 0x0, ttl 64, id 39755, offset 2960, flags [none], proto ICMP (1), length 68)
    10.0.0.1 > 10.0.0.2: ip-proto-1
```

3000 字节的数据加上 8 字节的 ICMP 头，被 IP 层切成了 3 片：

| 片 | IP 总长度 | IP 数据 | 偏移 | 标志 |
|---|---|---|---|---|
| 1 | 1500 | 1480 | 0 | `+`（后面还有） |
| 2 | 1500 | 1480 | 1480 | `+` |
| 3 | 68 | 48 | 2960 | 无（最后一片） |

1480 + 1480 + 48 = 3008。三片的 IP **标识**（id）都是 39755，接收方据此把它们重新组装起来。只有第一片带着 ICMP 头，所以 tcpdump 只能把第一片识别为 echo request，后两片只显示 `ip-proto-1`。

分片是网络层的功能，但它的起因是链路层的 MTU，所以放在这里讲。分片有很多缺点：任何一片丢失，整个数据报都要重发；中间的防火墙看不到后续片的端口号。所以 TCP 会主动把数据切成不超过 MTU 的段（上一篇握手包中的 `mss 1460` = 1500 − 20 − 20），现代网络中应该尽量避免分片。

## 6. 常见误区

1. **以为交换机会把所有帧都发给所有主机**：那是集线器。交换机只泛洪广播和未知目的的帧。
2. **以为在交换机连接的网络中能抓到别人的包**：只能抓到广播、发给自己的包，以及交换机还没学会时泛洪的包。
3. **以为 ARP 表是永久的**：记录会过期，需要重新确认。
4. **以为 MTU 包括以太网头**：1500 是 IP 数据报的上限，帧最大是 1514 字节（不含校验和）。
5. **跨网络使用 ARP**：ARP 是广播，只在同一个广播域内有效。访问其他网络的主机时，ARP 查询的是网关的 MAC。

## 7. 小结

- MAC 地址 48 位，在同一个网络中标识网卡；`ff:ff:ff:ff:ff:ff` 是广播地址
- ARP 把 IP 地址解析成 MAC 地址：请求是广播，应答是单播；结果缓存在 ARP 表（`ip neigh`）中，会过期
- 交换机学习每个帧的源 MAC，按目的 MAC 转发，未知目的和广播则泛洪；实测 h3 只收到 1 个 ARP 广播，换成集线器后收到全部 6 个包
- 网络命名空间 + veth + 网桥可以在一台机器上搭出一个局域网
- 以太网的 MTU 通常是 1500 字节；超过时 IP 分片（实测 3000 字节切成 3 片），用 `-M do` 禁止分片时直接报错

## 8. 练习

1. 在实验网络中，h1 执行 `ping 10.0.0.9`（一个不存在的地址），会发生什么？抓包看看 h1 发出了什么。
2. 把 h1 的 MTU 改成 1400，`ping -M do -s 1400 10.0.0.2` 会怎样？`-s` 最大能取多少？
3. 为什么 ARP 应答是单播，而不是广播？

<details>
<summary>点击查看答案</summary>

1. ```bash
   ip netns exec h1 tcpdump -i eth0 -nn -e "arp or icmp" &
   ip netns exec h1 ping -c 3 10.0.0.9
   ```

   ```
   From 10.0.0.1 icmp_seq=1 Destination Host Unreachable
   From 10.0.0.1 icmp_seq=2 Destination Host Unreachable
   From 10.0.0.1 icmp_seq=3 Destination Host Unreachable
   ```

   抓到的全部是 ARP 请求，大约每秒一个，**一个 ping 包都没有发出去**：

   ```
   07:09:52.882461 02:00:00:00:00:01 > ff:ff:ff:ff:ff:ff, ethertype ARP (0x0806), length 42: Request who-has 10.0.0.9 tell 10.0.0.1, length 28
   07:09:53.949414 02:00:00:00:00:01 > ff:ff:ff:ff:ff:ff, ethertype ARP (0x0806), length 42: Request who-has 10.0.0.9 tell 10.0.0.1, length 28
   07:09:54.972554 02:00:00:00:00:01 > ff:ff:ff:ff:ff:ff, ethertype ARP (0x0806), length 42: Request who-has 10.0.0.9 tell 10.0.0.1, length 28
   ```

   没有人回答，h1 不知道目的 MAC，无法构造以太网帧，ping 只能报告 `Destination Host Unreachable`（这个报告来自 h1 自己，`From 10.0.0.1`）。`ip neigh` 中这条记录的状态是 `FAILED`。在同一个网络中 ping 不通时，看看 ARP 表，就能区分「对方不存在或者没有开机」和「对方存在但不回应 ping」（比如被防火墙拦截）。

2. MTU 改为 1400 后，ping 的数据最多是 1400 − 20 − 8 = **1372** 字节：

   ```bash
   ip netns exec h1 ip link set eth0 mtu 1400
   ip netns exec h1 ping -c 1 -M do -s 1400 10.0.0.2
   ip netns exec h1 ping -c 1 -M do -s 1372 10.0.0.2
   ip netns exec h1 ping -c 1 -M do -s 1373 10.0.0.2
   ```

   ```
   PING 10.0.0.2 (10.0.0.2) 1400(1428) bytes of data.
   ping: local error: message too long, mtu=1400
   1380 bytes from 10.0.0.2: icmp_seq=1 ttl=64 time=0.136 ms
   ping: local error: message too long, mtu=1400
   ```

   VPN、隧道等会在原来的包外面再套一层头部，所以它们的网卡 MTU 通常小于 1500。MTU 设置不对时，常见的现象是「ping 得通，小网页能打开，大文件却下载不了」。
3. 只有发出请求的主机需要这个答案。ARP 请求中已经带着请求者的 MAC 地址，应答方可以直接单播给它，不必打扰网络中的其他主机。请求之所以必须是广播，是因为请求者此时还不知道对方的 MAC 地址。

</details>
