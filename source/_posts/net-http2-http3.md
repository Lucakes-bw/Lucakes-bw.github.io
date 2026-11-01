---
title: HTTP/2 与 HTTP/3
date: 2026-10-24 16:15:00
updated: 2026-10-24 16:15:00
categories:
  - 计算机网络
  - 应用层协议
tags:
  - 计算机网络
  - HTTP/2
  - HTTP/3
order: 4
description: HTTP/1.1 的一个连接同时只能处理一个请求。HTTP/2 改成二进制的帧，在一个连接上同时传送多个请求。本文用 TLS 密钥日志解密 curl 和 example.com 之间的 HTTP/2 连接，看到 SETTINGS、HEADERS、DATA 等帧和流编号，第二个请求的头部被压缩到只有 13 字节；同时发出 20 个请求，HTTP/1.1 开了 20 个连接用时 1.4 秒，HTTP/2 只用 1 个连接、0.36 秒。最后介绍基于 UDP 的 HTTP/3 和 QUIC。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 HTTP/1.1 在性能上的局限；理解 HTTP/2 的二进制帧、流和多路复用；理解头部压缩；会用 TLS 密钥日志解密并分析 HTTPS 的流量；理解 HTTP/3 和 QUIC 要解决的问题。
>
> **前置知识**：前两篇的 HTTP/1.1 和 TLS；第 2.3 篇的丢包与重传。
>
> **实验环境**：Ubuntu 24.04 容器，curl 8.5（使用 nghttp2 1.59 支持 HTTP/2），tshark 4.2。

## 1. HTTP/1.1 的局限

第 3.2 篇看到，HTTP/1.1 用持久连接省去了重复的握手，但一个连接上的请求只能**一个接一个**：发出请求，等响应全部收完，才能发下一个。一个网页有几十个资源，浏览器只好同时打开多个连接（每个网站通常最多 6 个），每个连接都要做 TCP 和 TLS 握手，各自从慢启动开始（第 2.4 篇）。

另外，每个请求都带着几乎一样的头部（`Host`、`User-Agent`、`Cookie`……），以文本形式一遍遍地重复发送。

## 2. HTTP/2：二进制帧和多路复用

HTTP/2（2015 年）保持了 HTTP 的语义（方法、状态码、头部的含义都不变），只改变了传送的方式：

- **二进制帧**：消息被切成一个个**帧**，每个帧有固定格式的头部，记录类型、长度、属于哪个**流**；
- **流**：每个请求/响应对是一个流，有自己的编号。客户端发起的流编号是奇数：1、3、5……；
- **多路复用**：多个流的帧可以在**一个** TCP 连接上交错发送，谁先准备好谁先发，不用排队；
- **头部压缩**（HPACK）：双方各自维护一张表，已经发过的头部，下次只需要发一个编号。

HTTP/2 在实践中总是和 TLS 一起使用。上一篇抓包时看到，curl 在 Client Hello 的 ALPN 中表示支持 `h2`，服务器选择了它，所以那次访问用的其实是 HTTP/2：

```bash
curl -sI --http2 https://example.com/ | head -1
curl -sI --http1.1 https://example.com/ | head -1
```

```
HTTP/2 200
HTTP/1.1 200 OK
```

## 3. 解密 HTTP/2 的流量

HTTP/2 的帧在 TLS 中加密，直接抓包看不到。但 curl（以及 Chrome、Firefox 等）支持一个调试功能：设置环境变量 `SSLKEYLOGFILE`，它会把每个 TLS 连接的密钥写进这个文件，tshark 有了密钥就能解密。在**自己的机器上**调试 HTTPS 时，这是最常用的方法：

```bash
tcpdump -i eth0 -nn -U -w h2.pcap "tcp port 443" &
SSLKEYLOGFILE=/lab/keys.log curl -s --http2 -o /dev/null -o /dev/null https://example.com/ https://example.com/?second
tshark -r h2.pcap -o tls.keylog_file:/lab/keys.log -Y http2 \
    -T fields -e frame.number -e ip.src -e http2.streamid -e http2.type -e http2.length -e _ws.col.Info
```

