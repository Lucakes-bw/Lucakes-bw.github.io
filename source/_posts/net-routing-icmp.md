---
title: 路由与 ICMP
date: 2026-10-24 15:10:00
updated: 2026-10-24 15:10:00
categories:
  - 计算机网络
  - 网络模型
tags:
  - 计算机网络
  - 路由
  - ICMP
order: 4
description: 不同子网之间的通信要靠路由器。本文用网络命名空间搭一个「主机—路由器—路由器—主机」的网络，从零开始一条一条地配置路由，记录下每一步的失败：Network is unreachable、Destination Net Unreachable、被反向路径过滤悄悄丢弃；用 ip route get 验证最长前缀匹配；故意配出一个路由环路，看 TTL 从 63 减到 1；抓包看懂 traceroute 的原理；最后配置 NAT，看源地址怎样被替换。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解路由表和路由器转发数据的过程；会用 `ip route` 查看和配置路由；理解默认路由和最长前缀匹配；理解 TTL 的作用；理解 ping 和 traceroute 所用的 ICMP 消息；理解 NAT 的工作方式。
>
> **前置知识**：上一篇的子网和「同一个子网才能直接通信」；第 1.2 篇用网络命名空间搭建实验网络的方法。
>
> **实验环境**：Ubuntu 24.04 容器，iproute2 6.1，traceroute 2.1，iptables 1.8。

## 1. 路由器和路由表

上一篇看到，目的地址不在同一个子网中时，主机要把数据交给**路由器**。路由器是连接多个网络的设备，它有多个网卡，每个网卡属于一个网络。收到一个 IP 数据报时，它查**路由表**，决定从哪个网卡发出、交给谁（下一跳），这个过程叫作**转发**。

主机也有路由表，只是通常很简单。实验容器的路由表：

```bash
ip route
```

```
default via 172.17.0.1 dev eth0
172.17.0.0/16 dev eth0 proto kernel scope link src 172.17.0.3
```

- 第二条：172.17.0.0/16 是直接连接的网络（`scope link`），这个网络中的地址直接用 ARP 找；
- 第一条：**默认路由**，其他所有地址都交给 172.17.0.1（网关）。第 1.1 篇中访问 example.com 时，帧的目的 MAC 是网关的，就是因为这条路由。

## 2. 搭一个有两台路由器的网络

```
 h1                 r1                      r2                 h2
 eth0 ──────── eth0    eth1 ──────── eth0    eth1 ──────── eth0
10.0.1.10   10.0.1.1  10.0.12.1    10.0.12.2  10.0.2.1    10.0.2.10
   └── 10.0.1.0/24 ──┘   └── 10.0.12.0/24 ──┘   └── 10.0.2.0/24 ──┘
```

三个子网，两台路由器 r1 和 r2。每一段都直接用一对 veth 连接，不需要交换机：

```bash
#!/bin/bash
# route.sh：h1 —— r1 —— r2 —— h2，三个子网，两台路由器
set -e
for n in h1 r1 r2 h2; do
    ip netns add $n
    ip netns exec $n ip link set lo up
done
link() {                                   # link 命名空间1 网卡1 地址1 命名空间2 网卡2 地址2
    ip link add tmpa type veth peer name tmpb          # 先用临时的名字创建一对 veth
    ip link set tmpa netns $1; ip netns exec $1 ip link set tmpa name $2
    ip link set tmpb netns $4; ip netns exec $4 ip link set tmpb name $5
    ip netns exec $1 ip addr add $3 dev $2
    ip netns exec $4 ip addr add $6 dev $5
    ip netns exec $1 ip link set $2 up
    ip netns exec $4 ip link set $5 up
}
link h1 eth0 10.0.1.10/24  r1 eth0 10.0.1.1/24
link r1 eth1 10.0.12.1/24  r2 eth0 10.0.12.2/24
link r2 eth1 10.0.2.1/24   h2 eth0 10.0.2.10/24
```

（veth 要先用临时的名字创建：如果直接叫 eth0，会和容器自己的 eth0 冲突。）建好后，每台机器的路由表中只有自己直接连接的网络，例如 r1：

```
10.0.1.0/24 dev eth0 proto kernel scope link src 10.0.1.1
10.0.12.0/24 dev eth1 proto kernel scope link src 10.0.12.1
```

下面一步一步地配置，让 h1 能 ping 通 h2。每一步失败的方式都不一样，它们正是实际排查网络问题时会遇到的情况。

