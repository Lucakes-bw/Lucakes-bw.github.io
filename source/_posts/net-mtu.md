---
title: MTU 黑洞：小请求正常，大响应卡住
date: 2026-11-11 10:40:00
updated: 2026-11-11 10:40:00
categories:
  - 计算机网络
  - 网络排障
tags:
  - 网络
  - TCP
  - ICMP
order: 5
description: 连接能建立，ping 得通，小的请求都正常，只要响应稍微大一点就卡住不动，最后超时。这是 MTU 黑洞：路径中间有一段 MTU 较小的链路，大包被丢弃，而本该通知发送方的 ICMP 消息被防火墙挡住了。本文在实验网络中先看路径 MTU 发现正常工作时的过程（路由器丢了 10 个大包、回复了 10 个 ICMP），再挡住 ICMP 制造黑洞：1300 字节的响应正常，1 MB 的响应 10 秒内一个字节也没收到；最后用 MSS 钳制和 tcp_mtu_probing 两种办法解决，后者每个连接要多花 6 秒多。
top_group_list: true
disableNunjucks: true
viz: [net-diag]
---

> **本文目标**：理解路径 MTU 发现（PMTUD）的过程和它对 ICMP 的依赖；能根据「小包正常、大包卡住」的现象判断 MTU 黑洞；会用 tcpdump、`ip route get`、`ss -ti` 和第 1 篇的 bpftrace 确认问题；会用 MSS 钳制和 `tcp_mtu_probing` 解决；知道为什么不应该丢弃所有的 ICMP。
>
> **前置知识**：《数据链路层：MAC 与 ARP》（MTU）；《路由与 ICMP》；《TCP 连接管理》（MSS 选项）；本章第 1 篇（drop reason）。
>
> **实验环境**：同第 1 篇。把路由器 r 通往客户端的出口 `r-c` 的 MTU 改成 1400，模拟路径中间的一段隧道（VPN、GRE、云厂商的覆盖网络都会占掉一些字节），两端的网卡仍然是 1500。

## 1. 路径 MTU 发现

每条链路都有自己的 MTU：一个帧最多能装多少字节的 IP 包，以太网是 1500。TCP 握手时，双方在 SYN 中告诉对方自己的 **MSS**，按自己网卡的 MTU 计算：1500 − 20（IP 头）− 20（TCP 头）= 1460（《TCP 连接管理》）。但两端都不知道**中间**的链路有多大。

TCP 发出的 IP 包都带「不要分片」（DF）标志。路由器转发一个比下一跳 MTU 大的 DF 包时，不能分片，只能丢弃，同时给发送方回一个 ICMP「目的不可达：需要分片」，其中写着下一跳的 MTU。发送方收到后，把到这个目的地址的**路径 MTU** 记下来，之后用更小的包。这就是**路径 MTU 发现**（PMTUD）。

<div class="dsviz" data-viz="nd-pmtu"></div>

先看它正常工作的样子。`bulk blob` 在 s 上运行，收到「字节数」的请求后回复这么多字节；`bulk fetch` 在 c 上请求 1 MB：

```
请求 1000000 字节：收到 1000000 字节，0.01 秒；本端 MSS 1348
```

一切正常。在 r 上抓包（只看 ICMP 和大于 1400 字节的 TCP 包）：

```
IP 10.0.2.2.9200 > 10.0.1.2.40328: Flags [.], seq ...:..., length 1448
IP 10.0.2.1 > 10.0.2.2: ICMP 10.0.1.2 unreachable - need to frag (mtu 1400), length 556
IP 10.0.2.2.9200 > 10.0.1.2.40328: Flags [.], seq 1448:2896, ack 1, length 1448
IP 10.0.2.1 > 10.0.2.2: ICMP 10.0.1.2 unreachable - need to frag (mtu 1400), length 556
```

s 按 MSS 1448（1460 减去 12 字节的时间戳选项）发出的段，加上头部是 1500 字节，r 放不下，回复「需要分片，MTU 1400」。第 1 篇的 bpftrace 在 r 上看到：

```
@drops[71, ip_forward]: 10
```