curl 在同一个连接上依次请求了两个地址：

```
11	172.17.0.3	0,0	4,8	18,4	Magic, SETTINGS[0], WINDOW_UPDATE[0]
13	172.17.0.3	1	1	28	HEADERS[1]: GET /
16	104.20.23.154	0,0,0	4,8,4	18,4,0	SETTINGS[0], WINDOW_UPDATE[0], SETTINGS[0]
17	172.17.0.3	0	4	0	SETTINGS[0]
19	104.20.23.154	1,1,1	1,0,0	160,577,0	DATA[1] (text/html)
20	172.17.0.3	3	1	13	HEADERS[3]: GET /?second
22	104.20.23.154	3,3	1,0	59,577	DATA[3]
23	104.20.23.154	3	0	0	DATA[3] (text/html)
```

各列依次是：帧的序号、发送方、流编号、帧类型（0 是 DATA，1 是 HEADERS，4 是 SETTINGS，8 是 WINDOW_UPDATE）、帧的长度。一个 TCP 报文中可以装多个帧，所以有些行有多个值。

1. 客户端先发送一个固定的**连接前言**（`Magic`），然后是 **SETTINGS** 帧（自己的参数，如允许同时打开多少个流）和 **WINDOW_UPDATE**（HTTP/2 自己的流量控制窗口）。这些都属于流 0，即整个连接；
2. 第一个请求在**流 1** 上：一个 **HEADERS** 帧，28 字节；
3. 服务器也发送自己的 SETTINGS，然后在流 1 上回复：一个 160 字节的 HEADERS 帧（响应的头部）、一个 577 字节的 **DATA** 帧（网页，正好 577 字节）、一个长度为 0 的 DATA 帧（带有「流结束」标志）；
4. 第二个请求在**流 3** 上，HEADERS 帧只有 **13 字节**；它的响应头部也只有 **59 字节**。

### 3.1 头部压缩

同样是 GET 请求，第一个的头部 28 字节，第二个只有 13 字节；响应的头部从 160 字节降到了 59 字节。这就是 **HPACK** 的效果：

- 常用的头部（如 `:method: GET`）在一张预先定义的**静态表**中，只需要发一个编号；
- 第一次发送的头部（如 `user-agent: curl/8.5.0`）会被加入**动态表**，之后再出现时也只发一个编号；
- 头部的值用哈夫曼编码压缩（数据结构专栏的《哈夫曼树与哈夫曼编码》）。

作为对比，第 1.1 篇中同样的请求用 HTTP/1.1 发送，文本的头部是 74 字节。真实的浏览器请求带着很长的 Cookie 和各种头部，头部压缩节省的就更多了。

## 4. 多路复用的效果

同时请求 example.com 的 20 个地址（`?n=1` 到 `?n=20`），分别用 HTTP/1.1 和 HTTP/2，各运行 3 次：

```bash
curl -s --http1.1 --parallel --parallel-max 20 ... https://example.com/?n=1 ... https://example.com/?n=20
curl -s --http2   --parallel --parallel-max 20 ... https://example.com/?n=1 ... https://example.com/?n=20
```

```
--http1.1 第 1 次：新建连接 20 个，用时 2.49 秒
--http1.1 第 2 次：新建连接 20 个，用时 1.40 秒
--http1.1 第 3 次：新建连接 20 个，用时 1.40 秒
--http2 第 1 次：新建连接 1 个，用时 0.36 秒
--http2 第 2 次：新建连接 1 个，用时 0.36 秒
--http2 第 3 次：新建连接 1 个，用时 0.36 秒
```

再加上一种：HTTP/1.1，不并行，20 个请求依次在一个连接上发送：

```
HTTP/1.1 顺序复用一个连接 第 1 次：新建连接 1 个，用时 2.11 秒
HTTP/1.1 顺序复用一个连接 第 2 次：新建连接 1 个，用时 1.69 秒
```

| 方式 | 连接数 | 用时 |
|---|---|---|
| HTTP/1.1，一个连接，依次请求 | 1 | 1.69～2.11 秒 |
| HTTP/1.1，20 个连接并行 | 20 | 1.40～2.49 秒 |
| HTTP/2，一个连接，多路复用 | 1 | 0.36 秒 |

