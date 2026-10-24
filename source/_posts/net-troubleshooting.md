---
title: 网络排查工具箱
date: 2026-10-24 16:20:00
updated: 2026-10-24 16:20:00
categories:
  - 计算机网络
  - 应用层协议
tags:
  - 计算机网络
  - 排查
  - tcpdump
order: 5
description: 「连不上」是最常见的网络问题，原因却可能出在任何一层。本文把前面用过的工具串成一套由下往上的排查流程，然后在实验网络中依次制造 6 种故障：DNS 服务器失效、客户端没有默认路由、服务器没有回程路由、防火墙丢包、服务没有运行、网络很慢，记录每一种故障在 curl、nc、ping、traceroute、tcpdump 中的表现：同样是 curl 失败，退出码分别是 6、7、28，nc 的报错分别是 Network is unreachable、timed out 和 Connection refused。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：建立一套由下往上、逐层排查网络问题的思路；熟悉每一层对应的工具；能根据错误的表现（立即失败还是超时、哪种错误信息）判断问题出在哪一层。
>
> **前置知识**：本专栏前面的所有内容，本文是对它们的一次综合运用。
>
> **实验环境**：Ubuntu 24.04 容器；实验网络 c（10.0.1.2）→ r（路由器）→ s（10.0.2.2），s 上运行着第 3.2 篇的演示服务器，监听 8081 端口。

## 1. 排查的顺序

「网站打不开」时，不要一上来就怀疑服务器，而是按照分层的顺序，**由下往上**逐层确认：

| 步骤 | 问题 | 工具 | 本专栏中 |
|---|---|---|---|
| 1 | 本机的网卡、IP 地址、路由正常吗？ | `ip addr`、`ip route` | 第 1.3、1.4 篇 |
| 2 | 能到达网关吗？能到达目标 IP 吗？ | `ping`、`ip neigh`、`traceroute` | 第 1.2、1.4 篇 |
| 3 | 域名能解析吗？解析的结果对吗？ | `dig`、`getent hosts` | 第 3.1 篇 |
| 4 | 目标端口能连上吗？ | `nc -zv`、`curl -v`、`ss` | 第 2.2 篇 |
| 5 | 应用层的请求和响应正常吗？ | `curl -v`、`openssl s_client` | 第 3.2、3.3 篇 |
| 6 | 还是看不出来？直接看包 | `tcpdump`、`tshark` | 贯穿全专栏 |

每一步的结果都缩小了范围：ping 得通目标 IP，网络层就基本没问题；端口连得上，问题就在应用层。

**错误的表现方式本身就是线索**，最重要的是区分三种情况：

- **立即失败，并且有明确的原因**（`Network is unreachable`、`Connection refused`）：有人明确地回答了「不行」；
- **等待很久后超时**：包发出去了，但没有任何回应，通常是被丢弃了；
- **成功了但很慢**：要看时间花在了哪个阶段。

## 2. 六种故障

下面在实验网络中依次制造 6 种故障。每一种都先看 curl 的表现：

```bash
curl -s -m 3 -o /dev/null -w "%{http_code}" http://10.0.2.2:8081/; echo " exit=$?"
```

正常时输出 `200 exit=0`。

### 2.1 DNS 服务器失效

把容器的 DNS 服务器改成一个不存在的地址（实验后恢复了）：

```bash
echo "nameserver 10.255.255.1" > /etc/resolv.conf
curl -sS -o /dev/null http://example.com/
getent hosts example.com; echo "exit=$?"
ping -c 1 1.1.1.1
dig +short example.com @1.1.1.1
```

```
curl: (6) Could not resolve host: example.com
  用时 10.0 秒
  getent exit=2
64 bytes from 1.1.1.1: icmp_seq=1 ttl=63 time=1.97 ms
172.66.147.243
104.20.23.154
```