## 3. 一步一步配置路由

**第 1 步：什么都不配。**

```bash
ip netns exec h1 ping -c 1 10.0.2.10
```

```
ping: connect: Network is unreachable
```

和上一篇一样：h1 的路由表中没有任何一条路由能匹配 10.0.2.10，错误在 h1 本机就产生了，一个包都没有发出去。

**第 2 步：给 h1 和 h2 加上默认路由**，分别指向各自的路由器：

```bash
ip netns exec h1 ip route add default via 10.0.1.1
ip netns exec h2 ip route add default via 10.0.2.1
ip netns exec h1 ping -c 1 10.0.2.10
```

```
From 10.0.1.1 icmp_seq=1 Destination Net Unreachable
```

这次包发出去了，但报错来自 **10.0.1.1**，也就是 r1：r1 收到了包，查自己的路由表，没有到 10.0.2.0/24 的路由，于是丢弃这个包，并发回一个 ICMP「目的网络不可达」消息。

**第 3 步：给 r1 加上到 10.0.2.0/24 的路由**，下一跳是 r2：

```bash
ip netns exec r1 ip route add 10.0.2.0/24 via 10.0.12.2
ip netns exec h1 ping -c 1 -W 1 10.0.2.10
```

```
--- 10.0.2.10 ping statistics ---
1 packets transmitted, 0 received, 100% packet loss, time 0ms
```

没有任何报错，就是收不到回复。在 r2 连接 h2 的 eth1 上抓包，也什么都没有：包到了 r2，却没有被转发出去。r2 并非不知道去 10.0.2.10 的路，问题在**回去的路**。用 `nstat` 查看 r2 的计数器，ping 3 次前后：

```
TcpExtIPReversePathFilter       6                  0.0
TcpExtIPReversePathFilter       9                  0.0
```

**反向路径过滤**（reverse path filter）丢弃了这 3 个包。r2 收到一个源地址为 10.0.1.10 的包，发现自己根本没有任何路由能到达 10.0.1.10，认为这个源地址很可能是伪造的，于是丢弃（`/proc/sys/net/ipv4/conf/all/rp_filter` 为 2，即宽松模式：只要有路由能回去就接受）。这个机制可以阻止伪造源地址的攻击，但也让配置漏掉回程路由时的表现变成了「悄悄地丢包」。

**第 4 步：给 r2 加上回程路由。**

```bash
ip netns exec r2 ip route add 10.0.1.0/24 via 10.0.12.1
ip netns exec h1 ping -c 2 10.0.2.10
```

```
64 bytes from 10.0.2.10: icmp_seq=1 ttl=62 time=0.123 ms
64 bytes from 10.0.2.10: icmp_seq=2 ttl=62 time=0.143 ms
```

通了。再看 r2 的计数器，反向路径过滤的计数不再增加，转发的数据报（`IpForwDatagrams`）增加了 6 个：3 个请求加 3 个回复。回复的 `ttl=62`：h2 发出时是 64，经过 r2、r1 两台路由器，各减了 1。

**通信是双向的，去程和回程都需要路由。**「ping 不通」时，要想到可能是回来的路断了。

> **转发开关**：Linux 默认不转发不是发给自己的包，要打开 `net.ipv4.ip_forward`。实验容器中它已经是 1（Docker 为了容器能上网打开了它），新建的命名空间也继承了这个值，所以上面没有专门去打开。把 r1 的转发关掉（`sysctl -w net.ipv4.ip_forward=0`）再 ping，r1 上抓包只看到 `In` 方向的请求，它被悄悄丢弃了，连 ICMP 报错都没有。在容器中修改命名空间的 sysctl，要先 `mount -o remount,rw /proc/sys`，因为 Docker 把它挂载成了只读。

## 4. 最长前缀匹配

路由表中可能有多条路由都能匹配一个目的地址。规则是：**选前缀最长的那一条**，也就是最具体的那一条。给 r1 再加两条路由做实验：

```bash
ip netns exec r1 ip route add 10.0.0.0/16 via 10.0.12.2
ip netns exec r1 ip route add 10.0.2.128/25 dev eth0
ip netns exec r1 ip route
```

