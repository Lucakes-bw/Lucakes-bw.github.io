---
title: DNS
date: 2026-10-24 16:00:00
updated: 2026-10-24 16:00:00
categories:
  - 计算机网络
  - 应用层协议
tags:
  - 计算机网络
  - DNS
order: 1
description: 访问一个网站之前，要先把域名换成 IP 地址，这就是 DNS 的工作。本文用 dig 查询 example.com，看懂回答中的每一部分；用 dig +trace 从根服务器开始一步步查到答案，四次查询分别耗时 65、36、218、66 毫秒；查询 A、AAAA、NS、MX、TXT、CNAME 等记录；抓包看到一次查询只有 94 字节的 UDP 请求和 114 字节的回答；在 /etc/hosts 中加一行，curl 就去连接一个不存在的地址了。配有递归解析和缓存的逐步演示。
top_group_list: true
disableNunjucks: true
viz: [net-dns]
---

> **本文目标**：理解 DNS 的作用和域名的层次结构；理解递归查询和迭代查询，以及缓存和 TTL；会用 dig 查询各种记录、追踪解析过程；知道 `/etc/hosts` 和 `/etc/resolv.conf` 的作用；能用抓包看懂一次 DNS 查询。
>
> **前置知识**：第 2.1 篇的 UDP；第 1.1 篇的抓包。
>
> **实验环境**：Ubuntu 24.04 容器，dig 9.18（`dnsutils` 软件包）。本章的实验访问真实的互联网：容器的 DNS 服务器是 1.1.1.1 和 8.8.8.8，查询的域名是 example.com 等。

## 1. 为什么需要 DNS

第 1.1 篇抓包时，curl 在发出 HTTP 请求之前，先查询了 example.com 的 IP 地址。网络层只认识 IP 地址，人却更容易记住名字。**DNS**（Domain Name System，域名系统）就是把**域名**翻译成 IP 地址的系统。

全世界有几亿个域名，记录还在不断变化，不可能放在一台服务器上。DNS 把域名组织成一棵树，分层管理：

```
                    .（根）
          ┌──────────┼──────────┐
         com        org         cn
    ┌─────┼─────┐                │
 example github …                com
    │                             │
   www                          …
```

域名从右往左读：`www.example.com` 是根之下、`com` 之下、`example` 之下的 `www`。每一层只负责自己的直接下级：根服务器知道谁负责 `com`，`com` 的服务器知道谁负责 `example.com`，`example.com` 的服务器（**权威服务器**）才知道 `www.example.com` 的地址。

## 2. 用 dig 查询

```bash
dig example.com
```

```
;; ->>HEADER<<- opcode: QUERY, status: NOERROR, id: 63902
;; flags: qr rd ra ad; QUERY: 1, ANSWER: 2, AUTHORITY: 0, ADDITIONAL: 1

;; OPT PSEUDOSECTION:
; EDNS: version: 0, flags:; udp: 1232
;; QUESTION SECTION:
;example.com.			IN	A

;; ANSWER SECTION:
example.com.		205	IN	A	172.66.147.243
example.com.		205	IN	A	104.20.23.154

;; Query time: 65 msec
;; SERVER: 1.1.1.1#53(1.1.1.1) (UDP)
;; WHEN: Sat Oct 24 08:03:16 UTC 2026
;; MSG SIZE  rcvd: 72
```

- `status: NOERROR`：查询成功。域名不存在时是 `NXDOMAIN`；
- `flags`：`qr` 表示这是回答，`rd`（递归期望）是我们请求「帮我查到底」，`ra`（递归可用）是服务器表示「我可以」；
- **QUESTION**：问的是 example.com 的 **A 记录**（IPv4 地址），`IN` 表示互联网类；
- **ANSWER**：两个 IP 地址。第二列 **205 是 TTL**：这条记录还可以被缓存 205 秒；
- `SERVER: 1.1.1.1#53 (UDP)`：回答来自 1.1.1.1（Cloudflare 的公共 DNS），用的是 UDP 的 53 端口；`Query time: 65 msec`。

一个域名可以有多个 IP 地址，客户端通常依次尝试，也可以用来分担负载。

### 2.1 解析器和 TTL

1.1.1.1 并不是 example.com 的权威服务器，它是一个**递归解析器**：替客户端去各级服务器查询，并把结果**缓存**起来。几秒后连续查询几次：