<figure class="diagram">
<svg viewBox="0 0 700 210" role="img" aria-label="请求 example.com 的 20 个地址的示意时间线：HTTP/1.1 在一个连接上依次请求，每个请求一个往返，共 1.69 到 2.11 秒；开 20 个连接并行，每个连接都要握手，共 1.40 到 2.49 秒；HTTP/2 一次握手后 20 个请求同时发出，共 0.36 秒">
  <defs></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="178" y="34" text-anchor="end" style="fill:var(--fg);font-size:12px">HTTP/1.1 一个连接依次请求</text><rect x="190.0" y="24" width="44.0" height="12" style="fill:var(--muted);opacity:.75"/><rect x="234.0" y="24" width="13.2" height="12" style="fill:var(--link);opacity:.75"/><rect x="250.5" y="24" width="13.2" height="12" style="fill:var(--link);opacity:.75"/><rect x="267.0" y="24" width="13.2" height="12" style="fill:var(--link);opacity:.75"/><rect x="283.5" y="24" width="13.2" height="12" style="fill:var(--link);opacity:.75"/><rect x="300.0" y="24" width="13.2" height="12" style="fill:var(--link);opacity:.75"/><rect x="316.5" y="24" width="13.2" height="12" style="fill:var(--link);opacity:.75"/><rect x="333.0" y="24" width="13.2" height="12" style="fill:var(--link);opacity:.75"/><rect x="349.5" y="24" width="13.2" height="12" style="fill:var(--link);opacity:.75"/><rect x="366.0" y="24" width="13.2" height="12" style="fill:var(--link);opacity:.75"/><rect x="382.5" y="24" width="13.2" height="12" style="fill:var(--link);opacity:.75"/><rect x="399.0" y="24" width="13.2" height="12" style="fill:var(--link);opacity:.75"/><rect x="415.5" y="24" width="13.2" height="12" style="fill:var(--link);opacity:.75"/><rect x="432.0" y="24" width="13.2" height="12" style="fill:var(--link);opacity:.75"/><rect x="448.5" y="24" width="13.2" height="12" style="fill:var(--link);opacity:.75"/><rect x="465.0" y="24" width="13.2" height="12" style="fill:var(--link);opacity:.75"/><rect x="481.5" y="24" width="13.2" height="12" style="fill:var(--link);opacity:.75"/><rect x="498.0" y="24" width="13.2" height="12" style="fill:var(--link);opacity:.75"/><rect x="514.5" y="24" width="13.2" height="12" style="fill:var(--link);opacity:.75"/><rect x="531.0" y="24" width="13.2" height="12" style="fill:var(--link);opacity:.75"/><rect x="547.5" y="24" width="13.2" height="12" style="fill:var(--link);opacity:.75"/><text x="572.0" y="34" text-anchor="start" style="fill:var(--link);font-size:11px">1.69～2.11 秒</text><text x="178" y="64" text-anchor="end" style="fill:var(--fg);font-size:12px">HTTP/1.1 开 20 个连接并行</text><rect x="190.0" y="52" width="44.0" height="12" style="fill:var(--muted);opacity:.75"/><rect x="234.0" y="52" width="17.6" height="12" style="fill:var(--link);opacity:.75"/><rect x="245.0" y="66" width="44.0" height="12" style="fill:var(--muted);opacity:.75"/><rect x="289.0" y="66" width="17.6" height="12" style="fill:var(--link);opacity:.75"/><rect x="300.0" y="80" width="44.0" height="12" style="fill:var(--muted);opacity:.75"/><rect x="344.0" y="80" width="17.6" height="12" style="fill:var(--link);opacity:.75"/><rect x="355.0" y="94" width="44.0" height="12" style="fill:var(--muted);opacity:.75"/><rect x="399.0" y="94" width="17.6" height="12" style="fill:var(--link);opacity:.75"/><rect x="410.0" y="108" width="44.0" height="12" style="fill:var(--muted);opacity:.75"/><rect x="454.0" y="108" width="17.6" height="12" style="fill:var(--link);opacity:.75"/><text x="190" y="128" text-anchor="start" style="fill:var(--link);font-size:11px">（画出了前 5 个）20 个连接各自握手，共 1.40～2.49 秒</text><text x="178" y="148" text-anchor="end" style="fill:var(--fg);font-size:12px">HTTP/2 一个连接多路复用</text><rect x="190.0" y="138" width="44.0" height="12" style="fill:var(--muted);opacity:.75"/><rect x="234.0" y="138" width="35.2" height="12" style="fill:var(--accent);opacity:.75"/><text x="277.2" y="148" text-anchor="start" style="fill:var(--accent);font-size:11px">0.36 秒：20 个请求同时发出，响应交错着回来</text><text x="190" y="180" text-anchor="middle" style="fill:var(--muted);font-size:11px">0 秒</text><text x="300.0" y="180" text-anchor="middle" style="fill:var(--muted);font-size:11px">0.5 秒</text><text x="410.0" y="180" text-anchor="middle" style="fill:var(--muted);font-size:11px">1 秒</text><text x="520.0" y="180" text-anchor="middle" style="fill:var(--muted);font-size:11px">1.5 秒</text><text x="630.0" y="180" text-anchor="middle" style="fill:var(--muted);font-size:11px">2 秒</text><rect x="190" y="192" width="12" height="10" style="fill:var(--muted);opacity:.75"/><text x="206" y="201" text-anchor="start" style="fill:var(--fg);font-size:11px">TCP + TLS 握手</text><rect x="330" y="192" width="12" height="10" style="fill:var(--link);opacity:.75"/><text x="346" y="201" text-anchor="start" style="fill:var(--fg);font-size:11px">一个请求一个往返</text>
  </g>
