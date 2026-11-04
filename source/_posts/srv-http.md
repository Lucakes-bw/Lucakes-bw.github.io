---
title: 实战：HTTP 服务器
date: 2026-11-04 14:40:00
updated: 2026-11-04 14:40:00
categories:
  - 计算机网络
  - 服务器开发
tags:
  - 网络编程
  - HTTP
  - 状态机
  - C++
order: 5
description: 在 tinynet 上实现一个 HTTP/1.1 服务器。核心是一个增量的请求解析器：数据一段一段地到达，解析器记住解析到了哪一步，数据不够就等下一次，一次到达多个请求就逐个处理。再加上持久连接、流水线、请求头的长度限制和空闲超时，业务代码只有几十行，就是一个每秒能处理四十多万个请求的服务器。
disableNunjucks: true
---

> **本文目标**：会写一个增量的、基于状态机的协议解析器；理解为什么必须限制请求头的长度；会处理持久连接和流水线；会用 tinynet 的 HttpServer 写一个简单的 Web 服务。
>
> **前置知识**：[上一篇](/posts/srv-multithread/)；《HTTP/1.1》；《实战：迷你 HTTP 服务器》。

## 1. 要做什么

《实战：迷你 HTTP 服务器》用 C 写了一个静态文件服务器，每个连接一个循环，阻塞地读请求。这一篇在 tinynet 上实现同样的协议，但是是非阻塞的、事件驱动的。业务代码只需要回答「这个请求的响应是什么」：

```cpp
HttpServer server(&loop, InetAddress(port), "http");
server.setThreadNum(threads);
server.setIdleTimeout(std::chrono::seconds(idle));
server.setHttpCallback([&server](const HttpRequest& req, HttpResponse* resp) {
    resp->setContentType("text/plain; charset=utf-8");
    if (req.method != "GET") {
        resp->setStatus(405, "Method Not Allowed");
    } else if (req.path == "/") {
        resp->setBody("hello, tinynet\n");
    } else if (req.path == "/stats") {
        resp->setBody("connections: " + std::to_string(server.connectionCount()) + "\n");
    } else {
        resp->setStatus(404, "Not Found");
        resp->setBody("not found: " + req.path + "\n");
    }
});
server.start();
loop.loop();
```

运行，发几个请求（用 bash 的 `/dev/tcp` 发送原始的请求，能看到完整的响应）：

```
$ printf 'GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n' > /dev/tcp/...
HTTP/1.1 200 OK
Connection: close
Content-Length: 15
Content-Type: text/plain; charset=utf-8

hello, tinynet

$ （GET /stats）
HTTP/1.1 200 OK
Connection: close
Content-Length: 15
Content-Type: text/plain; charset=utf-8

connections: 1

$ （GET /nope）
HTTP/1.1 404 Not Found
Connection: close
Content-Length: 17
Content-Type: text/plain; charset=utf-8

not found: /nope

$ （POST /，带 3 个字节的正文）
HTTP/1.1 405 Method Not Allowed
Connection: close
Content-Length: 0
Content-Type: text/plain; charset=utf-8

$ （HELLO）
HTTP/1.1 400 Bad Request
Connection: close
Content-Length: 0
```

## 2. 增量解析

### 2.1 为什么要「增量」

一个 HTTP 请求可能分好几次到达：

```
第 1 次 read：GET /index.html HT
第 2 次 read：TP/1.1\r\nHost: exa
第 3 次 read：mple.com\r\n\r\n
```

也可能一次到达好几个（客户端使用了流水线，或者 TCP 把几个小包合在了一起）。非阻塞的服务器不能「等它读完」，每次有数据到达，`onMessage` 被调用一次，解析器只能处理已经到达的部分，然后返回，下次再接着处理。所以解析器必须**记住自己解析到了哪一步**，这就是一个**状态机**。

### 2.2 状态机

HTTP 请求的结构是：一行请求行，若干行头部，一个空行，可能还有正文（长度由 `Content-Length` 给出）。解析器有三个状态：

