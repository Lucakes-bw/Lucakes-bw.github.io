---
title: HTTPS 与 TLS
date: 2026-10-24 16:10:00
updated: 2026-10-24 16:10:00
categories:
  - 计算机网络
  - 应用层协议
tags:
  - 计算机网络
  - HTTPS
  - TLS
order: 3
description: HTTP 是明文的，途中的任何人都能看到、修改你的数据。HTTPS 在 TCP 和 HTTP 之间加了一层 TLS，提供加密、完整性和身份验证。本文抓包对比 HTTP 和 HTTPS：前者能直接看到网页的内容，后者只剩下 Application Data；用 openssl s_client 查看 example.com 的 TLS 1.3 连接和由 4 张证书组成的证书链；测得 TLS 握手多花了约 0.077 秒（一个往返）；最后自己签发一张证书，依次复现「自签名证书」「名字不符」两种验证失败。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 HTTP 明文传输的风险；理解 TLS 提供的三种保护；理解对称加密、非对称加密和密钥交换在 TLS 中各自的作用；理解证书和证书链怎样证明「对方就是它声称的那个网站」；能读懂 TLS 握手的过程；会用 openssl 和 curl 排查证书问题。
>
> **前置知识**：上一篇的 HTTP；第 2.2 篇的 TCP 握手。
>
> **实验环境**：Ubuntu 24.04 容器，OpenSSL 3.0.13，curl 8.5，tshark 4.2。

## 1. HTTP 的问题

第 1.1 篇抓包时，在 tcpdump 的输出中直接就能读到 `GET / HTTP/1.1`、`Host: example.com`，以及网页的内容。在 HTTP 抓包中搜索网页的标题「Example Domain」，能找到 1 处；同样的网页通过 HTTPS 获取，抓包中一处也找不到：

```bash
tcpdump -nn -A -r http80.pcap | grep -c -i "example domain"
tcpdump -nn -A -r tls.pcap | grep -c -i "example domain"
```

```
1
0
```

HTTP 的数据经过的每一个设备，家里的路由器、公共 Wi-Fi、运营商、中间的每一台路由器，都能：

1. **窃听**：看到你访问的网址、提交的密码、Cookie；
2. **篡改**：在网页中插入广告，或者把下载的文件换成病毒；
3. **冒充**：第 1.2 篇讲的 ARP 欺骗、上一篇讲的 DNS 伪造，可以让你连到一台假的服务器，而你毫不知情。

**TLS**（Transport Layer Security，传输层安全）针对这三点提供了**加密**、**完整性校验**和**身份验证**。HTTPS 就是「HTTP over TLS」：在 TCP 之上先建立 TLS 连接，HTTP 在 TLS 中传送，默认端口 443。

```
HTTP        HTTP
            TLS
TCP         TCP
IP          IP
```

## 2. 用 openssl 连接 example.com

`openssl s_client` 是一个 TLS 客户端，能显示握手的所有细节：

```bash
echo | openssl s_client -connect example.com:443 -servername example.com
```

（只保留了关键的部分。）

```
New, TLSv1.3, Cipher is TLS_AES_256_GCM_SHA384
Peer signature type: ECDSA
Server Temp Key: X25519, 253 bits
Verification: OK
Verify return code: 0 (ok)
```

- **TLSv1.3**：协议版本，目前最新的版本（2018 年发布）；
- **TLS_AES_256_GCM_SHA384**：加密数据用的算法，AES-256-GCM 是一种对称加密算法；
- **Server Temp Key: X25519**：密钥交换用的算法，「Temp」表示这个密钥是这次连接临时生成的；
- **Peer signature type: ECDSA**：服务器用 ECDSA 签名来证明自己的身份；
- **Verification: OK**：证书验证通过。

这几行正好对应了 TLS 用到的三类密码学工具，下面逐一说明。

## 3. 加密：对称加密和密钥交换

**对称加密**（如 AES）用同一个密钥加密和解密，速度很快，适合加密大量数据。问题是：双方怎样得到同一个密钥？直接通过网络发送密钥，会被窃听者看到。

**密钥交换**（如 X25519，一种椭圆曲线上的 Diffie-Hellman 算法）解决了这个问题：双方各自生成一对临时的密钥，把**公开**的一半发给对方，再用自己**私密**的一半和对方公开的一半，各自算出同一个共享秘密。窃听者看到了两个公开的值，但从数学上无法算出这个秘密。双方再从共享秘密推导出对称加密的密钥。

因为密钥是每次连接临时生成的，用完就丢弃，即使将来服务器的私钥泄露了，过去被录下来的通信也无法解密，这叫**前向保密**。