</svg>
</figure>

- HTTP/1.1 依次请求：每个请求至少要等一个往返时间，20 个就是 20 个往返；
- HTTP/1.1 开 20 个连接：省去了排队，但每个连接都要做 TCP 和 TLS 握手，curl 还要分别建立它们，并不比依次请求快多少；
- HTTP/2：一次握手之后，20 个请求几乎同时发出，响应交错着回来，总共只比单个请求（约 0.28 秒，上一篇）多了约 0.08 秒。

## 5. HTTP/2 的剩余问题：TCP 的队头阻塞

HTTP/2 解决了 HTTP 层面的排队，但所有的流共用一个 TCP 连接。TCP 保证字节按顺序交付（第 2.3 篇）：如果某个报文段丢了，在它被重传到达之前，**后面所有的数据**都要在接收缓冲区中等着，即使它们属于其他的流、本身完整无缺。一个包的丢失让所有的流都停下来，这叫 TCP 层的**队头阻塞**。在丢包较多的网络（如移动网络）上，HTTP/2 有时甚至不如开多个连接的 HTTP/1.1。

<figure class="diagram">
<svg viewBox="0 0 700 176" role="img" aria-label="TCP 的队头阻塞：HTTP/2 的所有流共用一个 TCP 字节流，流 1 的一个报文段丢了，后面已经到达的流 2、流 3 的数据也要等它重传；QUIC 中每个流各自保证顺序，只有流 1 需要等待">
  <defs></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="20" y="40" text-anchor="start" style="fill:var(--fg);font-size:12px">HTTP/2 over TCP</text><text x="20" y="56" text-anchor="start" style="fill:var(--muted);font-size:11px">一个字节流</text><rect x="150" y="28" width="56" height="30" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="178" y="48" text-anchor="middle" style="fill:var(--fg);font-size:11px">流1·1</text><rect x="212" y="28" width="56" height="30" style="fill:var(--panel-2);stroke:var(--link);stroke-width:1.5"/><text x="240" y="48" text-anchor="middle" style="fill:var(--fg);font-size:11px">流2·1</text><rect x="274" y="28" width="56" height="30" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="302" y="48" text-anchor="middle" style="fill:var(--fg);font-size:11px">流3·1</text><rect x="336" y="28" width="56" height="30" style="fill:none;stroke:var(--danger);stroke-width:1.5;stroke-dasharray:4 3"/><text x="364" y="48" text-anchor="middle" style="fill:var(--danger);font-size:11px">丢了</text><rect x="398" y="28" width="56" height="30" style="fill:var(--panel-2);stroke:var(--link);stroke-width:1.5;opacity:.45"/><text x="426" y="48" text-anchor="middle" style="fill:var(--fg);font-size:11px">流2·2</text><rect x="460" y="28" width="56" height="30" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5;opacity:.45"/><text x="488" y="48" text-anchor="middle" style="fill:var(--fg);font-size:11px">流3·2</text><rect x="522" y="28" width="56" height="30" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5;opacity:.45"/><text x="550" y="48" text-anchor="middle" style="fill:var(--fg);font-size:11px">流1·3</text><text x="150" y="82" text-anchor="start" style="fill:var(--danger);font-size:11px">流1·2 丢了：后面已经到达的流2、流3 的数据也只能在接收缓冲区里等重传</text><text x="20" y="122" text-anchor="start" style="fill:var(--fg);font-size:12px">HTTP/3 over QUIC</text><text x="20" y="138" text-anchor="start" style="fill:var(--muted);font-size:11px">每个流各自有序</text><rect x="150" y="110" width="56" height="30" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="178" y="130" text-anchor="middle" style="fill:var(--fg);font-size:11px">流1·1</text><rect x="212" y="110" width="56" height="30" style="fill:var(--panel-2);stroke:var(--link);stroke-width:1.5"/><text x="240" y="130" text-anchor="middle" style="fill:var(--fg);font-size:11px">流2·1</text><rect x="274" y="110" width="56" height="30" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="302" y="130" text-anchor="middle" style="fill:var(--fg);font-size:11px">流3·1</text><rect x="336" y="110" width="56" height="30" style="fill:none;stroke:var(--danger);stroke-width:1.5;stroke-dasharray:4 3"/><text x="364" y="130" text-anchor="middle" style="fill:var(--danger);font-size:11px">丢了</text><rect x="398" y="110" width="56" height="30" style="fill:var(--panel-2);stroke:var(--link);stroke-width:1.5"/><text x="426" y="130" text-anchor="middle" style="fill:var(--fg);font-size:11px">流2·2</text><rect x="460" y="110" width="56" height="30" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="488" y="130" text-anchor="middle" style="fill:var(--fg);font-size:11px">流3·2</text><rect x="522" y="110" width="56" height="30" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5;opacity:.45"/><text x="550" y="130" text-anchor="middle" style="fill:var(--fg);font-size:11px">流1·3</text><text x="150" y="164" text-anchor="start" style="fill:var(--accent);font-size:11px">只有流1 等重传，流2、流3 照常交给应用</text>
  </g>