```
10.0.0.0/16 via 10.0.12.2 dev eth1
10.0.1.0/24 dev eth0 proto kernel scope link src 10.0.1.1
10.0.2.0/24 via 10.0.12.2 dev eth1
10.0.2.128/25 dev eth0 scope link
10.0.12.0/24 dev eth1 proto kernel scope link src 10.0.12.1
```

`ip route get` 查询内核对某个地址实际选择的路由：

```bash
for d in 10.0.2.10 10.0.2.200 10.0.7.7 8.8.8.8; do ip netns exec r1 ip route get $d; done
```

```
10.0.2.10 via 10.0.12.2 dev eth1 src 10.0.12.1 uid 0
10.0.2.200 dev eth0 src 10.0.1.1 uid 0
10.0.7.7 via 10.0.12.2 dev eth1 src 10.0.12.1 uid 0
RTNETLINK answers: Network is unreachable
```

| 目的地址 | 能匹配的路由 | 选中 |
|---|---|---|
| 10.0.2.10 | 10.0.0.0/16、10.0.2.0/24 | **/24** |
| 10.0.2.200 | 10.0.0.0/16、10.0.2.0/24、10.0.2.128/25 | **/25** |
| 10.0.7.7 | 10.0.0.0/16 | /16 |
| 8.8.8.8 | 无（r1 没有默认路由） | 不可达 |

10.0.2.200 落在 10.0.2.128/25 中，即使还有两条更「宽」的路由也能匹配它，也选了最具体的 /25。默认路由 `default` 就是 `0.0.0.0/0`，前缀长度为 0，能匹配所有地址，但优先级最低，只有在没有其他路由匹配时才会用到。互联网核心路由器的路由表中有约 100 万条路由，它们每秒要做数亿次这样的查找，所以通常用专门的硬件实现。

（实验后删除了这两条路由。）

## 5. TTL 和路由环路

如果路由配错了，比如 r1 把未知的地址交给 r2，r2 又把未知的地址交给 r1，一个包就会在它们之间无限循环。IP 头中的 **TTL**（Time To Live）就是为此设计的：每经过一个路由器减 1，减到 0 时路由器丢弃这个包，并发回一个 ICMP「超时」消息。故意配出这样的环路：

```bash
ip netns exec r1 ip route add default via 10.0.12.2
ip netns exec r2 ip route add default via 10.0.12.1
ip netns exec h1 ping -c 1 10.9.9.9
```

```
From 10.0.12.2 icmp_seq=1 Time to live exceeded
```

在 r1 和 r2 之间的链路上抓包，这一个 ping 请求在链路上被来回转发，TTL 从 63（经过 r1 时减了 1）开始逐次减小：

```
ttl 63 ttl 62 ttl 61 ttl 60 ...
```

一直减到 1，在这条链路上一共出现了 63 次，最后 r2 把 TTL 减到 0，丢弃它，向 h1 发回了「Time to live exceeded」。没有 TTL，这一个包就会永远在两台路由器之间循环，占用带宽。Linux 发出的包 TTL 默认是 64（`/proc/sys/net/ipv4/ip_default_ttl`），Windows 是 128。

（实验后删除了这两条默认路由。）

## 6. ICMP 与 traceroute

**ICMP**（Internet Control Message Protocol）用来报告错误和传递控制信息，它直接装在 IP 中（协议号 1）。上面已经遇到了好几种 ICMP 消息：

| 类型 | 代码 | 含义 | 本文中 |
|---|---|---|---|
| 8 | 0 | 回显请求（echo request） | ping 发出的 |
| 0 | 0 | 回显应答（echo reply） | ping 收到的 |
| 3 | 0 | 目的网络不可达 | 第 3 节第 2 步，r1 没有路由 |
| 3 | 1 | 目的主机不可达 | 第 1.2 篇练习 1，ARP 没有回答 |
| 3 | 3 | 目的端口不可达 | 下面的 traceroute |
| 11 | 0 | 传输中 TTL 超时 | 第 5 节的路由环路 |

ping 就是发送回显请求、等待回显应答，计算往返时间。

**traceroute** 巧妙地利用了 TTL 超时：

```bash
ip netns exec h1 traceroute -n 10.0.2.10
```

```
traceroute to 10.0.2.10 (10.0.2.10), 30 hops max, 60 byte packets
 1  10.0.1.1  0.039 ms  0.042 ms  0.005 ms
 2  10.0.12.2  0.026 ms  0.005 ms  0.005 ms
 3  10.0.2.10  0.031 ms  0.006 ms  0.005 ms
```

