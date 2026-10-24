---
title: HTTP/1.1
date: 2026-10-24 16:05:00
updated: 2026-10-24 16:05:00
categories:
  - 计算机网络
  - 应用层协议
tags:
  - 计算机网络
  - HTTP
order: 2
description: HTTP 是一个文本协议，用 nc 就能手写一个请求。本文手写请求访问 example.com，读懂响应中的状态行、头部和分块传输的正文（241 就是十六进制的 577）；去掉 Host 头，立即得到 400；用一个自己写的小服务器演示 POST、301 重定向和分块传输，抓包看请求的原始格式；最后实测持久连接：5 个请求各建一个连接，每次约 0.2 秒，复用同一个连接后，后面的请求只要 0.075 秒。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 HTTP 请求和响应的格式；认识常用的方法、状态码和头部；理解 Host 头和虚拟主机；理解 Content-Length 和分块传输两种确定正文长度的方式；理解持久连接（keep-alive）的作用；会用 curl 和 nc 调试 HTTP。
>
> **前置知识**：第 2 章的 TCP，特别是第 2.5 篇「字节流没有边界」；上一篇的 DNS。
>
> **实验环境**：Ubuntu 24.04 容器，curl 8.5，Python 3.12。访问外网的实验直接在容器中进行；自己写的服务器运行在本章的实验网络中（c → r → s，s 的地址是 10.0.2.2）。

## 1. 手写一个 HTTP 请求

**HTTP**（HyperText Transfer Protocol）是网页使用的协议，运行在 TCP 之上，默认端口 80。HTTP/1.1 是**文本协议**：请求和响应都是人能读懂的文字。所以不需要浏览器，用 nc 就能手写一个请求：

```bash
printf "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n" | nc example.com 80
```

```
HTTP/1.1 200 OK
Date: Sat, 24 Oct 2026 08:06:30 GMT
Content-Type: text/html; charset=utf-8
Transfer-Encoding: chunked
Connection: close
Server: cloudflare
Last-Modified: Sat, 24 Oct 2026 06:00:47 GMT
Allow: GET, HEAD
Accept-Ranges: bytes
Age: 9
cf-cache-status: HIT
CF-RAY: a4f77b8b3f16c7b1-HKG
alt-svc: h3=":443"; ma=86400

241
<!doctype html><html lang=en><head><meta charset=utf-8>...（省略）...</html>

0

```

### 1.1 请求的格式

```
GET / HTTP/1.1\r\n                ← 请求行：方法 路径 版本
Host: example.com\r\n             ← 头部，每行「名字: 值」
Connection: close\r\n
\r\n                              ← 空行：头部结束
（正文，GET 请求通常没有）
```

每行以 `\r\n`（回车换行）结尾，头部以一个**空行**结束。这就是第 2.5 篇讲的「用分隔符划分消息」：服务器读到 `\r\n\r\n`，就知道请求的头部完整了。

### 1.2 响应的格式

```
HTTP/1.1 200 OK                   ← 状态行：版本 状态码 原因短语
Content-Type: text/html; ...      ← 头部
...
                                  ← 空行
（正文）
```

几个头部：

| 头部 | 含义 |
|---|---|
| `Content-Type` | 正文的类型：`text/html` 是网页，`charset=utf-8` 是字符编码 |
| `Transfer-Encoding: chunked` | 正文分块传送（第 3 节） |
| `Connection: close` | 发完就关闭连接（因为请求中要求了） |
| `Server: cloudflare` | 服务器软件。example.com 由 Cloudflare 托管 |
| `Age: 9`、`cf-cache-status: HIT` | 这个响应来自 Cloudflare 的缓存，已经缓存了 9 秒 |
| `Last-Modified` | 内容最后修改的时间，浏览器可以据此询问「有没有更新」 |
| `alt-svc: h3=":443"` | 告诉客户端：这个网站也支持 HTTP/3（第 3.4 篇） |

### 1.3 Host 头

去掉 `Host` 头再试一次：

```bash
printf "GET / HTTP/1.1\r\nConnection: close\r\n\r\n" | nc example.com 80
```

```
HTTP/1.1 400 Bad Request
Server: cloudflare
...
<html>
<head><title>400 Bad Request</title></head>
```

