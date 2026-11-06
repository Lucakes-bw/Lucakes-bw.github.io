---
title: 容器网络：网桥、veth 与端口映射
date: 2026-11-12 10:30:00
updated: 2026-11-12 10:30:00
categories:
  - Linux基础
  - 容器原理
tags:
  - Linux
  - 容器
  - 网络
order: 4
description: 每个容器都有自己的网络命名空间：自己的网卡、IP、路由表和端口。它们怎样互相通信、访问外网、被外面访问？本文用 ip 和 iptables 命令手工搭出 Docker 默认的网络：一个网桥、每个容器一对 veth、一条 MASQUERADE 规则、一条 DNAT 规则，再和 Docker 自己生成的规则对照。实测延迟：经过网桥和 DNAT 只比本机回环多一两微秒，几乎可以忽略；但从宿主机用 127.0.0.1 访问映射的端口，走的是用户态的 docker-proxy，一次往返从 17.5 微秒变成 42.3 微秒。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解网络命名空间、veth、网桥如何组成容器网络；会手工搭建 Docker 式的 bridge 网络；理解容器访问外网用的 SNAT（MASQUERADE）和端口映射用的 DNAT；能看懂 Docker 生成的 iptables 规则；知道 docker-proxy 是什么、什么时候会经过它；知道容器网络的开销在哪里。
>
> **前置知识**：本章第 1 篇；《计算机网络》专栏的《路由与 ICMP》（路由、转发）、《数据链路层：MAC 与 ARP》（交换机）；《网络排障》一章的《内核在哪里丢包》（连接跟踪）。
>
> **实验环境**：同第 1 篇。在容器的网络命名空间中搭建实验，这个命名空间扮演「宿主机」。

## 1. 三个零件

一个新的网络命名空间里只有一个回环网卡，而且是关闭的。要让它和外界通信，需要三个零件：

- **veth**：一对虚拟网卡，从一端发进去的包从另一端出来，像一根网线。一端放进容器的命名空间，另一端留在宿主机；
- **网桥**（bridge）：宿主机上的一个虚拟交换机，把所有容器的 veth 连在一起，同一个网桥上的容器可以直接通信。网桥本身也可以有 IP 地址，作为容器的网关；
- **iptables 的 NAT 规则**：容器的地址是私有的（172.17.0.0/16），访问外网要把源地址换成宿主机的地址（SNAT）；外面访问宿主机的某个端口，要把目的地址换成容器的地址（DNAT）。

<figure class="diagram">
<svg viewBox="0 0 700 290" role="img" aria-label="手工搭建的容器网络：宿主机上的网桥 br0（10.10.0.1）通过 veth 连接容器 ctrA、ctrB；外部客户端 ext 通过另一对 veth 连到宿主机。容器访问外部走 MASQUERADE，外部访问宿主机的 8080 端口被 DNAT 到 ctrA:80">
  <defs><marker id="tpo-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="tpo-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="20" width="150" height="50" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="95.0" y="41.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">ctrA</text><text x="95.0" y="56.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">eth0 10.10.0.2</text><rect x="20" y="120" width="150" height="50" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="95.0" y="141.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">ctrB</text><text x="95.0" y="156.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">eth0 10.10.0.3</text><rect x="220" y="10" width="300" height="200" rx="8" style="fill:none;stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:5 4"/><text x="232.0" y="30.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">宿主机</text><rect x="240" y="70" width="130" height="50" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="305.0" y="91.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">br0（网桥）</text><text x="305.0" y="106.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">10.10.0.1</text><rect x="400" y="70" width="110" height="50" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="455.0" y="91.3" text-anchor="middle" style="fill:var(--fg);font-size:11px">iptables nat</text><text x="455.0" y="106.3" text-anchor="middle" style="fill:var(--muted);font-size:10px">DNAT、MASQ</text><rect x="400" y="140" width="110" height="50" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="455.0" y="161.3" text-anchor="middle" style="fill:var(--fg);font-size:11px">veth-ext</text><text x="455.0" y="176.3" text-anchor="middle" style="fill:var(--muted);font-size:10px">192.168.50.1</text><rect x="560" y="140" width="120" height="50" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="620.0" y="161.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">ext（外部）</text><text x="620.0" y="176.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">192.168.50.2</text><line x1="172" y1="45" x2="238" y2="85" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#tpo-a)"/><text x="200.0" y="56.0" text-anchor="middle" style="fill:var(--link);font-size:11px">veth</text><line x1="172" y1="145" x2="238" y2="105" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#tpo-a)"/><text x="200.0" y="136.0" text-anchor="middle" style="fill:var(--link);font-size:11px">veth</text><line x1="372" y1="95" x2="398" y2="95" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#tpo-a)"/><line x1="455" y1="122" x2="455" y2="138" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#tpo-a)"/><line x1="512" y1="165" x2="558" y2="165" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#tpo-a)"/><text x="20.0" y="236.0" text-anchor="start" style="fill:var(--fg);font-size:11px">ctrA → ext：源地址 10.10.0.2 被 MASQUERADE 成 192.168.50.1（ext 看到的是宿主机）</text><text x="20.0" y="256.0" text-anchor="start" style="fill:var(--fg);font-size:11px">ext → 192.168.50.1:8080：DNAT 到 10.10.0.2:80（相当于 docker run -p 8080:80）</text><text x="20.0" y="276.0" text-anchor="start" style="fill:var(--muted);font-size:11px">ctrA ↔ ctrB：同一个网桥上，直接二层转发</text>
  </g>