</svg>
</figure>

此外，TCP 的握手加上 TLS 的握手，建立一个连接至少要两个往返时间。TCP 在操作系统内核中实现，要改进它，就要升级全世界的操作系统和中间设备，非常缓慢。

## 6. HTTP/3 和 QUIC

**HTTP/3**（2022 年）的办法是：不再使用 TCP，改用 **QUIC**。QUIC 运行在 **UDP** 之上，在用户空间（应用程序或库中）实现：

| | HTTP/2 | HTTP/3 |
|---|---|---|
| 传输层 | TCP + TLS | QUIC（基于 UDP，内置 TLS 1.3） |
| 建立连接 | TCP 握手 + TLS 握手，至少 2 个往返 | 1 个往返；连接过的服务器可以 0 个往返 |
| 丢包的影响 | 所有流都要等 | 只有丢包的那个流要等，其他流不受影响 |
| 网络切换（如 Wi-Fi 换成 4G） | IP 地址变了，TCP 连接断开 | 用连接 ID 识别连接，可以继续使用 |
| 实现位置 | 操作系统内核 | 应用程序或库，可以快速更新 |

QUIC 把第 2 章中 TCP 的可靠传输、流量控制、拥塞控制，在 UDP 之上重新实现了一遍（第 2.1 篇讲过，UDP 正是用来做这种事的），并且按「流」而不是按「连接」来保证顺序。