**400 Bad Request**。HTTP/1.1 规定请求必须带 `Host` 头。原因是同一个 IP 地址上可能运行着成千上万个网站：上一篇看到 example.com 的地址是 Cloudflare 的，Cloudflare 的同一台服务器同时为大量的域名服务。TCP 连接中只有 IP 地址和端口，服务器只能靠 `Host` 头知道你要访问哪个网站，这叫**虚拟主机**。

## 2. 方法和状态码

为了方便实验，用 Python 写一个演示用的服务器，运行在 s（10.0.2.2:8081）上：

```python
# httpd.py：一个演示用的 HTTP 服务器
from http.server import BaseHTTPRequestHandler, HTTPServer
import socket, time

class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"          # 支持持久连接和分块传输

    def do_GET(self):
        if self.path == "/chunked":           # 分块传输：事先不知道总长度，边生成边发送
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for i in range(3):
                part = ("第 %d 块数据\n" % (i + 1)).encode()
                self.wfile.write(b"%x\r\n%s\r\n" % (len(part), part))
                self.wfile.flush()
                time.sleep(0.5)
            self.wfile.write(b"0\r\n\r\n")    # 长度为 0 的块表示结束
        elif self.path == "/old":
            self.send_response(301)
            self.send_header("Location", "/new")
            self.send_header("Content-Length", "0")
            self.end_headers()
        else:
            body = ("你请求的路径是 %s\n" % self.path).encode()
            self.send_response(200 if self.path == "/new" or self.path == "/" else 404)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        data = self.rfile.read(n)
        body = ("收到 %d 字节：%s\n" % (n, data.decode())).encode()
        self.send_response(201)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass

class Server(HTTPServer):
    def server_bind(self):                    # 跳过 HTTPServer 默认的反向域名查询（实验网络中没有 DNS）
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.socket.bind(self.server_address)
        self.server_name, self.server_port = self.server_address[:2]

Server(("10.0.2.2", 8081), H).serve_forever()
```

### 2.1 POST

```bash
curl -i -d "name=lucky&lang=c" http://10.0.2.2:8081/users
```

`-d` 表示用 POST 方法发送数据，`-i` 显示响应的头部。在 s 上抓到的请求原文：

```
POST /users HTTP/1.1
Host: 10.0.2.2:8081
User-Agent: curl/8.5.0
Accept: */*
Content-Length: 17
Content-Type: application/x-www-form-urlencoded

name=lucky&lang=c
```

响应：

```
HTTP/1.1 201 Created
Server: BaseHTTP/0.6 Python/3.12.3
Date: Sat, 24 Oct 2026 11:42:33 GMT
Content-Type: text/plain; charset=utf-8
Content-Length: 37

收到 17 字节：name=lucky&lang=c
```

POST 请求有正文，`Content-Length: 17` 告诉服务器正文有 17 字节，服务器读完头部后再读 17 字节，就得到了完整的请求（第 2.5 篇的「长度前缀」）。`Content-Type` 说明了正文的格式：`application/x-www-form-urlencoded` 是网页表单的默认格式。

常用的方法：

| 方法 | 用途 | 有正文吗 |
|---|---|---|
| GET | 获取资源 | 通常没有 |
| POST | 提交数据，创建资源 | 有 |
| PUT | 替换资源 | 有 |
| DELETE | 删除资源 | 通常没有 |
| HEAD | 和 GET 一样，但只要头部（`curl -I`） | 没有 |

### 2.2 重定向

```bash
curl -i -L http://10.0.2.2:8081/old
```

```
HTTP/1.1 301 Moved Permanently
Server: BaseHTTP/0.6 Python/3.12.3
Date: Sat, 24 Oct 2026 11:42:33 GMT
Location: /new
Content-Length: 0

HTTP/1.1 200 OK
...
你请求的路径是 /new
```

301 表示资源已经永久地移到了 `Location` 指出的地址，`-L` 让 curl 自动跟随，于是又请求了 `/new`。真实的例子：

```bash
curl -sI http://github.com/
```

```
HTTP/1.1 301 Moved Permanently
Content-Length: 0
Location: https://github.com/
```

用 HTTP 访问 GitHub，它让你改用 HTTPS（下一篇）。

### 2.3 状态码

状态码的第一位表示类别：