</svg>
</figure>

## 2. 手工搭建

`scripts/bridge.sh` 用十几条命令搭出和 Docker 默认网络相同的结构。网桥和两个「容器」：

```bash
ip link add br0 type bridge
ip addr add 10.10.0.1/24 dev br0          # 网桥的地址，就是容器的网关
ip link set br0 up

ip netns add ctrA
ip link add veth-ctrA type veth peer name eth0 netns ctrA   # 一端留在宿主机，另一端放进容器，改名为 eth0
ip link set veth-ctrA master br0 up                         # 宿主机这一端插到网桥上
ip -n ctrA addr add 10.10.0.2/24 dev eth0
ip -n ctrA link set eth0 up
ip -n ctrA link set lo up
ip -n ctrA route add default via 10.10.0.1                  # 默认路由指向网桥
# ctrB 同样，地址是 10.10.0.3
```

`bridge link` 显示两个 veth 都插在 br0 上：

```
4: veth-ctrA@eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 master br0 state forwarding ...
5: veth-ctrB@eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 master br0 state forwarding ...
```

ctrA `ping` ctrB 直接通过网桥，不需要路由：`rtt 0.034 ms`。

再建一个命名空间 `ext` 代表外面的客户端，用另一对 veth 连到宿主机（192.168.50.0/24），然后加上两条 NAT 规则：

```bash
sysctl -w net.ipv4.ip_forward=1
# 容器访问外部：源地址伪装成宿主机出口的地址
iptables -t nat -A POSTROUTING -s 10.10.0.0/24 ! -o br0 -j MASQUERADE
# 端口映射：访问宿主机的 8080，转给 ctrA 的 80（相当于 docker run -p 8080:80）
iptables -t nat -A PREROUTING -p tcp --dport 8080 -j DNAT --to-destination 10.10.0.2:80
```

**SNAT**。ctrA 连接 ext 上的服务（192.168.50.2:9000），两边看到的连接：

```
ctrA 上：ESTAB  10.10.0.2:36902      192.168.50.2:9000
ext  上：ESTAB  192.168.50.2:9000    192.168.50.1:36902
```

ext 看到的对方是 192.168.50.1，也就是宿主机，根本不知道 10.10.0.2 的存在。回程的包到达宿主机后，连接跟踪记得这个连接是谁发起的，自动把目的地址换回 10.10.0.2。

**DNAT**。ext 访问宿主机的 192.168.50.1:8080，PREROUTING 链把目的地址改成 10.10.0.2:80，包被转发给 ctrA。规则的计数器显示命中了：

```
    2   120 DNAT        tcp dpt:8080 to:10.10.0.2:80
    1    60 MASQUERADE  10.10.0.0/24  !br0
```

这两条规则都依赖连接跟踪：只有每个连接的第一个包经过 nat 表，之后的包按连接跟踪中的记录自动转换。所以《内核在哪里丢包》中连接跟踪表满了的问题，在运行大量容器的机器上格外常见。

