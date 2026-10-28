---
title: HTTP 请求与 JSON
date: 2026-10-29 11:00:00
updated: 2026-10-29 11:00:00
categories:
  - 客户端开发
  - 网络与数据
tags:
  - Qt
  - C++
  - HTTP
  - JSON
order: 3
description: 用 QNetworkAccessManager 请求一个返回 JSON 的接口：get 立即返回一个 QNetworkReply，finished 信号到来时读出正文，用 QJsonDocument 解析。同时发出 12 个各要 500 毫秒的请求，前 6 个在 500 毫秒完成，后 6 个在 1000 毫秒完成，服务器端同时处理的请求最多 6 个：Qt 对同一个服务器最多开 6 个连接。设置 2 秒的传输超时，等 5 秒的请求在 1.9 秒时以 Operation timed out 结束。404 和 401 时 error() 不为 0，但正文照样能读出来；302 重定向被自动跟随。JSON 被截断时，不检查错误的代码拿到的是一个空文档，name 是空字符串，online 是 false，看起来像是「没有名字、不在线」。最后，忘了 deleteLater，1000 个请求结束后 1000 个 QNetworkReply 还挂在 manager 下面。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用 `QNetworkAccessManager` 发送 GET、POST 请求，在 `finished` 信号中处理结果；会区分网络错误和 HTTP 状态码，知道出错时也能读到正文；会设置超时，知道并发连接的限制和重定向的默认行为；会用 `QJsonDocument`、`QJsonObject`、`QJsonArray` 解析和生成 JSON，并检查解析错误；知道每个 `QNetworkReply` 都要 `deleteLater()`。
>
> **前置知识**：本章前两篇；计算机网络专栏《HTTP/1.1》（请求、响应、状态码、持久连接）、《HTTPS 与 TLS》。
>
> **实验环境**：macOS 27.0，Qt 6.12.0。服务器是用 Python 标准库 `http.server` 写的一个测试服务器（支持 HTTP/1.1 持久连接，每个请求一个线程），运行在本机，提供几个接口：`/contacts` 返回联系人列表，`/slow?ms=n` 等 n 毫秒再返回，`/login` 检查用户名密码，`/old-contacts` 重定向到 `/contacts`，`/broken` 返回一段被截断的 JSON。

## 1. 第一个请求

前两篇用 TCP 长连接收发聊天消息。客户端的另一类常见需求是调用服务器的 **HTTP 接口**（常说的 Web API、REST API）：登录、获取联系人列表、上传头像、检查更新。这些都是「发一个请求，等一个响应」，用 HTTP 最方便。

Qt 中发 HTTP 请求用 **`QNetworkAccessManager`**（常简称 QNAM）。和 `QTcpSocket` 一样，它是异步的：

```cpp
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

QNetworkAccessManager *nam = new QNetworkAccessManager(&app);   // 整个程序通常只要一个

QNetworkReply *reply = nam->get(QNetworkRequest(QUrl(base + "/contacts")));
QObject::connect(reply, &QNetworkReply::finished, [reply] {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        std::printf("出错：%s\n", qPrintable(reply->errorString()));
        return;
    }
    int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QJsonArray contacts = QJsonDocument::fromJson(reply->readAll()).array();
    std::printf("状态码 %d，%lld 个联系人\n", status, (long long)contacts.size());
    for (const QJsonValue &v : contacts) {
        QJsonObject c = v.toObject();
        std::printf("  #%d %s %s\n", c["id"].toInt(), qPrintable(c["name"].toString()),
                    c["online"].toBool() ? "在线" : "离线");
    }
});
```

服务器返回的是：

```json
[{"id": 1, "name": "张三", "online": true}, {"id": 2, "name": "李四", "online": false}]
```

```
状态码 200，2 个联系人
  #1 张三 在线
  #2 李四 离线
```