- curl 等了 **10 秒**才报错，退出码 **6**：无法解析主机名。glibc 的解析器向 DNS 服务器查询，没有回应，等待超时后重试，耗尽了时间；
- `ping 1.1.1.1` 正常：网络是通的，问题只在 DNS；
- `dig @1.1.1.1` 正常：换一个 DNS 服务器就能解析，说明是本机配置的 DNS 服务器出了问题，检查 `/etc/resolv.conf`。

**「用 IP 地址能访问，用域名不能」，就是 DNS 的问题。**

### 2.2 客户端没有默认路由

删掉 c 的默认路由：

```
  curl：000 exit=7
curl: (7) Failed to connect to 10.0.2.2 port 8081 after 0 ms: Couldn't connect to server
nc: connect to 10.0.2.2 port 8081 (tcp) failed: Network is unreachable
ping: connect: Network is unreachable
```

curl 只说「连不上」（退出码 7），用了 **0 毫秒**；nc 和 ping 给出了更具体的原因：`Network is unreachable`。和第 1.4 篇一样，本机的路由表中没有到达目标的路由，一个包都没有发出：

```bash
ip netns exec c ip route
```

```
10.0.1.0/24 dev eth0 proto kernel scope link src 10.0.1.2
```

只有直连的网络，没有 `default`。

### 2.3 服务器没有回程路由

恢复 c 的路由，删掉 s 的默认路由：s 能收到请求，却不知道该把回复发往哪里。

```
  curl：000 exit=28

--- 10.0.2.2 ping statistics ---
1 packets transmitted, 0 received, 100% packet loss, time 0ms
traceroute to 10.0.2.2 (10.0.2.2), 4 hops max, 60 byte packets
 1  10.0.1.1  0.097 ms
 2  *
 3  *
 4  *
```

- curl 的退出码变成了 **28**：超时。没有任何报错信息传回来；
- ping 也不通；traceroute 第 1 跳（路由器 r）正常，之后全是 `*`：r 把包转发给了 s，但 s 的回复（ICMP 端口不可达）发不回来。

单从客户端看，这和「s 关机了」几乎没有区别。要在 s 上抓包或者检查 s 的路由，才能发现是回程的问题。第 1.4 篇讲过：**去程和回程都需要路由**。

### 2.4 防火墙丢包

恢复路由，在 s 上用 iptables 丢弃发往 8081 端口的包：

```
  curl：000 exit=28
curl: (28) Connection timed out after 3005 milliseconds
nc: connect to 10.0.2.2 port 8081 (tcp) timed out: Operation now in progress
64 bytes from 10.0.2.2: icmp_seq=1 ttl=63 time=1.15 ms
```

curl 和 nc 都超时了，但 **ping 是通的**：主机在，网络通，只有这个端口不通。在 s 上抓包：

```
11:48:47.009034 IP 10.0.1.2.43208 > 10.0.2.2.8081: Flags [S], seq 4195604112, ...
11:48:48.035864 IP 10.0.1.2.43208 > 10.0.2.2.8081: Flags [S], seq 4195604112, ...
11:48:49.059074 IP 10.0.1.2.43208 > 10.0.2.2.8081: Flags [S], seq 4195604112, ...
```

SYN 到达了 s 的网卡（tcpdump 在防火墙之前抓包），并且每隔 1 秒重发一次（第 2.2 篇），但 s 从来没有回复。到达了却没有回复，问题就在 s 本机：防火墙，或者程序没有处理。查看 iptables 的规则，计数器显示已经丢弃了 9 个包：

```
Chain INPUT (policy ACCEPT 1 packets, 84 bytes)
 pkts bytes target     prot opt in     out     source               destination
    9   540 DROP       6    --  *      *       0.0.0.0/0            0.0.0.0/0            tcp dpt:8081
```

（如果抓包时 SYN 根本没有到达 s，问题就在路径上的某个设备。）

### 2.5 服务没有运行

去掉防火墙规则，停掉 s 上的演示服务器：