<figure class="diagram">
<svg viewBox="0 0 700 200" role="img" aria-label="HTTP 请求解析器的状态机：请求行状态读到一行后进入头部状态；头部状态每读到一行加一个头部，读到空行时，没有 Content-Length 就得到完整的请求，有就进入正文状态；正文收够了就得到完整的请求；任何状态中数据不够都返回 NeedMore，下次接着解析">
  <defs><marker id="ps-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="ps-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="14" y="70" width="150" height="52" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="89.0" y="92.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">请求行</text><text x="89.0" y="108.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">GET /a HTTP/1.1</text><rect x="250" y="70" width="150" height="52" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="325.0" y="92.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">头部</text><text x="325.0" y="108.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">Name: value</text><rect x="486" y="10" width="200" height="52" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="586.0" y="32.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">得到请求</text><text x="586.0" y="48.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">kGotRequest</text><rect x="486" y="130" width="200" height="52" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="586.0" y="152.4" text-anchor="middle" style="fill:var(--fg);font-size:13px">正文</text><text x="586.0" y="168.4" text-anchor="middle" style="fill:var(--muted);font-size:11px">等够 Content-Length</text><line x1="164" y1="96" x2="248" y2="96" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ps-a)"/><text x="206.0" y="88.0" text-anchor="middle" style="fill:var(--link);font-size:11px">一行</text><path d="M 300 70 C 290 30, 360 30, 350 70" style="fill:none;stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ps-a)"/><text x="325.0" y="26.0" text-anchor="middle" style="fill:var(--link);font-size:11px">每行一个头部</text><line x1="400" y1="86" x2="484" y2="40" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ps-a)"/><text x="440.0" y="52.0" text-anchor="middle" style="fill:var(--link);font-size:11px">空行，无正文</text><line x1="400" y1="108" x2="484" y2="152" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ps-a)"/><text x="440.0" y="148.0" text-anchor="middle" style="fill:var(--link);font-size:11px">空行，有正文</text><line x1="586" y1="130" x2="586" y2="64" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ps-a)"/><text x="620.0" y="100.0" text-anchor="middle" style="fill:var(--link);font-size:11px">收够了</text><text x="14.0" y="160.0" text-anchor="start" style="fill:var(--muted);font-size:11px">任何状态：找不到 \r\n 或者</text><text x="14.0" y="176.0" text-anchor="start" style="fill:var(--muted);font-size:11px">数据不够 → kNeedMore，下次接着解析</text><text x="14.0" y="192.0" text-anchor="start" style="fill:var(--danger);font-size:11px">超过 8 KB 或格式不对 → kBadRequest</text>
  </g>
</svg>
</figure>

```cpp
// 增量的 HTTP 请求解析器：数据一段一段地到达，每次有新数据就调用 parse，
// 它从 Buffer 中取走能解析的部分，记住解析到哪一步了，数据不够时等下一次。
class HttpParser {
public:
    enum class Result { kNeedMore, kGotRequest, kBadRequest };

    static constexpr std::size_t kMaxHeaderBytes = 8192;    // 请求行加头部的最大长度
    static constexpr std::size_t kMaxBodyBytes = 1 << 20;

    Result parse(Buffer* buf);
    const HttpRequest& request() const { return request_; }
    void reset();                           // 处理完一个请求以后调用，准备解析下一个

private:
    enum class State { kRequestLine, kHeaders, kBody };
    State state_ = State::kRequestLine;
    std::size_t headerBytes_ = 0;
    std::size_t bodyLength_ = 0;
    HttpRequest request_;
};
```