## 3. Docker 生成的规则

用 `docker run -d -p 18080:80 ubuntu:24.04 sleep 300` 启动一个容器，在虚拟机上看 nat 表（截取）：

```
-N DOCKER
-A PREROUTING -m addrtype --dst-type LOCAL -j DOCKER
-A OUTPUT ! -d 127.0.0.0/8 -m addrtype --dst-type LOCAL -j DOCKER
-A DOCKER ! -i docker0 -p tcp -m tcp --dport 18080 -j DNAT --to-destination 172.17.0.5:80
```

和手工搭的一样：目的地址是本机的包进入 `DOCKER` 链，18080 端口 DNAT 到容器的 172.17.0.5:80。`docker0` 就是网桥，地址 172.17.0.1/16。

同时还有两个进程：

```
/usr/bin/docker-proxy -proto tcp -host-ip 0.0.0.0 -host-port 18080 -container-ip 172.17.0.5 -container-port 80
/usr/bin/docker-proxy -proto tcp -host-ip :: -host-port 18080 -container-ip 172.17.0.5 -container-port 80
```

**docker-proxy** 是一个用户态的转发程序：它监听宿主机的 18080 端口，收到连接后自己再连接容器的 80 端口，在两个连接之间复制数据。它用来处理 iptables 规则覆盖不到的情况，最常见的是**从宿主机本身用 127.0.0.1 访问**（`OUTPUT` 链的规则排除了 `127.0.0.0/8`），还有 IPv6。

## 4. 开销

`pingpong` 在 TCP 连接上一来一回 1 字节，测平均每次往返的时间。服务器在 ctrA 的 80 端口，客户端在不同的位置，每次 20000 次往返，测两轮：

| 路径 | 第 1 轮 | 第 2 轮 |
|---|---|---|
| ctrA 内部，回环 127.0.0.1 | 17.1 µs | 15.2 µs |
| ctrB → ctrA，经过网桥 | 15.9 µs | 15.8 µs |
| 宿主机 → ctrA，经过网桥 | 16.3 µs | 16.3 µs |
| ext → 宿主机:8080 → ctrA，经过 DNAT | 17.3 µs | 17.4 µs |

经过 veth 和网桥，和回环没有可测量的区别；经过 DNAT 多了一两微秒。一次往返的十几微秒，主要花在两边的系统调用和进程的唤醒上，网络路径本身只占很小一部分。

在真正的 Docker 上，对比三种访问同一个容器的方式（服务器在容器中，客户端用 `--network host` 运行在虚拟机的网络命名空间中）：

| 路径 | 第 1 轮 | 第 2 轮 |
|---|---|---|
| 直接连容器的 IP 172.17.0.5:80 | 15.1 µs | 17.0 µs |
| 虚拟机的 IP:18080（DNAT） | 17.4 µs | 17.5 µs |
| 127.0.0.1:18080（docker-proxy） | 42.3 µs | 42.3 µs |

<figure class="diagram">
<svg viewBox="0 0 700 170" role="img" aria-label="访问同一个 Docker 容器的三种方式，每次 TCP 往返 1 字节的平均时间（两轮的平均）：直接连容器 IP 16.1 微秒，经过 DNAT 17.5 微秒，经过用户态的 docker-proxy 42.3 微秒">
  <defs><marker id="lat-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="lat-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="250.0" y="36.0" text-anchor="end" style="fill:var(--fg);font-size:12px">直接连容器 IP</text><rect x="260.0" y="20.0" width="128.8" height="22.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="396.8" y="36.0" text-anchor="start" style="fill:var(--muted);font-size:11px">16.1 µs</text><text x="250.0" y="76.0" text-anchor="end" style="fill:var(--fg);font-size:12px">宿主机 IP:18080（DNAT）</text><rect x="260.0" y="60.0" width="140.0" height="22.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="408.0" y="76.0" text-anchor="start" style="fill:var(--muted);font-size:11px">17.5 µs</text><text x="250.0" y="116.0" text-anchor="end" style="fill:var(--fg);font-size:12px">127.0.0.1:18080（docker-proxy）</text><rect x="260.0" y="100.0" width="338.4" height="22.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="606.4" y="116.0" text-anchor="start" style="fill:var(--muted);font-size:11px">42.3 µs</text><text x="20.0" y="160.0" text-anchor="start" style="fill:var(--muted);font-size:11px">docker-proxy 在用户态复制数据：多两次系统调用和进程切换</text>
  </g>