它显示出了路径上的每一台路由器。在 r1 的 eth0 上抓包看它是怎样做到的（每跳只发 1 个探测）：

```
IP (tos 0x0, ttl 1, id 31358, offset 0, flags [none], proto UDP (17), length 60)
    10.0.1.10.42981 > 10.0.2.10.33434: UDP, length 32
IP (tos 0xc0, ttl 64, id 29263, offset 0, flags [none], proto ICMP (1), length 88)
    10.0.1.1 > 10.0.1.10: ICMP time exceeded in-transit, length 68
IP (tos 0x0, ttl 2, id 61413, offset 0, flags [none], proto UDP (17), length 60)
    10.0.1.10.38175 > 10.0.2.10.33435: UDP, length 32
IP (tos 0xc0, ttl 63, id 27081, offset 0, flags [none], proto ICMP (1), length 88)
    10.0.12.2 > 10.0.1.10: ICMP time exceeded in-transit, length 68
IP (tos 0x0, ttl 3, id 31771, offset 0, flags [none], proto UDP (17), length 60)
    10.0.1.10.41505 > 10.0.2.10.33436: UDP, length 32
IP (tos 0xc0, ttl 62, id 48994, offset 0, flags [none], proto ICMP (1), length 88)
    10.0.2.10 > 10.0.1.10: ICMP 10.0.2.10 udp port 33436 unreachable, length 68
```

1. 发一个 **TTL = 1** 的 UDP 包。第一台路由器 r1 把 TTL 减到 0，丢弃它，发回「TTL 超时」，**源地址就是 r1 自己**。于是知道了第 1 跳是 10.0.1.1；
2. 发 TTL = 2 的包，它在第二台路由器 r2 处超时，知道了第 2 跳是 10.0.12.2；
3. 发 TTL = 3 的包，它到达了 h2。h2 上没有程序在监听 33436 这个不常用的端口，于是回复「**端口不可达**」，traceroute 知道已经到达终点。

每个超时消息中还附带了原来那个包的 IP 头（输出中缩进的部分，这里省略了），traceroute 据此知道这是对哪个探测的回答。注意 h2 回复的 TTL 是 62，经过了两台路由器。

在真实的互联网上，很多路由器出于安全考虑不回复 ICMP，traceroute 中会显示 `* * *`；也有防火墙会拦截 UDP，这时可以用 `traceroute -I`（用 ICMP 回显请求做探测）或 `-T`（用 TCP）。

## 7. NAT

上一篇讲过，私有地址的主机要经过 **NAT** 访问互联网。把 h2 当作互联网上的一台服务器，让 r2 对来自 10.0.1.0/24 的、从 eth1 发出的包做 NAT：

```bash
ip netns exec r2 iptables -t nat -A POSTROUTING -s 10.0.1.0/24 -o eth1 -j MASQUERADE
```

`MASQUERADE`（伪装）表示把源地址换成发出网卡（eth1）的地址。在 r2 的内侧（eth0）和 h2 上同时抓包，h1 ping h2 一次：

r2 的 eth0（NAT 之前）：

```
07:14:39.990664 IP 10.0.1.10 > 10.0.2.10: ICMP echo request, id 771, seq 1, length 64
07:14:39.990780 IP 10.0.2.10 > 10.0.1.10: ICMP echo reply, id 771, seq 1, length 64
```

h2 看到的（NAT 之后）：

```
07:14:39.990750 IP 10.0.2.1 > 10.0.2.10: ICMP echo request, id 771, seq 1, length 64
07:14:39.990776 IP 10.0.2.10 > 10.0.2.1: ICMP echo reply, id 771, seq 1, length 64
```

h2 完全不知道 h1 的存在，它看到的请求来自 r2 的 10.0.2.1，回复也发给了 10.0.2.1。r2 在转发请求时记下了「id 为 771 的 ICMP 来自 10.0.1.10」（这个记录叫作**连接跟踪**，TCP 和 UDP 则用端口号区分），收到回复后查这个记录，把目的地址换回 10.0.1.10，转发给 h1。对于 h1 来说，这一切都是透明的。

既然 h2 只和 10.0.2.1 打交道，它就不再需要到 10.0.1.0/24 的路由了。删掉 h2 的默认路由（它现在只认识自己的 10.0.2.0/24），有 NAT 时 h1 仍然能 ping 通；删掉 NAT 规则，就 ping 不通了：