```bash
dig +noall +answer example.com                 # 1.1.1.1
dig +noall +answer example.com @8.8.8.8         # 指定用 8.8.8.8 查询
```

```
example.com.		18	IN	A	104.20.23.154
example.com.		18	IN	A	172.66.147.243
example.com.		300	IN	A	172.66.147.243
example.com.		300	IN	A	104.20.23.154
```

1.1.1.1 的缓存中这条记录还剩 18 秒（刚才还是 205，它的背后有很多台服务器，每台的缓存各不相同）；8.8.8.8 回答的 TTL 是 300，说明它刚刚从权威服务器取来，这正是权威服务器设定的有效期。缓存过期后，解析器要重新查询。

TTL 决定了修改 DNS 记录后多久才能生效：把一个网站迁到新的 IP 地址，旧的记录还会在各地的缓存中保留最多 TTL 秒。所以迁移之前，通常先把 TTL 调小。

## 3. 从根开始查：dig +trace

`dig +trace` 不使用递归解析器，而是自己从根服务器开始，模拟解析器的工作：

```bash
dig +trace +nodnssec example.com
```

```
.			508405	IN	NS	a.root-servers.net.
...（共 13 个根服务器：a 到 m）
;; Received 239 bytes from 1.1.1.1#53(1.1.1.1) in 65 ms

com.			172800	IN	NS	c.gtld-servers.net.
...（共 13 个 com 服务器）
;; Received 864 bytes from 202.12.27.33#53(m.root-servers.net) in 36 ms

example.com.		172800	IN	NS	hera.ns.cloudflare.com.
example.com.		172800	IN	NS	elliott.ns.cloudflare.com.
;; Received 359 bytes from 192.43.172.30#53(i.gtld-servers.net) in 218 ms

example.com.		300	IN	A	172.66.147.243
example.com.		300	IN	A	104.20.23.154
;; Received 72 bytes from 108.162.195.228#53(elliott.ns.cloudflare.com) in 66 ms
```

1. 先向 1.1.1.1 要来根服务器的列表（`a.root-servers.net` 到 `m.root-servers.net`）；
2. 问根服务器 m：它不知道 example.com 的地址，但回答「`com` 归这 13 台服务器管」（NS 记录），这叫**转介**；
3. 问 com 服务器 i：回答「`example.com` 归 `hera.ns.cloudflare.com` 和 `elliott.ns.cloudflare.com` 管」；
4. 问 example.com 的权威服务器 elliott：得到最终的 A 记录，TTL 300。

四次查询共用了约 0.4 秒，其中问 com 服务器那一次就用了 218 毫秒。正是因为有缓存，平时的查询才不用每次都走一遍：根和 com 服务器的 NS 记录 TTL 是 172800 秒（2 天），在解析器中一直有效，大部分查询直接从「问权威服务器」甚至「缓存中已有答案」开始。

下面的演示展示了这个过程，以及缓存怎样减少查询的次数：

<div class="dsviz" data-viz="dns-resolve"></div>

**递归查询和迭代查询**：客户端对解析器说「请帮我查到底」，这是**递归查询**；解析器对各级服务器则是「你知道就告诉我，不知道就告诉我该问谁」，这是**迭代查询**。

## 4. 记录类型

DNS 不只保存 IP 地址。example.com 的其他记录：

```bash
for t in NS MX TXT AAAA SOA; do dig +noall +answer example.com $t; done
dig +noall +answer www.github.com
```

```
example.com.		75946	IN	NS	hera.ns.cloudflare.com.
example.com.		75946	IN	NS	elliott.ns.cloudflare.com.
example.com.		172	IN	MX	0 .
example.com.		20	IN	TXT	"v=spf1 -all"
example.com.		20	IN	TXT	"_k2n1y4vw3qtb4skdx9e7dxt97qrmmq9"
example.com.		161	IN	AAAA	2606:4700:10::6814:179a
example.com.		161	IN	AAAA	2606:4700:10::ac42:93f3
example.com.		1800	IN	SOA	elliott.ns.cloudflare.com. dns.cloudflare.com. 2418004112 10000 2400 604800 1800
www.github.com.		3463	IN	CNAME	github.com.
github.com.		59	IN	A	20.205.243.166
```