```
  curl：000 exit=7
curl: (7) Failed to connect to 10.0.2.2 port 8081 after 1 ms: Couldn't connect to server
nc: connect to 10.0.2.2 port 8081 (tcp) failed: Connection refused
  s 上监听 8081 的套接字：0
```

**1 毫秒**就失败了，nc 报告 `Connection refused`：s 的内核收到 SYN，发现没有程序在监听这个端口，回复了 RST（第 2.2 篇）。在 s 上用 `ss -ltn` 确认，确实没有程序监听 8081。立即被拒绝说明网络完全正常，问题是服务没有启动、崩溃了，或者监听在了别的端口、别的地址上（比如只监听了 127.0.0.1）。

和 2.4 节对比：同样是连不上，一个是**超时**（包被丢弃），一个是**立即拒绝**（收到了 RST）。

### 2.6 网络很慢

恢复服务，在路由器 r 上加入 300 毫秒的延迟。curl 能成功，但很慢。用 `-w` 把时间按阶段拆开：

```bash
curl -s -o /dev/null -w "DNS %{time_namelookup}  连接 %{time_connect}  首字节 %{time_starttransfer}  总计 %{time_total}\n" http://10.0.2.2:8081/
ping -c 3 10.0.2.2
```

```
  DNS 0.000020  连接 0.302432  首字节 0.608681  总计 0.911597
rtt min/avg/max/mdev = 300.176/300.461/300.764/0.240 ms
```

- 用 IP 地址访问，DNS 不花时间；
- 建立连接用了 0.3 秒，即一个往返时间（TCP 握手）；
- 从连接建立到收到第一个字节又用了 0.3 秒：发出请求、收到响应，又是一个往返；
- 总计 0.91 秒：演示服务器发完响应后关闭了连接，收到这个关闭信号又花了一个往返。

ping 显示往返时间是 300 毫秒，所有的时间都可以用往返时间解释，问题就是网络延迟本身，而不是服务器处理得慢。如果「首字节」减去「连接」远大于一个往返时间，那就是服务器处理请求慢了。

## 3. 汇总

| 故障 | curl 退出码 | 报错 | 用时 | ping 目标 IP | 关键证据 |
|---|---|---|---|---|---|
| DNS 服务器失效 | 6 | Could not resolve host | 10 秒 | 通 | `dig @其他服务器` 正常 |
| 客户端没有路由 | 7 | Network is unreachable | 0 毫秒 | 不通（本机报错） | `ip route` 中没有 default |
| 服务器没有回程路由 | 28 | 超时 | 超时 | 不通 | traceroute 过了某一跳之后全是 `*` |
| 防火墙丢包 | 28 | 超时 | 超时 | **通** | 对方抓包有 SYN，没有回复 |
| 服务没有运行 | 7 | Connection refused | 1 毫秒 | 通 | 对方 `ss -ltn` 中没有这个端口 |
| 网络很慢 | 0 | — | 0.91 秒 | 往返 300 毫秒 | `curl -w` 的各阶段都是往返时间的倍数 |

## 4. 常用命令速查

| 工具 | 常用写法 | 作用 |
|---|---|---|
| `ip addr` / `ip route` | `ip route get 1.1.1.1` | 地址、路由，以及去某个地址实际走哪条路 |
| `ping` | `ping -c 4 -W 1 地址` | 是否可达、往返时间、丢包率 |
| `traceroute` | `traceroute -n 地址`；`-T -p 443` 用 TCP 探测 | 路径上的每一跳 |
| `dig` | `dig 域名`、`dig @服务器 域名`、`dig +trace 域名` | DNS 查询 |
| `getent` | `getent hosts 域名` | 程序实际得到的解析结果（含 `/etc/hosts`） |
| `nc` | `nc -zv -w 3 地址 端口` | 端口能否连通，报错信息比 curl 具体 |
| `curl` | `-v`、`-I`、`-w "%{time_connect} %{time_starttransfer}"`、`--resolve` | HTTP(S) 请求、分阶段计时 |
| `openssl s_client` | `openssl s_client -connect 主机:443 -servername 主机` | TLS 握手和证书 |
| `ss` | `ss -tlnp`（监听）、`ss -tan`（连接）、`ss -ti`（TCP 详情） | 本机的套接字 |
| `tcpdump` | `tcpdump -i eth0 -nn "host 地址 and port 端口"` | 看包，最终的手段 |
| `nstat` | `nstat -az TcpRetransSegs` | 内核的协议计数器 |