- **`get()` 立即返回一个 `QNetworkReply`**，代表这个正在进行的请求。请求完成（成功或失败）时，它发出 `finished` 信号。
- 和 `QTcpSocket` 不同，`finished` 时**整个响应已经收全了**，`readAll()` 读到的就是完整的正文。HTTP 的响应有 `Content-Length` 或分块编码，QNAM 自己处理了分帧，不需要上一篇那样的缓冲区。
- **`reply->deleteLater()`**：`QNetworkReply` 是 QNAM 创建的，处理完必须由我们删除，第 5 节会看到不删的后果。用 `deleteLater()` 而不是 `delete`，因为我们正在它自己发出的信号的槽中（第一章《对象树与内存管理》第 4.2 节）。
- 一个程序通常只需要**一个** `QNetworkAccessManager`：它内部维护连接池、缓存、Cookie，多个请求共享同一个管理器，才能复用连接。

### 1.1 JSON

Qt 的 JSON 类：

| 类 | 对应 JSON 的 |
|---|---|
| `QJsonDocument` | 整个文档，`fromJson()` 解析，`toJson()` 生成 |
| `QJsonObject` | 对象 `{...}`，用 `obj["key"]` 访问 |
| `QJsonArray` | 数组 `[...]` |
| `QJsonValue` | 任意一个值，用 `toString()`、`toInt()`、`toBool()`、`toObject()` 等取出 |

生成 JSON 用初始化列表最方便：

```cpp
QJsonObject body{{"user", "lucky"}, {"password", "12345678"}};
QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);   // {"password":"12345678","user":"lucky"}
```

`QJsonObject` 中的键按字母顺序保存，所以生成的 JSON 中 `password` 在 `user` 前面，和写的顺序不同。JSON 本身不规定键的顺序，服务器不应该依赖它。

## 2. 并发和超时

### 2.1 同时最多 6 个连接

同时发出 12 个请求，每个请求服务器都要处理 500 毫秒：

```cpp
for (int i = 0; i < 12; ++i) {
    QNetworkReply *reply = nam->get(QNetworkRequest(QUrl(base + "/slow?ms=500")));
    QObject::connect(reply, &QNetworkReply::finished, [reply, t0, i] {
        reply->deleteLater();
        std::printf("  请求 %2d 完成于 %4lld ms\n", i, clock_.elapsed() - t0);
    });
}
```

服务器还记录了自己同时处理的请求最多有几个：

```
  请求  4 完成于  502 ms
  请求  2 完成于  503 ms
  请求  3 完成于  503 ms
  请求  1 完成于  503 ms
  请求  5 完成于  503 ms
  请求  0 完成于  506 ms
  请求  6 完成于 1004 ms
  请求  7 完成于 1008 ms
  请求  8 完成于 1008 ms
  请求  9 完成于 1008 ms
  请求 10 完成于 1008 ms
  请求 11 完成于 1012 ms
  服务器端同时处理的请求最多 6 个
```

12 个请求分成了两批：前 6 个在 500 毫秒完成，后 6 个在 1000 毫秒完成。服务器端确认，同一时刻最多只有 6 个请求在处理。

QNAM 对**同一个服务器**（同一个主机和端口）最多同时打开 6 个 HTTP/1.1 连接，多出来的请求排队，等前面的连接空出来再发。浏览器也有同样的限制。这是 HTTP/1.1 的一个连接一次只能处理一个请求造成的（网络专栏《HTTP/1.1》的队头阻塞），HTTP/2 在一个连接上同时传输多个请求，就没有这个限制了（《HTTP/2 与 HTTP/3》）。QNAM 在服务器支持时会自动使用 HTTP/2（通过 HTTPS 协商），实验中的测试服务器只支持 HTTP/1.1。

这对客户端的设计有影响：如果同时要加载 100 个头像，后面的头像要等前面的加载完，一个慢请求就会拖住一个连接。通常的做法是让重要的请求（例如当前屏幕上可见的内容）先发出，不重要的晚一点发。

### 2.2 超时

请求一个要 5 秒才返回的接口，设置 2 秒的传输超时：

```cpp
QNetworkRequest req(QUrl(base + "/slow?ms=5000"));
req.setTransferTimeout(2000);
QNetworkReply *reply = nam->get(req);
```