71 是 `PKT_TOO_BIG`，在 `ip_forward` 中被丢弃。r 的计数器 `IpFragFails` 和 `IcmpOutDestUnreachs` 都是 10：丢了 10 个大包，发了 10 个 ICMP（第一批发出的 10 段都太大）。s 收到 ICMP 以后记住了路径 MTU：

```
$ ip -n s route get 10.0.1.2
10.0.1.2 via 10.0.2.1 dev s-r src 10.0.2.2 uid 0
    cache expires 599sec mtu 1400
```

这条记录 600 秒后过期，过期后再重新发现一次。整个过程只多花了一个往返时间，用户感觉不到。

顺便一提，如果小 MTU 是在**客户端自己**的网卡上（把 c 的网卡也改成 1400），c 握手时就会通告 MSS 1360，s 从一开始就不会发大包，根本用不到 PMTUD。问题只出在**两端都不知道**的中间链路上。

## 2. 挡住 ICMP：黑洞

很多防火墙「为了安全」丢弃所有的 ICMP。在 s 上加一条规则，再清掉刚才学到的路径 MTU：

```bash
ip netns exec s iptables -A INPUT -p icmp -j DROP
ip -n s route flush cache
```

```
请求 1000 字节：收到 1000 字节，0.00 秒；本端 MSS 1000
请求 1300 字节：收到 1300 字节，0.00 秒；本端 MSS 1300
请求 1000000 字节：收到 0 字节，10.01 秒（超时）；本端 MSS 536
```

1000、1300 字节的响应都正常（一个段，加上头部不超过 1400）；1 MB 的响应，10 秒内**一个字节都没收到**。这就是 **MTU 黑洞**：大包进去就消失了，没有任何错误。在 s 上看这个连接：

```
mss:1448 pmtu:1500 rto:3200 retrans:1/4 lost:11 unacked:11
```

s 仍然以为路径 MTU 是 1500，发出的 11 段全部丢失，正在第 4 次重传，RTO 已经翻倍到 3.2 秒，而且每次重传的都是同样大小的包，永远过不去。s 的防火墙规则计数器显示挡掉了 16 个 ICMP：r 每次都老老实实地发了「需要分片」，只是到不了 s。

<figure class="diagram">
<svg viewBox="0 0 700 230" role="img" aria-label="MTU 黑洞：s 发出 1500 字节的包，r 通往 c 的链路只有 1400，丢弃并回复 ICMP「需要分片」，但 s 的防火墙丢弃了 ICMP。s 以为是普通的丢包，一直重传同样大小的包；小包（握手、请求、1300 字节以内的响应）都能通过">
  <defs><marker id="bhl-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="bhl-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="60" width="140" height="50" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="90.0" y="81.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">客户端 c</text><text x="90.0" y="96.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">MTU 1500</text><rect x="280" y="60" width="140" height="50" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="350.0" y="81.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">路由器 r</text><text x="350.0" y="96.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">到 c 的出口 MTU 1400</text><rect x="540" y="60" width="140" height="50" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="610.0" y="81.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">服务器 s</text><text x="610.0" y="96.6" text-anchor="middle" style="fill:var(--muted);font-size:10px">防火墙丢弃 ICMP</text><line x1="538" y1="75" x2="422" y2="75" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#bhl-a)"/><text x="480.0" y="68.0" text-anchor="middle" style="fill:var(--link);font-size:11px">1500 字节，DF</text><line x1="278" y1="95" x2="162" y2="95" style="stroke:var(--accent);stroke-width:1.5" marker-end="url(#bhl-a)"/><text x="220.0" y="88.0" text-anchor="middle" style="fill:var(--accent);font-size:11px">1300 字节：通过</text><text x="350.0" y="130.0" text-anchor="middle" style="fill:var(--danger);font-size:11px">✕ 1500 &gt; 1400，丢弃</text><path d="M420,100 C470,150 520,150 560,112" style="fill:none;stroke:var(--danger);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#bhl-d)"/><text x="470.0" y="168.0" text-anchor="middle" style="fill:var(--danger);font-size:11px">ICMP 需要分片（MTU 1400）</text><text x="600.0" y="132.0" text-anchor="middle" style="fill:var(--danger);font-size:14px">✕</text><text x="20.0" y="200.0" text-anchor="start" style="fill:var(--fg);font-size:11px">s 的状态：pmtu 1500，retrans 1/4，lost 11；1 MB 的响应 10 秒内 0 字节</text><text x="20.0" y="220.0" text-anchor="start" style="fill:var(--muted);font-size:11px">修复：r 上做 MSS 钳制（握手时把 MSS 改成 1360），或者让 ICMP 通过</text>
  </g>