| 类型 | 含义 | 例子中 |
|---|---|---|
| **A** | IPv4 地址 | 172.66.147.243 |
| **AAAA** | IPv6 地址 | 2606:4700:10::6814:179a |
| **NS** | 这个域由哪些权威服务器负责 | Cloudflare 的两台服务器 |
| **CNAME** | 别名：这个名字其实是另一个名字 | www.github.com 是 github.com 的别名，解析器接着查 github.com 的 A 记录 |
| **MX** | 接收这个域的邮件的服务器 | `0 .` 表示这个域不接收邮件 |
| **TXT** | 任意文本，常用于验证域名所有权、邮件防伪（SPF） | `v=spf1 -all`：任何服务器都不能代表它发信 |
| **SOA** | 这个域的管理信息：主服务器、管理员邮箱、序列号、各种计时器 | |

`dig -x 1.1.1.1` 可以做**反向查询**：由 IP 地址查域名（PTR 记录）。

## 5. 一次查询的报文

DNS 查询通常用 UDP，一问一答各一个数据报：

```bash
tcpdump -i eth0 -nn -w dns.pcap "udp port 53" &
dig +noall +answer example.com A
```

```
08:03:49.352623 IP 172.17.0.3.35545 > 1.1.1.1.53: 39999+ [1au] A? example.com. (52)
08:03:49.418701 IP 1.1.1.1.53 > 172.17.0.3.35545: 39999$ 2/0/1 A 172.66.147.243, A 104.20.23.154 (72)
```

- 查询：`39999` 是**事务 ID**，回答中带着同样的 ID，客户端据此把回答和问题对应起来；`+` 表示请求递归；`A?` 是问题；
- 回答：`2/0/1` 是回答、授权、附加三部分各有几条记录；
- 整个查询帧 94 字节，回答帧 114 字节（DNS 数据本身 52 和 72 字节）。

tshark 解析出的回答（节选）：

```
Domain Name System (response)
    Transaction ID: 0x9c3f
    Flags: 0x81a0 Standard query response, No error
        1... .... .... .... = Response: Message is a response
        .... .0.. .... .... = Authoritative: Server is not an authority for domain
        .... ...1 .... .... = Recursion desired: Do query recursively
        .... .... 1... .... = Recursion available: Server can do recursive queries
    Answers
        example.com: type A, class IN, addr 172.66.147.243
            Time to live: 7 (7 seconds)
```

`Authoritative: Server is not an authority`：这个回答来自解析器的缓存，不是权威服务器给的。0x9c3f 就是十进制的 39999。

为什么用 UDP？一问一答，用 TCP 还要先花一个往返时间握手；丢了，客户端过一会儿重新问就是了（第 2.1 篇）。回答超过 UDP 的大小限制时（上面 EDNS 的 `udp: 1232` 是客户端能接收的最大字节数），服务器会在回答中设置 `Truncated` 标志，客户端改用 TCP 重新查询。

> **DNS 的安全问题**：传统的 DNS 查询是明文的，途中的任何人都能看到你访问了哪些网站，甚至伪造回答。**DoH**（DNS over HTTPS）和 **DoT**（DNS over TLS）把查询放在加密的连接中；**DNSSEC** 则用签名保证回答没有被篡改（上面 `flags` 中的 `ad` 表示解析器验证过签名）。

## 6. 本机的解析：/etc/hosts 和 /etc/resolv.conf

程序调用 `getaddrinfo` 解析域名时，glibc 按 `/etc/nsswitch.conf` 中的顺序查找：

```bash
grep hosts /etc/nsswitch.conf
cat /etc/resolv.conf
```

```
hosts:          files dns
nameserver 1.1.1.1
nameserver 8.8.8.8
```

先查 `files`（即 `/etc/hosts`），再查 `dns`（`/etc/resolv.conf` 中的服务器）。在 `/etc/hosts` 中给 example.com 指定一个不存在的地址：

```bash
echo "10.9.8.7 example.com" >> /etc/hosts
getent ahostsv4 example.com
dig +short example.com
curl -v -m 3 -o /dev/null http://example.com/
```

```
10.9.8.7        STREAM example.com
10.9.8.7        DGRAM
172.66.147.243
104.20.23.154
*   Trying 10.9.8.7:80...
* Connection timed out after 3007 milliseconds
```