加密之外，AES-GCM 还附带了**完整性校验**：每条记录都带有一个校验码，途中被修改过一个比特，接收方都能发现并断开连接。

## 4. 身份验证：证书

密钥交换能防止窃听，但防不了**冒充**：如果你连接的根本就是攻击者的服务器，密钥交换照样成功，你只是和攻击者建立了一个加密的连接。所以还需要证明「对方确实是 example.com」，这靠的是**证书**。

### 4.1 证书和签名

证书的核心内容是：「这个公钥属于 example.com」，再由一个可信的机构（**证书颁发机构**，CA）用自己的私钥**签名**。查看 example.com 的证书：

```bash
echo | openssl s_client -connect example.com:443 -servername example.com 2>/dev/null \
    | openssl x509 -noout -subject -issuer -dates -ext subjectAltName
```

```
subject=CN = example.com
issuer=C = US, O = SSL Corporation, CN = Cloudflare TLS Issuing ECC CA 3
notBefore=Sep 26 22:49:11 2026 GMT
notAfter=Dec 25 22:56:35 2026 GMT
X509v3 Subject Alternative Name:
    DNS:example.com, DNS:*.example.com
```

| 字段 | 含义 |
|---|---|
| subject | 证书的主人 |
| issuer | 签发者：Cloudflare 的一个中间 CA |
| notBefore / notAfter | 有效期：约 3 个月，过期就要换新的 |
| Subject Alternative Name | 这张证书可以用于哪些域名：example.com 和 `*.example.com`（任何一个子域名） |

握手时，服务器把证书发给客户端，并用证书中公钥对应的**私钥**签名握手的内容（上面的 `Peer signature type: ECDSA`）。客户端用证书中的公钥验证这个签名：只有真正拥有私钥的服务器才能签出正确的签名。攻击者可以把 example.com 的证书原样拿来，但没有私钥，就无法完成握手。

### 4.2 证书链

客户端凭什么相信「Cloudflare TLS Issuing ECC CA 3」？`-showcerts` 显示服务器发来的整条链（只保留了每张证书的主体和签发者）：

```
 0 s:CN = example.com
   i:C = US, O = SSL Corporation, CN = Cloudflare TLS Issuing ECC CA 3
 1 s:C = US, O = SSL Corporation, CN = Cloudflare TLS Issuing ECC CA 3
   i:C = US, O = SSL Corporation, CN = SSL.com TLS Transit ECC CA R2
 2 s:C = US, O = SSL Corporation, CN = SSL.com TLS Transit ECC CA R2
   i:C = US, O = SSL Corporation, CN = SSL.com TLS ECC Root CA 2022
 3 s:C = US, O = SSL Corporation, CN = SSL.com TLS ECC Root CA 2022
   i:C = GB, ST = Greater Manchester, L = Salford, O = Comodo CA Limited, CN = AAA Certificate Services
```

每张证书由下一张证书的主人签发：example.com ← Cloudflare 的中间 CA ← SSL.com 的中间 CA ← SSL.com 的**根证书**。客户端沿着这条链逐级验证签名，直到遇到一个**自己信任**的根证书。操作系统和浏览器都内置了一组受信任的根证书，这个容器中有 120 多个，其中就有这条链用到的根：

```bash
ls /etc/ssl/certs | grep SSL.com_TLS_ECC_Root
```

```
SSL.com_TLS_ECC_Root_CA_2022.pem
```

（第 3 张证书是 SSL.com 的根证书由更老的 AAA Certificate Services 交叉签名的版本，让还不认识这个新根的旧设备也能验证通过。）

所以，整个 HTTPS 的安全性最终建立在「你信任的这一百多个根 CA 不会乱签证书」之上。CA 在签发证书前，要验证申请者确实控制着这个域名。

## 5. TLS 握手

抓下 curl 访问 `https://example.com/` 的包，tshark 能识别出 TLS 的消息（只保留了前面的部分）：

```
3	1.018293	172.17.0.3	74	52116 → 443 [SYN] ...
4	1.082860	104.20.23.154	74	443 → 52116 [SYN, ACK] ...
5	1.082913	172.17.0.3	66	52116 → 443 [ACK] ...
6	1.087076	172.17.0.3	583	Client Hello (SNI=example.com)
8	1.153023	104.20.23.154	1514	Server Hello, Change Cipher Spec
10	1.153118	104.20.23.154	1514	443 → 52116 [PSH, ACK] ... [TCP segment of a reassembled PDU]
12	1.153120	104.20.23.154	1160	Application Data
14	1.159441	172.17.0.3	146	Change Cipher Spec, Application Data
16	1.159662	172.17.0.3	152	Application Data
17	1.159756	172.17.0.3	125	Application Data
20	1.222833	104.20.23.154	619	Application Data, Application Data
```