服务器怎样告诉浏览器自己支持 HTTP/3？第 3.2 篇 example.com 的响应头中有一行：

```
alt-svc: h3=":443"; ma=86400
```

意思是「这个网站也可以通过 HTTP/3 在 UDP 的 443 端口访问，这个信息在 86400 秒内有效」。浏览器第一次用 HTTP/2 访问，看到这个头部后，之后的访问就会尝试 HTTP/3。

> **没有实测的部分**：实验容器中的 curl 8.5 编译时没有包含 HTTP/3 的支持（`--http3` 选项报告 `the installed libcurl version doesn't support this`），所以本节没有实测 HTTP/3。在 Chrome 中打开开发者工具的 Network 面板，加上 Protocol 列，访问支持的网站，可以看到 `h3`。

## 7. 常见问题

1. **以为 HTTP/2 一定比 HTTP/1.1 快**：在丢包严重的网络上，TCP 的队头阻塞会让它变慢。
2. **为了 HTTP/1.1 做的优化在 HTTP/2 中反而有害**：比如把很多小图片合并成一张大图、把资源分散到多个域名以绕过 6 个连接的限制，在 HTTP/2 中都没有必要了。
3. **用 tcpdump 分析 HTTPS 的问题却什么都看不到**：在自己的客户端设置 `SSLKEYLOGFILE`，再用 tshark 或 Wireshark 解密。这个文件包含了密钥，用完要删除。
4. **防火墙只放行了 TCP 443**：HTTP/3 用的是 UDP 443，被拦截时浏览器会退回 HTTP/2，功能正常但享受不到 HTTP/3 的好处。

## 8. 小结

- HTTP/1.1 的一个连接同时只能处理一个请求，头部以文本形式重复发送
- HTTP/2 使用二进制帧，每个请求是一个流，多个流在一个连接上多路复用；通过 TLS 的 ALPN 协商使用
- 用 `SSLKEYLOGFILE` 和 tshark 可以解密自己的 HTTPS 流量；实测看到 SETTINGS、HEADERS、DATA 帧，流 1、流 3
- HPACK 头部压缩：实测第二个请求的头部从 28 字节降到 13 字节，响应头部从 160 字节降到 59 字节
- 实测 20 个请求：HTTP/1.1 依次请求 1.69～2.11 秒，开 20 个连接 1.40～2.49 秒，HTTP/2 一个连接 0.36 秒
- HTTP/2 仍受 TCP 队头阻塞的影响；HTTP/3 改用基于 UDP 的 QUIC，握手更快、丢包只影响一个流、支持网络切换

## 9. 练习

1. 在第 3 节的输出中，为什么第二个请求的流编号是 3，而不是 2？
2. 一个网页有 100 个小图片，分别用 HTTP/1.1（每个网站最多 6 个连接）和 HTTP/2 加载，大约各需要多少个往返时间？（忽略握手和传输时间）
3. QUIC 为什么选择基于 UDP，而不是设计一个新的传输层协议（和 TCP、UDP 并列）？

<details>
<summary>点击查看答案</summary>

1. HTTP/2 规定，客户端发起的流用奇数编号，服务器发起的流（服务器推送）用偶数编号，编号只增不减，这样双方不会用到同一个编号。流 0 表示整个连接。所以客户端的第一个请求是流 1，第二个是流 3。
2. HTTP/1.1：6 个连接同时进行，每个连接依次请求，100 / 6 ≈ 17 轮，大约 **17 个往返时间**。HTTP/2：100 个请求可以同时发出，大约 **1 个往返时间**（实际还受服务器允许的最大并发流数、带宽等限制）。
3. 互联网上大量的防火墙、NAT 设备只认识 TCP 和 UDP，一个新的传输层协议（IP 头中一个新的协议号）会被它们拦截或丢弃，几乎无法部署。基于 UDP，QUIC 的包在这些设备看来只是普通的 UDP 数据报，可以顺利通过；同时，在用户空间实现也让它可以随着浏览器和服务器软件快速更新，不必等待操作系统升级。

</details>