</svg>
</figure>

现象的特征非常鲜明：

- TCP 连接能建立，`ping` 也通（默认的 ping 包只有 84 字节）；
- 小的请求和响应正常，大的响应（下载文件、返回很长的列表、HTTPS 握手中服务器发出的证书链）卡住，最后超时；
- 往往只影响**某些客户端**（经过 VPN、某个运营商、某条隧道的那些）。

确认的办法：

```bash
ping -M do -s 1472 10.0.2.2     # 带 DF、总长 1500 的 ping：过不去
ping -M do -s 1372 10.0.2.2     # 总长 1400：能过去
tracepath 10.0.2.2              # 逐跳探测路径 MTU（同样依赖 ICMP）
```

在服务器上抓包，能看到同样大小的包反复重传，却从来看不到 ICMP。

## 3. 解决办法一：MSS 钳制

最常用的办法是在小 MTU 链路所在的路由器上做 **MSS 钳制**：转发 SYN 时，把其中的 MSS 改小，让两端从一开始就不发大包，不再需要 ICMP：

```bash
ip netns exec r iptables -t mangle -A FORWARD -p tcp --tcp-flags SYN,RST SYN \
    -j TCPMSS --clamp-mss-to-pmtu
```

`--clamp-mss-to-pmtu` 根据路由器上的路由自动计算：它同时考虑包的出口和返回方向的 MTU，c 发来的 SYN 来自 MTU 为 1400 的方向，MSS 被改成 1360。也可以明确地指定：

```bash
ip netns exec r iptables -t mangle -A FORWARD -i r-c -p tcp --tcp-flags SYN,RST SYN \
    -j TCPMSS --set-mss 1360
```

两种写法都试了，ICMP 仍然被挡着：

```
请求 1000000 字节：收到 1000000 字节，0.00 秒；本端 MSS 1348
```

规则的计数器显示改写了 1 个 SYN。家用路由器、VPN 网关、云厂商的隧道普遍会做 MSS 钳制，这是 TCP 的黑洞在实际中不那么常见的原因。它的局限是只对 TCP 有效，UDP（包括 QUIC）、ICMP 不受影响。

## 4. 解决办法二：tcp_mtu_probing

如果没法控制中间的路由器，可以在发送方开启**分组层路径 MTU 发现**（PLPMTUD，RFC 4821）：

```bash
sysctl -w net.ipv4.tcp_mtu_probing=1
```

值为 1 表示**检测到黑洞后才启用**：连续超时重传、又收不到 ICMP 时，内核怀疑遇到了黑洞，把 MSS 降到 `tcp_base_mss`（1024）重试，成功以后再逐步试探更大的值。去掉 MSS 钳制、仍然挡着 ICMP，连续请求两次 1 MB：

```
请求 1000000 字节：收到 1000000 字节，6.38 秒；本端 MSS 1347
请求 1000000 字节：收到 1000000 字节，6.40 秒；本端 MSS 1347
```

能完成了，但每次都要 6 秒多：先要经历几次超时重传（RTO 0.2、0.4、0.8、1.6、3.2 秒……）才能判断是黑洞，而且这个结论属于单个连接，不会像 ICMP 那样记在路由缓存中，**每个新连接都要重新经历一遍**。值为 2 表示一开始就使用较小的 MSS 并逐步探测，没有这个延迟，但每个连接起步都慢一些。

所以 `tcp_mtu_probing` 是一个兜底的办法，真正的修复是让 ICMP 通过，或者在隧道上做 MSS 钳制。