```cpp
HttpParser::Result HttpParser::parse(Buffer* buf)
{
    while (true) {
        if (state_ == State::kBody) {
            if (buf->readableBytes() < bodyLength_) return Result::kNeedMore;
            request_.body = buf->retrieveAsString(bodyLength_);
            return Result::kGotRequest;
        }
        const char* crlf = buf->findCRLF();
        if (crlf == nullptr) {              // 一行还没有收完
            if (headerBytes_ + buf->readableBytes() > kMaxHeaderBytes) return Result::kBadRequest;
            return Result::kNeedMore;
        }
        headerBytes_ += static_cast<std::size_t>(crlf - buf->peek()) + 2;
        if (headerBytes_ > kMaxHeaderBytes) return Result::kBadRequest;

        if (state_ == State::kRequestLine) {
            if (!parseRequestLine(buf->peek(), crlf)) return Result::kBadRequest;
            state_ = State::kHeaders;
        } else if (crlf == buf->peek()) {   // 空行：头部结束
            buf->retrieveUntil(crlf + 2);
            std::string len = request_.header("content-length");
            if (len.empty()) return Result::kGotRequest;
            // ……检查 len 是合法的数字、不超过 kMaxBodyBytes
            bodyLength_ = n;
            state_ = State::kBody;
            continue;
        } else if (!parseHeader(buf->peek(), crlf)) {
            return Result::kBadRequest;
        }
        buf->retrieveUntil(crlf + 2);
    }
}
```

要点：

- **按行处理**：用 `Buffer::findCRLF()` 找 `\r\n`，找到一行就处理一行，并从缓冲区中取走；找不到说明这一行还没收完，返回 `kNeedMore`，已经收到的半行留在缓冲区中；
- **状态保存在对象中**：下一次调用 `parse` 时，从上次停下的状态继续；
- **只取走处理完的部分**：一个请求解析完以后，缓冲区中剩下的是下一个请求的开头，不能动它。

测试 `ByteByByte` 把一个带正文的 POST 请求**一个字节一个字节地**喂给解析器，每喂一个字节调用一次 `parse`，最后得到完整、正确的请求。这是对增量解析最严格的测试：任何「假设一次能收到一整行」的写法都过不了。

### 2.3 限制长度

注意 `kMaxHeaderBytes`：请求行加头部最多 8 KB。如果不限制，一个客户端可以连上来，然后不停地发送一个永远没有 `\r\n` 的「请求行」，服务器就一直把它存在输入缓冲区中，直到内存耗尽。几个这样的连接就能让服务器崩溃，这是一种很便宜的拒绝服务攻击。

有了限制，超过 8 KB 还没有收到完整的头部，就返回 400 并关闭连接（测试 `HeaderTooLong`）。正文同样限制了 1 MB。真正的服务器（Nginx 的 `large_client_header_buffers`、`client_max_body_size`）都有类似的设置。

### 2.4 请求行和头部

```cpp
// GET /path?query HTTP/1.1
bool HttpParser::parseRequestLine(const char* begin, const char* end)
{
    std::string_view line(begin, static_cast<std::size_t>(end - begin));
    std::size_t sp1 = line.find(' ');
    if (sp1 == std::string_view::npos || sp1 == 0) return false;
    std::size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos || sp2 == sp1 + 1) return false;
    request_.method = std::string(line.substr(0, sp1));
    std::string_view target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    std::size_t q = target.find('?');
    request_.path = std::string(target.substr(0, q));
    if (q != std::string_view::npos) request_.query = std::string(target.substr(q + 1));
    request_.version = std::string(line.substr(sp2 + 1));
    return request_.version == "HTTP/1.1" || request_.version == "HTTP/1.0";
}
```

头部的名字不区分大小写（`Content-Length` 和 `content-length` 是同一个），解析时统一转成小写保存，值去掉两端的空白。

## 3. HttpServer

### 3.1 每个连接的状态

解析器的状态属于连接：两个连接的请求同时到达一半，各自要记住各自的进度。tinynet 的 `TcpConnection` 有一个 `std::any` 类型的 context，HttpServer 在连接建立时放进去：

```cpp
struct ConnState {
    HttpParser parser;
    Clock::time_point lastActive;           // 第 3 篇的空闲检查
};

conn->setContext(ConnState{HttpParser(), Clock::now()});
```

### 3.2 处理请求：流水线和持久连接