| 状态码 | 类别 | 本文中的例子 |
|---|---|---|
| 2xx | 成功 | 200 OK、201 Created |
| 3xx | 重定向 | 301 Moved Permanently（还有 302 临时重定向、304 未修改） |
| 4xx | 客户端的错误 | 400 Bad Request（缺 Host 头）、404 Not Found（`curl http://example.com/nope` 实测返回 404） |
| 5xx | 服务器的错误 | 500 Internal Server Error、502 Bad Gateway、503 Service Unavailable |

## 3. 正文有多长：Content-Length 和分块传输

TCP 是字节流，客户端怎么知道正文在哪里结束？有两种方式：

1. **Content-Length**：在头部写明正文的字节数，上面的 POST 和响应都是这样；
2. **分块传输**（`Transfer-Encoding: chunked`）：服务器事先不知道总长度时（比如边生成边发送），把正文分成若干块，每块前面写上这一块的长度（**十六进制**），最后用一个长度为 0 的块表示结束。

演示服务器的 `/chunked` 每 0.5 秒发出一块。用 nc 取回原始的响应，按 `\r\n` 分行显示：

```
'HTTP/1.1 200 OK'
'Server: BaseHTTP/0.6 Python/3.12.3'
'Date: Sat, 24 Oct 2026 11:42:38 GMT'
'Content-Type: text/plain'
'Transfer-Encoding: chunked'
''
'10'
'第 1 块数据\n'
'10'
'第 2 块数据\n'
'10'
'第 3 块数据\n'
'0'
''
''
```

每块前面的 `10` 是十六进制的 16：`第 1 块数据\n` 在 UTF-8 中正好是 16 个字节（每个汉字 3 个字节）。最后的 `0` 表示结束。

回头看 example.com 的响应：它也是分块传输的，只有一块，长度是 `241`，即十六进制的 577，正好是第 1.1 篇中 curl 报告的网页大小（577 字节）。

还有第三种方式：HTTP/1.0 中，服务器发完正文就关闭连接，客户端读到连接关闭就知道结束了。但这样每个请求都要建立一个新连接。

## 4. 持久连接

HTTP/1.1 默认使用**持久连接**（keep-alive）：一个请求完成后，TCP 连接不关闭，下一个请求接着使用它。效果有多大？请求 example.com 5 次，先用 5 个 curl 进程（每次一个新连接），再用一个 curl 进程请求 5 次（复用连接）：

```bash
for i in 1 2 3 4 5; do
    curl -s -o /dev/null -w "连接 %{time_connect}s  首字节 %{time_starttransfer}s  总计 %{time_total}s\n" http://example.com/
done
curl -s -o /dev/null ... -w "..." http://example.com/ http://example.com/ http://example.com/ http://example.com/ http://example.com/
```

```
--- 每次一个新连接（5 个 curl 进程）
连接 0.128260s  首字节 0.197202s  总计 0.197308s
连接 0.133504s  首字节 0.204990s  总计 0.205152s
连接 0.132557s  首字节 0.209302s  总计 0.209435s
连接 0.129858s  首字节 0.203725s  总计 0.203904s
连接 0.131817s  首字节 0.201507s  总计 0.201644s
--- 一个 curl 进程请求 5 次（复用连接）
连接 0.134282s  首字节 0.208153s  总计 0.208327s  num_connects=1
连接 0.000000s  首字节 0.075620s  总计 0.075744s  num_connects=0
连接 0.000000s  首字节 0.074723s  总计 0.074847s  num_connects=0
连接 0.000000s  首字节 0.072519s  总计 0.072699s  num_connects=0
连接 0.000000s  首字节 0.071697s  总计 0.071865s  num_connects=0
```

- 新连接的请求每次约 0.2 秒，其中约 0.13 秒花在「连接」上：DNS 查询加上 TCP 握手。
- 复用连接时，第一个请求一样要 0.2 秒，之后的请求不需要建立连接（`num_connects=0`），只要约 **0.075 秒**，也就是一个往返时间。

`curl -v` 可以看到复用的过程：

```
* Connected to example.com (172.66.147.243) port 80
* Connection #0 to host example.com left intact
* Re-using existing connection with host example.com
* Connection #0 to host example.com left intact
```