## 5. 常见误区

1. **一开始就抓包**：先用 ping、nc、curl 定位到大致的层次，再抓包，效率高得多。
2. **只在客户端排查**：回程路由、防火墙、服务没有运行这些问题，在客户端看来都差不多，要到服务器上确认。
3. **ping 不通就认为主机不在**：很多服务器和防火墙会丢弃 ICMP，ping 不通而端口能连上的情况很常见。
4. **忽略「用时」**：0 毫秒失败、1 毫秒失败、3 秒超时、10 秒超时，背后的原因完全不同。
5. **用 dig 验证程序的解析**：dig 不读 `/etc/hosts`，要用 `getent`。

## 6. 小结

- 排查网络问题由下往上：本机配置 → 可达性 → DNS → 端口 → 应用 → 抓包
- 区分立即失败（有人明确拒绝：无路由、RST）、超时（包被丢弃）、成功但慢（看各阶段的时间）
- 实测 6 种故障：DNS 失效（curl 6，10 秒）、无路由（7，0 毫秒，Network is unreachable）、无回程路由（28，超时）、防火墙（28，超时但 ping 通）、服务未运行（7，1 毫秒，Connection refused）、延迟（各阶段都是往返时间的倍数）
- 对方抓包是最有力的证据：包到了没有、回复了没有

## 7. 练习

1. 服务器上的程序只监听了 `127.0.0.1:8081`，从另一台机器访问会出现上面哪种情况？怎样确认？
2. curl 访问一个 HTTPS 网站时报 `SSL certificate problem: certificate has expired`，但同事的电脑上正常。可能是什么原因？
3. 一个网页在公司内网打开很慢，`curl -w` 显示 DNS 用了 5.01 秒，其他阶段都很快。可能是什么原因？怎样确认？

<details>
<summary>点击查看答案</summary>

1. 和 2.5 节相同：`Connection refused`。程序只在回环地址上监听，从外部发往 10.0.2.2:8081 的 SYN 找不到监听的套接字，内核回复 RST。在服务器上用 `ss -ltn` 查看，Local Address 一列是 `127.0.0.1` 而不是 `0.0.0.0` 或 `10.0.2.2`。实测（用 nc 在 s 上监听 127.0.0.1:8082，从 c 连接）：

   ```
   nc: connect to 10.0.2.2 port 8082 (tcp) failed: Connection refused
   State  Recv-Q Send-Q Local Address:Port Peer Address:PortProcess
   LISTEN 0      1          127.0.0.1:8082      0.0.0.0:*
   ```
2. 如果证书真的过期了，同事那里也会报错。只有自己报错，很可能是**本机的时间不对**：时间被设到了证书的有效期之后。用 `date` 检查本机的时间。（也可能是本机的 CA 证书太旧，但那通常报的是 `unable to get local issuer certificate`。）
3. 5 秒左右是 glibc 解析器查询超时后换下一个 DNS 服务器的典型时间：`/etc/resolv.conf` 中的第一个 DNS 服务器没有回应，等了 5 秒才去问第二个。用 `dig @第一个服务器 域名` 和 `dig @第二个服务器 域名` 分别测试，就能找到那个失效的服务器。（另一个常见原因是 IPv6 的 AAAA 查询被某些设备丢弃，可以用 `getent ahostsv4` 对比。）

</details>