```cpp
void HttpServer::onMessage(const TcpConnectionPtr& conn, Buffer* buf)
{
    auto* state = std::any_cast<ConnState>(conn->context());
    state->lastActive = Clock::now();
    Buffer out;
    bool close = false;
    // 一次可能收到好几个请求（流水线），逐个处理，响应攒在一起发送
    while (!close) {
        HttpParser::Result r = state->parser.parse(buf);
        if (r == HttpParser::Result::kNeedMore) break;
        if (r == HttpParser::Result::kBadRequest) {
            HttpResponse resp(true);
            resp.setStatus(400, "Bad Request");
            resp.appendTo(&out);
            close = true;
            break;
        }
        const HttpRequest& req = state->parser.request();
        HttpResponse resp(!req.keepAlive());
        if (httpCallback_) httpCallback_(req, &resp);
        else resp.setStatus(404, "Not Found");
        resp.appendTo(&out);
        close = resp.closeConnection();
        state->parser.reset();
    }
    if (out.readableBytes() > 0) conn->send(&out);
    if (close) conn->shutdown();            // 发完响应以后关闭写端
}
```

**流水线**（pipelining）：客户端可以不等响应，连续发出几个请求。服务器必须**按顺序**回复。这里用一个循环把输入缓冲区中所有完整的请求都处理掉，响应先攒在 `out` 中，最后一次 `send`：几个响应只需要一次 `write` 系统调用。下一篇会看到，这对性能有很大的影响。

**持久连接**（keep-alive）：HTTP/1.1 默认保持连接，除非请求中有 `Connection: close`；HTTP/1.0 默认关闭，除非有 `Connection: keep-alive`：

```cpp
bool HttpRequest::keepAlive() const
{
    std::string conn = header("connection");     // 转成小写后比较
    std::transform(conn.begin(), conn.end(), conn.begin(), [](unsigned char c) { return std::tolower(c); });
    if (version == "HTTP/1.0") return conn == "keep-alive";
    return conn != "close";
}
```

需要关闭时用 `shutdown` 而不是 `forceClose`：上一篇说过，`shutdown` 会等输出缓冲区发完，保证客户端收到完整的响应。

测试 `PipelinedThenClose`：一次发送两个请求，第二个带 `Connection: close`，客户端要收到两个响应，然后连接关闭。

### 3.3 生成响应

```cpp
void HttpResponse::appendTo(Buffer* out) const
{
    out->append("HTTP/1.1 " + std::to_string(status_) + " " + reason_ + "\r\n");
    out->append(closeConnection_ ? "Connection: close\r\n" : "Connection: keep-alive\r\n");
    out->append("Content-Length: " + std::to_string(body_.size()) + "\r\n");
    for (const auto& [name, value] : headers_) out->append(name + ": " + value + "\r\n");
    out->append("\r\n");
    out->append(body_);
}
```

`Content-Length` 是持久连接能工作的前提：客户端靠它知道这个响应在哪里结束、下一个响应从哪里开始。没有它（也没有 chunked 编码），就只能用关闭连接来表示响应结束。

## 4. 测试

HTTP 部分有 10 个测试：

| 测试 | 检查什么 |
|---|---|
| `HttpParser.CompleteRequest` | 方法、路径、查询字符串、版本、头部（名字转小写，值去空白） |
| `HttpParser.ByteByByte` | 一个字节一个字节地到达 |
| `HttpParser.Pipelined` | 一次到达两个请求 |
| `HttpParser.BadRequests` | 5 种格式错误 |
| `HttpParser.HeaderTooLong` | 头部超过 8 KB |
| `HttpRequest.KeepAlive` | HTTP/1.0 和 1.1 的默认行为 |
| `HttpServer.KeepAlive` | 同一个连接上依次发两个请求，逐字比较响应 |
| `HttpServer.PipelinedThenClose` | 流水线，最后一个请求要求关闭 |
| `HttpServer.BadRequestGets400` | 错误的请求得到 400，连接关闭 |
| `HttpServer.IdleTimeout` | 空闲超时，中间的请求让计时重新开始 |