1. TCP 三次握手（3～5）；
2. **Client Hello**（6）：客户端列出支持的 TLS 版本、加密算法，以及自己的密钥交换公开值；
3. **Server Hello**（8）：服务器选定算法，给出自己的密钥交换公开值。此时双方已经可以算出共享秘密，**之后的一切都是加密的**：服务器接着发出的证书、签名（8～12，tshark 只能显示为 `Application Data`）都已经看不到明文了；
4. 客户端验证证书和签名，发出握手完成的消息（14）；
5. 之后就是加密的 HTTP 请求和响应（16、17、20）。

TLS 1.3 的握手只需要**一个往返**（Client Hello → Server Hello），旧的 TLS 1.2 需要两个。测量各阶段的耗时：

```bash
curl -s -o /dev/null -w "DNS %{time_namelookup}  TCP 连接 %{time_connect}  TLS 完成 %{time_appconnect}  首字节 %{time_starttransfer}  总计 %{time_total}\n" https://example.com/
```

```
DNS 0.067653  TCP 连接 0.132743  TLS 完成 0.209210  首字节 0.279521  总计 0.279636
DNS 0.067056  TCP 连接 0.132025  TLS 完成 0.209615  首字节 0.285634  总计 0.285820
DNS 0.067190  TCP 连接 0.131668  TLS 完成 0.209585  首字节 0.282877  总计 0.282983
```

TLS 握手用了约 0.077 秒（从 TCP 连接完成到 TLS 完成），正好是一个往返时间。和上一篇的 HTTP 相比，第一个请求多了约 0.08 秒。持久连接在 HTTPS 中更加重要：TLS 握手只需要在建立连接时做一次。

### 5.1 仍然能看到什么

加密并不是什么都隐藏了。用 tshark 取出 Client Hello 中的两个字段：

```bash
tshark -r tls.pcap -Y "tls.handshake.type==1" -T fields -e tls.handshake.extensions_server_name -e tls.handshake.extensions_alpn_str
```

```
example.com	h2,http/1.1
```

- **SNI**（服务器名称指示）：Client Hello 中以明文写着要访问的域名 `example.com`。原因和上一篇的 Host 头一样：一个 IP 地址上有很多网站，服务器要在加密开始之前就知道该出示哪个网站的证书。所以，途中的人看不到你访问的具体页面和内容，但能知道你访问了哪个网站（新的 ECH 扩展正在解决这个问题）；
- **ALPN**：客户端表示支持 HTTP/2（`h2`）和 HTTP/1.1，服务器在 Server Hello 中选择一个。这次 curl 实际使用的是 HTTP/2，下一篇详细讲。

此外，IP 地址、数据的大小和时间也都是可见的。

## 6. 证书验证失败

自己签发一张证书，运行一个 TLS 服务器，复现常见的验证失败：

```bash
#!/bin/bash
cd /lab
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -days 30 \
    -subj "/CN=lab.test" -addext "subjectAltName=DNS:lab.test" -keyout lab.key -out lab.crt 2>/dev/null
openssl x509 -in lab.crt -noout -subject -issuer
for p in $(pgrep -f "openssl s_server"); do kill $p; done
ip netns exec s openssl s_server -accept 8443 -cert lab.crt -key lab.key -www -quiet & S=$!; sleep 1
R="--resolve lab.test:8443:10.0.2.2"
echo "=== 1. 直接访问"; ip netns exec c curl -s -o /dev/null $R https://lab.test:8443/; echo "exit=$?"; ip netns exec c curl -sS -o /dev/null $R https://lab.test:8443/ 2>&1 | head -2
echo "=== 2. 信任这个证书"; ip netns exec c curl -s -o /dev/null -w "%{http_code} %{ssl_verify_result}\n" --cacert lab.crt $R https://lab.test:8443/
echo "=== 3. 名字不符"; ip netns exec c curl -sS -o /dev/null --cacert lab.crt https://10.0.2.2:8443/ 2>&1 | head -2
echo "=== 4. -k 跳过验证"; ip netns exec c curl -s -k -o /dev/null -w "%{http_code} %{ssl_verify_result}\n" $R https://lab.test:8443/
kill $S
```

```
subject=CN = lab.test
issuer=CN = lab.test
=== 1. 直接访问
exit=60
curl: (60) SSL certificate problem: self-signed certificate
=== 2. 信任这个证书
200 0
=== 3. 名字不符
curl: (60) SSL: no alternative certificate subject name matches target host name '10.0.2.2'
=== 4. -k 跳过验证
200 18
```