## 5. 不要丢弃所有的 ICMP

ICMP 不只是 `ping`。「需要分片」（IPv4 的类型 3 代码 4，IPv6 的 Packet Too Big）是 PMTUD 必需的，IPv6 的路由器根本不分片，完全依赖它。防火墙可以限制 ICMP 的速率，可以丢弃不需要的类型（比如重定向），但不要丢弃「目的不可达」这一类。

还有几个相关的坑：

- **IPv6、隧道叠加**：VXLAN、IPsec、WireGuard 各自占用几十到上百字节，一层套一层，MTU 很容易比 1500 小得多；
- **巨型帧**：数据中心内部用 MTU 9000，和外部互通的地方如果没处理好，同样会出现黑洞；
- **只有一个方向有问题**：上传正常、下载卡住（或者反过来），因为两个方向的路径和防火墙可能不同。

## 6. 常见错误

1. **ping 得通就认为网络没问题**。默认的 ping 包太小，用 `ping -M do -s 1472` 测大包。
2. **防火墙丢弃所有的 ICMP**。PMTUD 依赖「需要分片」消息。
3. **以为两端的 MTU 都是 1500 就没有问题**。问题在两端都看不到的中间链路。
4. **用 `tcp_mtu_probing` 代替真正的修复**。每个连接都要多花几秒。
5. **只对 TCP 做了 MSS 钳制，就以为解决了所有的问题**。UDP、QUIC 的大包仍然会被丢弃。

## 7. 小结

- 两端按自己的 MTU 计算 MSS，中间更小的链路要靠路径 MTU 发现：路由器丢弃大的 DF 包，回复 ICMP「需要分片」，发送方记住路径 MTU（实测缓存 600 秒）；
- 实测正常时：r 丢了 10 个包（`PKT_TOO_BIG`），发了 10 个 ICMP，传输只多了一个往返时间；
- ICMP 被挡住就成了黑洞：1300 字节的响应正常，1 MB 的响应 10 秒内收到 0 字节，发送方一直重传同样大小的包；
- 特征：能连接、能 ping、小请求正常、大响应卡住，常常只影响部分客户端；
- 解决：在路由器上做 MSS 钳制（实测立即恢复）；发送方开启 `tcp_mtu_probing=1`（实测每个连接多花 6 秒多）；根本上不要丢弃「需要分片」的 ICMP。

## 8. 练习

1. 用户反映：公司的 VPN 连上以后，能打开网站的首页，但下载文件总是卡在 0%；不用 VPN 一切正常。最可能的原因是什么？怎样确认？
2. MTU 1400 的链路上，带 DF 标志的 ping 最多能带多少字节的数据？（`ping -M do -s ?`）
3. 为什么 `tcp_mtu_probing=1` 时，每个新连接都要重新花 6 秒，而正常的 PMTUD 只有第一个连接多花一个往返时间？

<details>
<summary>点击查看答案</summary>

1. VPN 隧道占用了一部分字节，路径 MTU 小于 1500；首页的响应可能较小或者恰好分成了小包，下载文件时服务器发出满 1500 字节的包，在隧道入口被丢弃，而「需要分片」的 ICMP 被某处的防火墙挡住了：MTU 黑洞。确认：连上 VPN 后用 `ping -M do -s 1472 服务器` 测试，不通，再逐步减小 `-s` 找到能通过的最大值；在服务器上抓包，看到同样大小的包反复重传而没有 ICMP。解决：在 VPN 网关上做 MSS 钳制，或者减小隧道接口的 MTU（让客户端按小 MTU 计算 MSS）。

2. 1400 − 20（IP 头）− 8（ICMP 头）= 1372 字节，即 `ping -M do -s 1372`。

3. 正常的 PMTUD 中，ICMP 带来的路径 MTU 记录在内核的路由缓存中（按目的地址，600 秒），之后到同一个目的地址的所有连接都直接使用 1400，不需要再发现一次。`tcp_mtu_probing` 是每个连接自己根据超时推断出来的，结论只保存在这个连接中，新连接仍然从 1500 开始，要再经历几次超时重传才能判断出黑洞。

</details>