后 4 个启动真正的服务器（端口 0，由系统分配），用阻塞的套接字作为客户端。

## 5. 和真正的服务器的差距

tinynet 的 HttpServer 只是一个演示，离生产环境还很远：

- 不支持 `Transfer-Encoding: chunked` 的请求正文，也不能流式地发送很大的响应（整个响应要先放进内存）；
- 路由是业务代码中的 `if`，没有静态文件、没有 HTTPS；
- 解析器对格式的检查很宽松：比如请求的目标中有空格、头部的值中有控制字符，都没有拒绝。真正的解析器（比如 Node.js 的 llhttp）对每个字节都有严格的规定，宽松的解析是很多安全漏洞（HTTP 请求走私）的来源。

但结构是一样的：**事件循环 + 每个连接的缓冲区 + 增量的协议解析 + 业务回调**。把 HTTP 解析器换成别的协议的解析器（Redis 的 RESP、自定义的二进制协议），就是另一种服务器。

## 6. 常见错误

1. **假设一次 `onMessage` 就是一个完整的请求**。请求被拆开或合在一起时出错。
2. **不限制请求头的长度**。一个不发 `\r\n` 的客户端就能耗尽服务器的内存。
3. **流水线时只处理第一个请求**。缓冲区中剩下的请求要等下一次有数据到达才处理，而客户端在等响应，不会再发数据，连接就卡住了。
4. **响应中没有 `Content-Length`**，却保持连接。客户端不知道响应在哪里结束。
5. **回复错误请求以后没有关闭连接**。解析器的状态已经乱了，后面的数据无法正确解析。

## 7. 小结

- 非阻塞服务器的协议解析必须是增量的：用状态机记住进度，数据不够就返回，下次接着解析；
- 请求头、正文都要限制长度；
- 一次处理缓冲区中所有完整的请求（流水线），响应攒在一起发送；
- 持久连接靠 `Content-Length` 区分响应的边界，关闭时用 `shutdown` 保证响应发完。

## 8. 练习

1. `HttpParser` 在头部结束、正文还没收完时返回 `kNeedMore`。如果客户端声明了 `Content-Length: 1000000`，然后只发了 10 个字节就不发了，服务器会怎样？怎样避免这种连接一直占着资源？
2. 给 HttpServer 加上对 `Expect: 100-continue` 的支持：客户端上传大文件前会先发送头部，等服务器回复 `HTTP/1.1 100 Continue` 再发送正文。应该在解析器的哪个状态中处理？
3. 把解析器改成支持 `Transfer-Encoding: chunked` 的请求正文，需要增加哪些状态？

<details>
<summary>点击查看答案</summary>

1. 解析器停在 `kBody` 状态，一直等剩下的数据，连接一直占着。第 3 篇的空闲超时可以处理这种情况：一段时间内没有任何数据到达，就关闭连接。更严格的服务器还会限制「读完一个请求」的总时间（比如 Nginx 的 `client_body_timeout`），防止对方每隔几秒发一个字节来绕过空闲超时（这种攻击叫 Slowloris）。

2. 在头部结束、准备进入 `kBody` 状态的时候：如果请求中有 `Expect: 100-continue`，解析器返回一个新的结果，比如 `kExpectContinue`，HttpServer 收到后先发送 `HTTP/1.1 100 Continue\r\n\r\n`（也可以根据头部判断是否接受这个上传，不接受就直接回复 413 等错误），再继续解析正文。

3. chunked 编码的正文由若干块组成，每块是「十六进制的长度 `\r\n` 数据 `\r\n`」，最后是长度为 0 的块，后面可能还有尾部的头部，再一个空行。需要增加的状态：读块的长度行、读块的数据（加上结尾的 `\r\n`）、读尾部的头部。每个状态都和现有的一样，数据不够时返回 `kNeedMore`。同时要限制所有块加起来的总长度。

</details>

下一篇：[压测与性能分析](/posts/srv-benchmark/)。四十多万请求每秒，时间都花在了哪里？