```
  1902 ms 后 finished，error = 4（Operation timed out）
```

约 2 秒后（实测 1902 毫秒）请求被放弃，`error()` 是 `QNetworkReply::TimeoutError`（4）。

**传输超时**的含义是「这么长时间没有任何数据传输」，而不是整个请求的总时间：一个正在持续下载的大文件，只要数据还在流动，就不会超时。也可以用 `nam->setTransferTimeout()` 给管理器上的所有请求设置一个默认值。

**不设置的话，默认没有超时**。上一篇看到，网络有问题时可能几十秒、甚至永远等不到结果。所以每个请求都应该有超时，用户才不会对着转圈的界面干等。

## 3. 出错时也有正文

### 3.1 网络错误和 HTTP 状态码

请求一个不存在的路径，以及用错误和正确的密码各登录一次：

```cpp
QNetworkRequest req(QUrl(base + "/login"));
req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
QJsonObject body{{"user", "lucky"}, {"password", pw}};
QNetworkReply *r2 = nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
```

```
  /nothing: error = 203（Error transferring http://127.0.0.1:29201/nothing - server replied: Not Found），状态码 404，正文 {"error": "not found", "path": "/nothing"}
  POST /login（密码 wrong）：状态码 401，{"error":"用户名或密码错误"}
  POST /login（密码 12345678）：状态码 200，{"token":"abc123","user":"lucky"}
```

- HTTP 状态码是 4xx、5xx 时，`error()` **不是** `NoError`（404 对应 `ContentNotFoundError`，203），但**正文照样能读出来**，服务器在正文中给出的错误信息（「用户名或密码错误」）可以直接显示给用户。
- 所以第 1 节那种「`error()` 不为 0 就只打印 `errorString()`」的处理方式不够好：用户看到的是「Error transferring … server replied: Not Found」，而不是服务器给出的「用户名或密码错误」。

实际的程序中要区分两类错误：

| 类型 | 判断 | 处理 |
|---|---|---|
| 网络错误：连不上、超时、TLS 证书错误 | `error()` 不为 0，**没有**状态码（`HttpStatusCodeAttribute` 无效） | 提示「网络不可用」，可以重试 |
| HTTP 错误：4xx、5xx | 有状态码 | 4xx 是请求的问题（密码错、没有权限），显示服务器的错误信息，重试没有用；5xx 是服务器的问题，可以稍后重试 |

### 3.2 重定向

```
== 5. GET /old-contacts（302 到 /contacts）
  状态码 200，最终的 URL /contacts，91 字节
```

服务器返回 302，QNAM 自动跟随到了新地址，最后拿到的是 `/contacts` 的内容，`reply->url()` 是最终的地址。Qt 6 默认跟随「不会降低安全性」的重定向（`QNetworkRequest::NoLessSafeRedirectPolicy`）：从 HTTPS 重定向到 HTTP 不会被跟随，避免数据在不知不觉中以明文传输。

## 4. JSON 解析要检查错误

服务器（或者中间的代理）出了问题，返回了一段被截断的 JSON：

```
{"id": 1, "name": "张三", "online": tru
```

```cpp
QByteArray data = reply->readAll();
QJsonDocument d1 = QJsonDocument::fromJson(data);
std::printf("  不检查错误：isNull = %d，name = \"%s\"，online = %d\n", d1.isNull(),
            qPrintable(d1["name"].toString()), d1["online"].toBool());

QJsonParseError err;
QJsonDocument::fromJson(data, &err);
std::printf("  检查错误：%s，位置 %d\n", qPrintable(err.errorString()), err.offset);
```

```
  不检查错误：isNull = 1，name = ""，online = 0
  检查错误：illegal value，位置 39
```

解析失败时，`fromJson` 返回一个**空的文档**，不会抛出异常，也不会崩溃。从空文档中取值，`toString()` 返回空字符串，`toBool()` 返回 `false`，`toInt()` 返回 0。于是程序会认为「这个联系人没有名字，不在线」，而不是「数据出错了」。这种错误不会让程序崩溃，却会把错误的数据显示给用户，甚至存进本地数据库。