（`--resolve lab.test:8443:10.0.2.2` 让 curl 把 lab.test 解析成 10.0.2.2，相当于在 `/etc/hosts` 中加了一行；`openssl s_server -www` 是一个简单的 TLS 服务器，对每个请求返回一个状态页面。）

1. **自签名证书**：主体和签发者都是 lab.test 自己，没有任何受信任的 CA 为它担保，curl 拒绝连接；
2. 用 `--cacert` 明确地信任这张证书后，验证通过，`ssl_verify_result` 为 0；
3. **名字不符**：证书是给 lab.test 的，用 IP 地址 10.0.2.2 访问，和证书中的名字不一致，同样失败。攻击者即使有一张合法的证书，也只能是他自己的域名的；
4. `-k`（`--insecure`）跳过验证，请求成功了，但 `ssl_verify_result` 是 18（OpenSSL 的错误码：自签名证书）。数据仍然是加密的，但已经无法确认对方是谁，**中间人攻击**正是利用这一点。`-k` 只能在测试时使用。

常见的验证失败还有：证书过期（`certificate has expired`）、服务器没有发送中间证书（`unable to get local issuer certificate`）、系统时间不对（证书「尚未生效」或「已过期」）。

## 7. 常见问题

1. **用 `curl -k` 或者在程序中关闭证书验证来「解决」错误**：这让 HTTPS 失去了身份验证，等于没有保护。
2. **服务器只配置了自己的证书，没有配置中间证书**：浏览器可能能用（它会缓存或下载中间证书），但 curl 和程序会失败。
3. **证书过期**：现在的证书有效期通常只有几个月，要用自动续期的工具（如 certbot）。
4. **以为 HTTPS 隐藏了访问的网站**：SNI 是明文的，IP 地址也是可见的。
5. **混合内容**：HTTPS 的网页中用 HTTP 加载脚本，脚本可能被篡改，浏览器会阻止。

## 8. 小结

- HTTP 是明文的，可以被窃听、篡改、冒充；HTTPS = HTTP + TLS + TCP，默认端口 443
- 密钥交换（如 X25519）让双方得到同一个秘密而不被窃听者知道，再用对称加密（如 AES-GCM）加密数据并校验完整性；临时密钥提供前向保密
- 证书证明公钥属于某个域名，由 CA 签名；客户端沿证书链验证到内置的根证书；实测 example.com 的链有 4 张证书
- TLS 1.3 握手一个往返，实测多花约 0.077 秒；握手之后连证书都是加密的，但 SNI 和 ALPN 仍是明文
- 证书验证失败的常见原因：自签名、名字不符、过期、缺少中间证书；不要用 `-k` 掩盖问题

## 9. 练习

1. 用 `openssl s_client -connect example.com:443 -tls1_2` 强制使用 TLS 1.2，协商出的加密算法有什么不同？
2. 为什么证书的有效期越来越短（从几年缩短到几个月）？
3. 攻击者控制了你连接的 Wi-Fi，把 example.com 解析到他自己的服务器，并出示一张他自签名的、名字为 example.com 的证书。浏览器会怎样？

<details>
<summary>点击查看答案</summary>

1. ```
   New, TLSv1.2, Cipher is ECDHE-ECDSA-CHACHA20-POLY1305
   Peer signature type: ECDSA
   Server Temp Key: X25519, 253 bits
   ```

   TLS 1.2 的算法名称把各部分都写了出来：ECDHE（密钥交换）、ECDSA（签名）、CHACHA20-POLY1305（对称加密和完整性校验，AES-GCM 的替代品）；TLS 1.3 的名称中只写对称加密部分，密钥交换另外协商。再用 curl 限制最高版本为 TLS 1.2 测量两次：

   ```
   TLS1.2：TCP 连接 0.130406  TLS 完成 0.269833
   TLS1.2：TCP 连接 0.133379  TLS 完成 0.273628
   ```

   TLS 握手用了约 0.14 秒，是两个往返时间，而 TLS 1.3 只要约 0.077 秒（一个往返）。
2. 证书一旦签发就无法真正收回：私钥泄露或域名易主后，旧证书在过期前仍然有效（吊销机制在实践中并不可靠）。有效期越短，泄露的证书能被滥用的时间就越短；同时也迫使网站使用自动续期，减少「忘了续期导致证书过期」这种故障。
3. 浏览器会显示严重的安全警告并阻止访问，和第 6 节的第 1 种情况一样：这张证书的签发者是攻击者自己，不在浏览器信任的根证书中。名字虽然对了，但没有受信任的 CA 为它担保。只要用户不点「继续访问」，攻击就失败了。

</details>