一个网页通常有几十个资源（图片、CSS、JS），持久连接省下了大量的握手时间，也减少了第 2.2 篇讲的 TIME_WAIT。但 HTTP/1.1 中，一个连接上的请求只能一个接一个地进行，前一个响应没有完成，后一个就要等着，浏览器只好同时打开多个连接（通常每个网站最多 6 个）。第 3.4 篇的 HTTP/2 解决了这个问题。

## 5. 无状态和 Cookie

HTTP 是**无状态**的：每个请求都是独立的，服务器不会记得「刚才是谁」。登录、购物车这些需要「记住」的功能，靠的是 **Cookie**：服务器在响应中用 `Set-Cookie` 头给客户端一个标识，客户端之后的每个请求都在 `Cookie` 头中带上它，服务器据此认出这个用户。Cookie 中常常保存着登录凭据，所以在明文的 HTTP 上传送它是很危险的，这是下一篇 HTTPS 要解决的问题之一。

## 6. 常见问题

1. **请求的行尾只写 `\n`**：标准要求 `\r\n`，很多服务器能容忍，但不要依赖它。
2. **忘了 Host 头**：HTTP/1.1 中是 400。
3. **自己实现客户端时，用「连接关闭」判断正文结束**：持久连接下连接不会关闭，要根据 `Content-Length` 或分块传输来判断。
4. **把分块的长度当成十进制**：它是十六进制的。
5. **POST 数据却忘了 `Content-Type`**：服务器不知道该怎样解析正文。

## 7. 小结

- HTTP/1.1 是运行在 TCP 上的文本协议：请求行或状态行、头部、空行、正文，行尾是 `\r\n`
- `Host` 头是必须的，同一个 IP 地址上的多个网站靠它区分；缺少时实测返回 400
- 常用方法：GET、POST、PUT、DELETE、HEAD；状态码按首位分为成功、重定向、客户端错误、服务器错误
- 正文的长度用 `Content-Length` 或分块传输确定；分块的长度是十六进制，实测 example.com 的 `241` 就是 577 字节
- 持久连接复用 TCP 连接：实测新连接每个请求约 0.2 秒，复用后约 0.075 秒
- HTTP 是无状态的，用 Cookie 识别用户

## 8. 练习

1. 用 nc 手写一个 HEAD 请求访问 example.com，响应和 GET 有什么不同？
2. 在第 4 节中，新连接的请求「连接」用了约 0.13 秒，而复用连接时一个完整的请求只要 0.075 秒。为什么建立连接比完成一个请求还慢？
3. 如果服务器在一个分块传输的响应中途崩溃了，客户端能发现吗？如果用的是 Content-Length 呢？

<details>
<summary>点击查看答案</summary>

1. ```bash
   printf "HEAD / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n" | nc example.com 80
   ```

   ```
   HTTP/1.1 200 OK
   Date: Sat, 24 Oct 2026 11:43:34 GMT
   Content-Type: text/html; charset=utf-8
   Connection: close
   Server: cloudflare
   Last-Modified: Sat, 24 Oct 2026 06:00:47 GMT
   Allow: GET, HEAD
   Accept-Ranges: bytes
   Age: 7
   cf-cache-status: HIT
   CF-RAY: a4f8b9814c8f0506-HKG
   alt-svc: h3=":443"; ma=86400
   ```

   头部和 GET 基本相同，但**没有正文**，也没有 `Transfer-Encoding: chunked`。HEAD 常用于检查一个资源是否存在、有多大、有没有更新，而不必下载它。
2. 「连接」的时间包括 **DNS 查询**和 **TCP 握手**。第 3.1 篇看到，查询 1.1.1.1 要约 65 毫秒，TCP 握手又要一个往返时间（约 65 毫秒），加起来正好约 0.13 秒（实测一次：DNS 0.067 秒，到连接建立完成共 0.131 秒）；而复用连接的请求只需要一个往返时间。用 `-w "%{time_namelookup}"` 可以单独看到 DNS 查询的时间。
3. 都能发现。分块传输时，客户端没有收到长度为 0 的结束块，连接就断开了，说明响应不完整；用 Content-Length 时，收到的字节数少于声明的长度，连接就断开了，同样说明不完整。只有 HTTP/1.0 那种「以关闭连接表示结束」的方式，才无法区分正常结束和中途崩溃。

</details>