所以：

- 解析时传入 `QJsonParseError`，失败时按错误处理（`err.offset` 指出了出错的位置，方便调试）；
- 取值前检查类型和是否存在：`obj.contains("name")`、`obj["name"].isString()`；
- 必需的字段缺失时，宁可报错，也不要用默认值继续。

## 5. 每个 reply 都要 deleteLater

最后一个实验：发出 1000 个请求，在 `finished` 中读取了数据，但没有调用 `deleteLater()`。连续做两轮：

```cpp
QObject::connect(reply, &QNetworkReply::finished, [reply] {
    reply->readAll();                       // 读了数据，但没有 deleteLater
});
// 全部完成后：
std::printf("  全部完成后，manager 下面还有 %lld 个 QNetworkReply，内存 +%.1f MB\n",
            (long long)nam->findChildren<QNetworkReply *>().size(), rssMB() - before);
```

```
  全部完成后，manager 下面还有 1000 个 QNetworkReply，内存 +15.8 MB
  全部完成后，manager 下面还有 2000 个 QNetworkReply，内存 +9.5 MB
```

加上 `reply->deleteLater()` 再做两轮：

```
  全部完成后，manager 下面还有 0 个 QNetworkReply，内存 +13.2 MB
  全部完成后，manager 下面还有 0 个 QNetworkReply，内存 +0.0 MB
```

（第一轮的内存数字中包含了建立连接、初始化等一次性的开销，看第二轮更准确。）

`QNetworkReply` 是 QNAM 的子对象，请求结束后不会自动删除，而是一直挂在 QNAM 下面，直到 QNAM 被删除，可 QNAM 通常和程序活得一样长。每 1000 个请求多占约 10 MB。这正是第一章说的那种泄漏：对象还在对象树中，泄漏检查工具发现不了。客户端程序一开就是几天，每隔几秒刷新一次状态，内存就会一直涨。

Qt 6 也可以让 QNAM 自动删除：`nam->setAutoDeleteReplies(true)`，或者对单个请求设置 `QNetworkRequest::AutoDeleteReplyOnFinishAttribute`。

## 6. 小结

- `QNetworkAccessManager` 发送 HTTP 请求，`get` / `post` 立即返回 `QNetworkReply`，`finished` 时正文已经完整，直接 `readAll()`。整个程序共用一个 QNAM。
- QNAM 对同一个服务器最多同时打开 6 个 HTTP/1.1 连接，多余的请求排队。
- 默认没有超时，每个请求都应该用 `setTransferTimeout` 设置超时。
- HTTP 状态码为 4xx、5xx 时 `error()` 不为 0，但正文可以读出来，用于显示服务器的错误信息；要区分网络错误和 HTTP 错误，分别处理。默认跟随不降低安全性的重定向。
- `QJsonDocument::fromJson` 解析失败时返回空文档，取出的值都是默认值，看起来像正常数据。必须用 `QJsonParseError` 检查。
- 每个 `QNetworkReply` 都要 `deleteLater()`（或打开自动删除），否则会一直挂在 QNAM 下面。

客户端从服务器拿到的数据，总不能每次打开程序都重新下载一遍：联系人、聊天记录要保存在本地。下一篇用 SQLite。

## 练习

1. 写一个函数 `apiGet(path, onSuccess, onError)`，封装本文的错误处理：网络错误、HTTP 错误（读出服务器的 `error` 字段）、JSON 解析错误分别调用 `onError` 并给出合适的提示；成功时把解析好的 `QJsonDocument` 交给 `onSuccess`。
2. 把第 2.1 节的 12 个请求改成请求 12 个不同的端口（启动 12 个测试服务器），还会分成两批吗？为什么？
3. 用 `reply->abort()` 实现「用户离开页面时取消还没完成的请求」。`abort()` 之后 `finished` 还会发出吗？`error()` 是什么？
4. 用 `QNetworkReply::downloadProgress` 信号显示下载进度。服务器没有返回 `Content-Length`（使用分块编码）时，总大小是多少？