```
64 bytes from 10.0.2.10: icmp_seq=1 ttl=62 time=0.056 ms

--- 10.0.2.10 ping statistics ---
1 packets transmitted, 0 received, 100% packet loss, time 0ms
```

这正是家庭网络的情况：互联网上的服务器不知道也不需要知道 192.168.x.x 网络的存在，只要能回复到家庭路由器的公网地址就行。而反过来，h2 无法主动访问 h1：它不知道 h1 的地址，r2 上也没有对应的连接跟踪记录。

## 8. 常见错误

1. **只配了去程路由，忘了回程**：ping 不通，而且没有任何报错，因为包在途中被悄悄丢弃。
2. **以为 `Network is unreachable` 和 `Destination Net Unreachable` 是一回事**：前者是本机没有路由，包没有发出；后者来自途中的某台路由器，`From` 后面就是它的地址。
3. **忘记打开 `ip_forward`**：普通的 Linux 主机默认不转发。
4. **以为 traceroute 中的 `* * *` 表示网络断了**：可能只是那台路由器不回复 ICMP。
5. **在路由器上配置互相指向的默认路由**：路由环路，靠 TTL 才不至于无限循环。
6. **以为 NAT 后面的主机能被外网直接访问**：需要在路由器上配置端口映射（DNAT）。

## 9. 小结

- 路由器连接多个网络，查路由表把数据报转发给下一跳；每台主机也有路由表，通常是直接连接的网络加一条默认路由
- 去程和回程都需要路由；实测缺少路由时依次遇到：本机 `Network is unreachable`、路由器回复 `Destination Net Unreachable`、反向路径过滤悄悄丢弃
- 多条路由匹配时选前缀最长的一条；默认路由 0.0.0.0/0 优先级最低；`ip route get` 查询实际的选择
- 每经过一个路由器 TTL 减 1，减到 0 时丢弃并回复 ICMP 超时；实测路由环路中一个包在链路上出现了 63 次
- traceroute 依次发送 TTL = 1、2、3…… 的探测，根据 ICMP 超时消息的源地址得到每一跳
- NAT（MASQUERADE）把源地址换成路由器的地址，靠连接跟踪把回复送回原来的主机

## 10. 练习

1. 在第 3 节第 4 步之后，h2 上执行 `traceroute -n 10.0.1.10`，会显示哪几跳？
2. h1 ping r2 的 10.0.12.2 能通吗？ping r2 的 10.0.2.1 呢？
3. 为什么 traceroute 的探测用一个不常用的 UDP 端口（33434 起）？

<details>
<summary>点击查看答案</summary>

1. ```bash
   ip netns exec h2 traceroute -n 10.0.1.10
   ```

   ```
   traceroute to 10.0.1.10 (10.0.1.10), 30 hops max, 60 byte packets
    1  10.0.2.1  0.022 ms  0.004 ms  0.001 ms
    2  10.0.12.1  0.026 ms  0.003 ms  0.002 ms
    3  10.0.1.10  0.008 ms  0.003 ms  0.002 ms
   ```

   和 h1 到 h2 的方向相比，路由器显示的地址不同了：第 1 跳是 r2 的 10.0.2.1，第 2 跳是 r1 的 10.0.12.1。路由器发回超时消息时，用的是**收到探测的那个网卡**的地址，所以同一台路由器在两个方向上显示的地址不一样。

2. 都能通：

   ```
   64 bytes from 10.0.12.2: icmp_seq=1 ttl=63 time=0.013 ms
   64 bytes from 10.0.2.1: icmp_seq=1 ttl=63 time=0.038 ms
   ```

   这两个地址都属于 r2 自己，r1 有到 10.0.12.0/24 的直连路由和到 10.0.2.0/24 的路由，能把请求送到 r2；r2 有到 10.0.1.0/24 的回程路由。回复的 TTL 都是 63：r2 发出时是 64，只经过了 r1。路由器回复发给自己任何一个地址的 ping，不论请求是从哪个网卡进来的。
3. traceroute 要靠目的主机回复「端口不可达」来判断已经到达终点。如果用的是一个有程序在监听的端口，目的主机就会把探测交给那个程序，而不是回复 ICMP，traceroute 就不知道已经到了。33434 以上的端口通常没有程序使用。每个探测的端口还依次加 1，这样可以根据超时消息中附带的原始包判断这是哪个探测的回答。

</details>