</svg>
</figure>

经过 docker-proxy 的往返时间是 DNAT 的 **2.4 倍**：每个字节都要从内核复制到 docker-proxy 的用户空间，再从另一个套接字发出去，多了两次系统调用和两次进程切换。对吞吐量和延迟要求高的服务，不要从宿主机通过 127.0.0.1 访问映射的端口，直接用容器的地址，或者用 `--network host`；Docker 也可以用 `"userland-proxy": false` 关掉 docker-proxy，改用 iptables 处理本机访问。

Kubernetes 等环境中，容器网络还可能经过 VXLAN 等隧道（多一层封装，MTU 变小，见《MTU 黑洞》）或者 eBPF 实现的转发，开销和本文的情况不同，但基本的零件是一样的：命名空间、veth、路由或网桥、NAT。

## 5. 常见错误

1. **忘了开 `ip_forward`**。容器能访问宿主机，却访问不了外网，也收不到端口映射的流量。
2. **以为端口映射是 docker-proxy 在转发所有流量**。外部来的流量走的是内核的 DNAT，只有本机回环、IPv6 等情况才经过 docker-proxy。
3. **从宿主机用 127.0.0.1 访问容器的映射端口做性能测试**。测到的是 docker-proxy 的性能。
4. **手动清空 iptables 的 nat 表**（`iptables -t nat -F`）。Docker 的所有端口映射和 MASQUERADE 都会消失，要重启 Docker 才能恢复。
5. **大量容器、大量短连接，却没注意连接跟踪表的容量**。

## 6. 小结

- 容器网络的零件：网络命名空间、veth（虚拟网线）、网桥（虚拟交换机）、iptables 的 NAT；
- 容器访问外部靠 MASQUERADE（SNAT），外部访问容器靠 DNAT，都依赖连接跟踪；手工搭建的规则和 Docker 生成的一致；
- 经过网桥和 DNAT 的往返时间和回环几乎一样，只多一两微秒；
- 从宿主机用 127.0.0.1 访问映射端口会经过用户态的 docker-proxy，往返时间从 17.5 µs 变成 42.3 µs。

## 7. 练习

1. 两个容器在同一个网桥上，它们之间的通信会经过 iptables 的 nat 表吗？会经过宿主机的路由吗？
2. 在宿主机上执行 `curl 127.0.0.1:18080` 和 `curl 宿主机IP:18080`，分别经过了什么？
3. 一台机器上运行着 200 个容器，每个容器每秒向外部发起 50 个新连接。按默认的 `nf_conntrack_max`（65536）和 TIME_WAIT 的跟踪时间（120 秒）估算，会出现什么问题？

<details>
<summary>点击查看答案</summary>

1. 不经过路由，也不经过 nat 表的转换：它们在同一个二层网络（同一个网段）上，包直接通过网桥交换，就像同一台交换机上的两台机器。（如果加载了 `br_netfilter` 并打开了 `net.bridge.bridge-nf-call-iptables`，经过网桥的包也会经过 iptables 的 filter 表，Kubernetes 通常会打开它。）

2. `127.0.0.1:18080`：Docker 的 OUTPUT 链规则排除了 127.0.0.0/8，内核不做 DNAT，连接到达监听 18080 的 docker-proxy，由它在用户态转发到容器。`宿主机IP:18080`：OUTPUT 链中目的地址是本机的包进入 DOCKER 链，DNAT 到容器的地址，内核直接转发，不经过 docker-proxy。

3. 每秒共 200 × 50 = 10000 个新连接，每个连接关闭后在连接跟踪表中还要保留约 120 秒，稳定时表中约有 120 万条记录，远远超过 65536。大约 6.5 秒后表就满了，之后新连接的包被丢弃（`nf_conntrack: table full, dropping packet`），表现为新连接间歇性超时。需要加大 `nf_conntrack_max`、缩短超时、让应用复用连接。

</details>