- `getent`（使用和普通程序相同的解析方式）返回了 10.9.8.7，curl 也去连接它，3 秒后超时；
- `dig` 却仍然返回真实的地址：**dig 直接向 DNS 服务器查询，不读 `/etc/hosts`**。所以用 dig 排查「程序为什么连到了错误的地址」时，要记得 `/etc/hosts` 也可能是原因，用 `getent` 才能看到程序实际得到的结果。

实验后恢复了原来的 `/etc/hosts`。`/etc/hosts` 常用于开发时把域名临时指向本机或测试服务器。

在普通的 Ubuntu 桌面上，`/etc/resolv.conf` 中通常是 `nameserver 127.0.0.53`：本机运行着 systemd-resolved，它自己也有一层缓存，再转发给真正的 DNS 服务器。`resolvectl query 域名` 可以查看它的结果。浏览器通常也有自己的 DNS 缓存。

## 7. 常见问题

1. **修改了 DNS 记录却迟迟不生效**：旧记录还在各级缓存中，要等 TTL 过期。
2. **用 dig 查询正常，程序却连到了别的地址**：检查 `/etc/hosts`，用 `getent hosts` 查看程序实际的结果。
3. **「DNS 污染」或者解析很慢**：换一个解析器对比，`dig @8.8.8.8 域名`、`dig @1.1.1.1 域名`。
4. **把 CNAME 当作重定向**：CNAME 只是 DNS 中的别名，浏览器地址栏中的名字不会变；HTTP 的重定向是另一回事（下一篇）。
5. **以为 DNS 只用 UDP**：回答太大时会用 TCP，防火墙要同时放行 UDP 和 TCP 的 53 端口。

## 8. 小结

- DNS 把域名翻译成 IP 地址；域名是分层的，根、顶级域、权威服务器各管一层
- 客户端向递归解析器发出递归查询，解析器从根开始迭代查询；实测 `dig +trace` 走了 4 步，共约 0.4 秒
- 记录带有 TTL，解析器和本机都会缓存；实测不同解析器缓存中剩余的 TTL 各不相同
- 常用记录：A、AAAA、NS、CNAME、MX、TXT、SOA
- 查询通常用 UDP 53 端口，一问一答，用事务 ID 对应；实测一次查询只有 94 字节的请求和 114 字节的回答
- 本机先查 `/etc/hosts` 再查 DNS；dig 不读 `/etc/hosts`，`getent` 读

## 9. 练习

1. 用 dig 查询 `www.example.com` 和一个不存在的域名，回答的状态分别是什么？
2. 为什么根服务器只有 13 个名字（a 到 m），它们能承受全世界的查询吗？
3. 一个网站的 A 记录 TTL 是 86400，要把网站迁到新的服务器上，怎样做才能让用户尽快访问到新的服务器？

<details>
<summary>点击查看答案</summary>

1. 实测：

   ```
   ;; ->>HEADER<<- opcode: QUERY, status: NOERROR, id: 43702
   www.example.com.	1	IN	A	172.66.147.243
   www.example.com.	1	IN	A	104.20.23.154
   ```

   `www.example.com` 存在，`NOERROR`，地址和 example.com 相同。查询一个不存在的 .com 域名 `nosuch-domain-xyz-2026.com`：

   ```
   ;; ->>HEADER<<- opcode: QUERY, status: NXDOMAIN, id: 31024
   ```

   `NXDOMAIN` 表示「这个域名不存在」。有意思的是，查询 example.com 下一个不存在的名字 `nosuch-xyz-123abc.example.com`，状态却是 `NOERROR`，只是没有回答，授权部分给出了 example.com 的 SOA 记录。example.com 的权威服务器（Cloudflare）对不存在的名字采用了这种回答方式，含义是「这个名字下没有你要的记录」。所以判断一个名字是否存在，不能只看状态码，还要看有没有回答。
2. 13 个是「名字」，不是 13 台机器。每个根服务器的地址都使用**任播**（anycast）：同一个 IP 地址在全世界很多地方同时宣告，路由会把查询送到最近的那一台。实际的根服务器有一千多台。何况大部分查询根本到不了根服务器：com 等顶级域的 NS 记录在解析器中缓存 2 天。
3. 提前至少 86400 秒（一天），把 TTL 改小，比如 60 秒；等旧的 TTL 过期，所有缓存中的记录都只能保留 60 秒了，再修改 A 记录，用户最多 60 秒后就能访问到新的服务器；迁移完成后，再把 TTL 改回较大的值，减少查询的压力。

</details>
